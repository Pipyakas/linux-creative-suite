/*
 * wmi-unsecapp-srv.c -- Wine shim, out-of-process half.
 *
 * This is the LocalServer32 target for CLSID {49bd2028-1523-11d1-ad79-00c04fd8fdff}
 * ("Unsecured Apartment").  It is the stand-in for Windows' unsecapp.exe.
 *
 * the apps pass CLSCTX_LOCAL_SERVER, and Wine's com_get_class_object() only
 * consults InprocServer32 when the context contains CLSCTX_INPROC_SERVER
 * (dlls/combase/combase.c:  "if (clscontext & CLSCTX_INPROC_SERVER) { ...
 * open_key_for_clsid(rclsid, L"InprocServer32", ...) }").  With a bare 0x4
 * context the in-proc key is never read, so an in-proc-only shim cannot
 * satisfy the apps.  Wine then goes:
 *
 *   CoGetClassObject(LOCAL_SERVER)
 *     -> rpc_get_local_class_object()            (dlls/combase/rpc.c)
 *          -> rpcss_get_class_object()           -- asks rpcss whether some
 *             process already has the class registered
 *          -> create_server()                    -- launches LocalServer32
 *             with " -Embedding" if it is not
 *
 * so the shim has to be a real COM local server: register the class factory
 * with CoRegisterClassObject(CLSCTX_LOCAL_SERVER) and pump messages.
 *
 * The implementation mirrors Wine's own programs/dllhost/dllhost.c (the
 * DllSurrogate host), which is the known-good example of this pattern in
 * Wine 11.19:
 *   - load wmi-unsecapp.dll and take its IClassFactory from DllGetClassObject
 *   - wrap it in a factory whose IMarshal forwards marshalling to the DLL
 *     factory, so the client talks to the DLL factory object directly
 *   - CoRegisterClassObject with REGCLS_MULTIPLEUSE
 *   - pump messages until our class object is revoked
 *
 * Build: see build.sh.  Install: see install.sh.
 */

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <initguid.h>

DEFINE_GUID(CLSID_UnsecuredApartment,
    0x49bd2028, 0x1523, 0x11d1, 0xad, 0x79, 0x00, 0xc0, 0x4f, 0xd8, 0xfd, 0xff);

static void trace(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    HANDLE h;
    DWORD written;

    h = CreateFileA("wmi-unsecapp-srv.log", GENERIC_WRITE, FILE_SHARE_READ,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;

    SetFilePointer(h, 0, NULL, FILE_END);
    va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap);
    va_end(ap);
    lstrcatA(buf, "\r\n");
    WriteFile(h, buf, lstrlenA(buf), &written, NULL);
    CloseHandle(h);
}

/* ------------------------------------------------------------------ *
 * Wrapper factory: IClassFactory + IMarshal (same shape as dllhost.c).
 * ------------------------------------------------------------------ */

typedef struct CServerFactory
{
    IClassFactory IClassFactory_iface;  /* must be first */
    IMarshal IMarshal_iface;
    IClassFactory *dll_factory;
    LONG ref;
} CServerFactory;

static inline CServerFactory *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, CServerFactory, IClassFactory_iface);
}

static inline CServerFactory *impl_from_IMarshal(IMarshal *iface)
{
    return CONTAINING_RECORD(iface, CServerFactory, IMarshal_iface);
}

static ULONG WINAPI sf_AddRef(IClassFactory *iface);
static ULONG WINAPI sf_Release(IClassFactory *iface);

static HRESULT WINAPI sf_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    CServerFactory *This = impl_from_IClassFactory(iface);

    if (!ppv) return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *ppv = &This->IClassFactory_iface;
        sf_AddRef(iface);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_IMarshal))
    {
        *ppv = &This->IMarshal_iface;
        sf_AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI sf_AddRef(IClassFactory *iface)
{
    CServerFactory *This = impl_from_IClassFactory(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI sf_Release(IClassFactory *iface)
{
    CServerFactory *This = impl_from_IClassFactory(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    if (!ref)
    {
        if (This->dll_factory) IClassFactory_Release(This->dll_factory);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

/*
 * Never reached in practice: the client's proxy is bound straight to the
 * DLL factory (see marshal_MarshalInterface below), which is what dllhost.c
 * does as well.  Kept for completeness.
 */
static HRESULT WINAPI sf_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    CServerFactory *This = impl_from_IClassFactory(iface);
    trace("sf_CreateInstance (should be handled by the DLL factory)");
    return IClassFactory_CreateInstance(This->dll_factory, outer, riid, ppv);
}

static HRESULT WINAPI sf_LockServer(IClassFactory *iface, BOOL lock)
{
    (void)iface; (void)lock;
    return S_OK;
}

static const IClassFactoryVtbl ServerFactoryVtbl =
{
    sf_QueryInterface,
    sf_AddRef,
    sf_Release,
    sf_CreateInstance,
    sf_LockServer
};

static HRESULT WINAPI sm_QueryInterface(IMarshal *iface, REFIID riid, void **ppv)
{
    CServerFactory *This = impl_from_IMarshal(iface);
    return sf_QueryInterface(&This->IClassFactory_iface, riid, ppv);
}

static ULONG WINAPI sm_AddRef(IMarshal *iface)
{
    CServerFactory *This = impl_from_IMarshal(iface);
    return sf_AddRef(&This->IClassFactory_iface);
}

static ULONG WINAPI sm_Release(IMarshal *iface)
{
    CServerFactory *This = impl_from_IMarshal(iface);
    return sf_Release(&This->IClassFactory_iface);
}

static HRESULT WINAPI sm_GetUnmarshalClass(IMarshal *iface, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags, CLSID *clsid)
{
    (void)iface; (void)riid; (void)pv; (void)dest_context;
    (void)dest_context_data; (void)mshlflags;
    *clsid = CLSID_StdMarshal;
    return S_OK;
}

/* E_NOTIMPL, exactly as dllhost.c does: nothing here calls
 * CoGetMarshalSizeMax, and COM falls back to the standard sizer. */
static HRESULT WINAPI sm_GetMarshalSizeMax(IMarshal *iface, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags, DWORD *size)
{
    (void)iface; (void)riid; (void)pv; (void)dest_context;
    (void)dest_context_data; (void)mshlflags; (void)size;
    return E_NOTIMPL;
}

/* Talk to the DLL factory, not to this wrapper -- this is what makes the
 * client's IClassFactory proxy land in wmi-unsecapp.dll. */
static HRESULT WINAPI sm_MarshalInterface(IMarshal *iface, IStream *stream, REFIID riid,
        void *pv, DWORD dest_context, void *dest_context_data, DWORD mshlflags)
{
    CServerFactory *This = impl_from_IMarshal(iface);
    return CoMarshalInterface(stream, riid, (IUnknown *)This->dll_factory,
                              dest_context, dest_context_data, mshlflags);
}

static HRESULT WINAPI sm_UnmarshalInterface(IMarshal *iface, IStream *stream, REFIID riid, void **ppv)
{
    (void)iface; (void)stream; (void)riid; (void)ppv;
    return E_NOTIMPL;
}

static HRESULT WINAPI sm_ReleaseMarshalData(IMarshal *iface, IStream *stream)
{
    (void)iface; (void)stream;
    return S_OK;
}

static HRESULT WINAPI sm_DisconnectObject(IMarshal *iface, DWORD reserved)
{
    (void)iface; (void)reserved;
    return S_OK;
}

static const IMarshalVtbl ServerMarshalVtbl =
{
    sm_QueryInterface,
    sm_AddRef,
    sm_Release,
    sm_GetUnmarshalClass,
    sm_GetMarshalSizeMax,
    sm_MarshalInterface,
    sm_UnmarshalInterface,
    sm_ReleaseMarshalData,
    sm_DisconnectObject
};

/* ------------------------------------------------------------------ *
 * Helpers
 * ------------------------------------------------------------------ */

/* Load wmi-unsecapp.dll from the directory this EXE lives in. */
static HMODULE load_shim_dll(void)
{
    WCHAR path[MAX_PATH];
    WCHAR *slash;

    if (!GetModuleFileNameW(NULL, path, MAX_PATH))
        return NULL;

    slash = wcsrchr(path, L'\\');
    if (!slash) return NULL;
    slash[1] = 0;
    lstrcatW(path, L"wmi-unsecapp.dll");

    return LoadLibraryW(path);
}

/*
 * Message loop.
 *
 * Real Windows keeps unsecapp.exe resident; here that would leave a Wine
 * process (and its whole session) alive after the client exits, which is
 * untidy in a desktop prefix and can make `wine <app>` appear to hang.  So
 * the loop polls the DLL's live-instance count and shuts down after
 * WMI_UNSECAPP_IDLE_SECS (default 120) with no live instance.  A client that
 * still holds an instance -- or that creates one again later, which just
 * relaunches this EXE through LocalServer32 -- is unaffected.  Set it to 0
 * to stay resident forever.
 */
static void pump_messages(LONG (WINAPI *live_objects)(void))
{
    MSG msg;
    DWORD idle_secs = 120;
    DWORD idle_since = GetTickCount();
    BOOL quit = FALSE;
    char buf[32];

    if (GetEnvironmentVariableA("WMI_UNSECAPP_IDLE_SECS", buf, sizeof(buf)))
        idle_secs = (DWORD)atoi(buf);

    trace("message loop, idle timeout %lu s", idle_secs);

    while (!quit)
    {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 1000, QS_ALLINPUT);

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) { quit = TRUE; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        CoFreeUnusedLibraries();

        if (live_objects && live_objects() > 0)
            idle_since = GetTickCount();
        else if (idle_secs && GetTickCount() - idle_since > idle_secs * 1000)
        {
            trace("idle for %lu s with no live instance, exiting", idle_secs);
            break;
        }
    }
}

/* ------------------------------------------------------------------ *
 * Entry point
 * ------------------------------------------------------------------ */

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    typedef HRESULT (WINAPI *pDllGetClassObject)(REFCLSID, REFIID, void **);
    typedef LONG (WINAPI *pLiveObjects)(void);
    pDllGetClassObject get_class_object;
    pLiveObjects live_objects;
    CServerFactory *factory;
    HMODULE dll;
    IClassFactory *dll_factory = NULL;
    DWORD cookie = 0;
    HRESULT hr;

    (void)hinst; (void)prev; (void)show;

    /* Wine's create_server() launches us as "wmi-unsecapp-srv.exe -Embedding";
     * -RegServer/-UnregServer are accepted for parity with real COM servers. */
    if (cmdline && *cmdline)
    {
        if (!wcsicmp(cmdline, L"-RegServer") || !wcsicmp(cmdline, L"/RegServer"))
            return 0;
        if (!wcsicmp(cmdline, L"-UnregServer") || !wcsicmp(cmdline, L"/UnregServer"))
            return 0;
    }

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        trace("CoInitializeEx failed: %08lx", (unsigned long)hr);
        return 1;
    }

    dll = load_shim_dll();
    if (!dll)
    {
        trace("could not load wmi-unsecapp.dll (error %lu)", GetLastError());
        CoUninitialize();
        return 1;
    }

    get_class_object = (pDllGetClassObject)(void *)GetProcAddress(dll, "DllGetClassObject");
    if (!get_class_object)
    {
        trace("wmi-unsecapp.dll has no DllGetClassObject");
        CoUninitialize();
        return 1;
    }

    hr = get_class_object(&CLSID_UnsecuredApartment, &IID_IClassFactory, (void **)&dll_factory);
    if (FAILED(hr))
    {
        trace("DllGetClassObject failed: %08lx", (unsigned long)hr);
        CoUninitialize();
        return 1;
    }

    factory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*factory));
    if (!factory)
    {
        IClassFactory_Release(dll_factory);
        CoUninitialize();
        return 1;
    }

    factory->IClassFactory_iface.lpVtbl = &ServerFactoryVtbl;
    factory->IMarshal_iface.lpVtbl = &ServerMarshalVtbl;
    factory->dll_factory = dll_factory;
    factory->ref = 1;

    hr = CoRegisterClassObject(&CLSID_UnsecuredApartment,
                               (IUnknown *)&factory->IClassFactory_iface,
                               CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &cookie);
    if (FAILED(hr))
    {
        trace("CoRegisterClassObject failed: %08lx", (unsigned long)hr);
        sf_Release(&factory->IClassFactory_iface);
        CoUninitialize();
        return 1;
    }

    trace("registered CLSID {49bd2028-1523-11d1-ad79-00c04fd8fdff} cookie=%lu", cookie);

    live_objects = (pLiveObjects)(void *)GetProcAddress(dll, "WmiUnsecappLiveObjects");

    CoAddRefServerProcess();
    pump_messages(live_objects);
    CoReleaseServerProcess();

    CoRevokeClassObject(cookie);
    sf_Release(&factory->IClassFactory_iface);
    CoUninitialize();
    return 0;
}

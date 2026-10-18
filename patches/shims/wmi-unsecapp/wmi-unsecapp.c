/*
 * wmi-unsecapp.c -- Wine shim for the WMI "Unsecured Apartment" COM class.
 *
 *   CLSID {49bd2028-1523-11d1-ad79-00c04fd8fdff}   ("UnsecuredApartment",
 *          CLSID_UnsecuredApartment / CLSID_WbemUnsecuredApartment)
 *
 * Windows implements this CLSID with the out-of-process COM server
 * C:\Windows\System32\wbem\unsecapp.exe, registered under LocalServer32.
 * Wine has never implemented it (WineHQ bug 30439), which makes every 2026-era
 * app in the suite that touches WMI print
 *
 *     err:ole:create_server class {49bd2028-...} not registered
 *     err:ole:com_get_class_object no class object {...} could be created for context 0x4
 *
 * and the raster app dies at startup.
 *
 * This file builds the *in-process* half of the shim: a normal COM in-proc
 * server (DllGetClassObject + class factory + object) so that a
 * CLSCTX_INPROC_SERVER request for the CLSID succeeds.
 * The *out-of-process* half is wmi-unsecapp-srv.c (LocalServer32), which is
 * what CLSCTX_LOCAL_SERVER (0x4, the context the apps actually ask for) needs.
 *
 * Build: see build.sh.  Install: see install.sh.
 */

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <initguid.h>

/* ------------------------------------------------------------------ *
 * GUIDs, verbatim from the WMI SDK (wbemcli.idl / wbemcli.h).
 * ------------------------------------------------------------------ */

/* The class itself.  Same UUID as CLSID_WbemUnsecuredApartment. */
DEFINE_GUID(CLSID_UnsecuredApartment,
    0x49bd2028, 0x1523, 0x11d1, 0xad, 0x79, 0x00, 0xc0, 0x4f, 0xd8, 0xfd, 0xff);

/* interface IUnsecuredApartment : IUnknown */
DEFINE_GUID(IID_IUnsecuredApartment,
    0x1cfaba8c, 0x1523, 0x11d1, 0xad, 0x79, 0x00, 0xc0, 0x4f, 0xd8, 0xfd, 0xff);

/* interface IWbemUnsecuredApartment : IUnsecuredApartment */
DEFINE_GUID(IID_IWbemUnsecuredApartment,
    0x31739d04, 0x3471, 0x4cf4, 0x9a, 0x7c, 0x57, 0xa4, 0x4a, 0xe7, 0x19, 0x56);

/* ------------------------------------------------------------------ *
 * Hand-written interface declarations (vtable shapes from wbemcli.idl).
 * Declared here rather than including wbemcli.h so the file stays
 * self-contained and compiles against any mingw revision.
 * ------------------------------------------------------------------ */

typedef struct IUnsecuredApartment IUnsecuredApartment;
typedef struct IWbemUnsecuredApartment IWbemUnsecuredApartment;

typedef struct IUnsecuredApartmentVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IUnsecuredApartment *iface, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IUnsecuredApartment *iface);
    ULONG   (STDMETHODCALLTYPE *Release)(IUnsecuredApartment *iface);
    HRESULT (STDMETHODCALLTYPE *CreateObjectStub)(IUnsecuredApartment *iface,
                                                  IUnknown *pObject, IUnknown **ppStub);
} IUnsecuredApartmentVtbl;

struct IUnsecuredApartment { const IUnsecuredApartmentVtbl *lpVtbl; };

typedef struct IWbemUnsecuredApartmentVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWbemUnsecuredApartment *iface, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IWbemUnsecuredApartment *iface);
    ULONG   (STDMETHODCALLTYPE *Release)(IWbemUnsecuredApartment *iface);
    HRESULT (STDMETHODCALLTYPE *CreateObjectStub)(IWbemUnsecuredApartment *iface,
                                                  IUnknown *pObject, IUnknown **ppStub);
    HRESULT (STDMETHODCALLTYPE *CreateSinkStub)(IWbemUnsecuredApartment *iface,
                                                IUnknown *pSink, DWORD dwFlags,
                                                LPCWSTR wszReserved, IUnknown **ppStub);
} IWbemUnsecuredApartmentVtbl;

struct IWbemUnsecuredApartment { const IWbemUnsecuredApartmentVtbl *lpVtbl; };

/* ------------------------------------------------------------------ *
 * Tracing
 * ------------------------------------------------------------------ */

static void trace(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    /* Off by default: the raster app already emits plenty of noise. */
    if (!GetEnvironmentVariableA("WMI_UNSECAPP_TRACE", NULL, 0))
        return;

    va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap);
    va_end(ap);

    OutputDebugStringA("[wmi-unsecapp] ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

/* ------------------------------------------------------------------ *
 * The class object.
 *
 * NOTE: every method here is a *stub*.  Nothing does WMI work.  The only
 * behaviour that matters for the apps' startup path is that
 * CoCreateInstance() succeeds and hands back an object whose IUnknown is
 * well behaved; see WMI-UNSECAPP.md for the reasoning.
 * ------------------------------------------------------------------ */

typedef struct CUnsecuredApartment
{
    IWbemUnsecuredApartment IWbemUnsecuredApartment_iface; /* must be first */
    LONG ref;
} CUnsecuredApartment;

static inline CUnsecuredApartment *impl_from_IWbemUnsecuredApartment(IWbemUnsecuredApartment *iface)
{
    return CONTAINING_RECORD(iface, CUnsecuredApartment, IWbemUnsecuredApartment_iface);
}

/* Instances handed out (and not yet released).  Read by the LocalServer32
 * process through WmiUnsecappLiveObjects() so it can tell when it has gone
 * idle and shut itself down. */
static LONG live_objects;

static ULONG WINAPI ua_AddRef(IWbemUnsecuredApartment *iface);
static ULONG WINAPI ua_Release(IWbemUnsecuredApartment *iface);

/* --- IUnknown ------------------------------------------------------ */

static HRESULT WINAPI ua_QueryInterface(IWbemUnsecuredApartment *iface, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IUnsecuredApartment) ||
        IsEqualIID(riid, &IID_IWbemUnsecuredApartment))
    {
        *ppv = iface;
        ua_AddRef(iface);
        return S_OK;
    }

    trace("QueryInterface: E_NOINTERFACE");
    return E_NOINTERFACE;
}

static ULONG WINAPI ua_AddRef(IWbemUnsecuredApartment *iface)
{
    CUnsecuredApartment *This = impl_from_IWbemUnsecuredApartment(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI ua_Release(IWbemUnsecuredApartment *iface)
{
    CUnsecuredApartment *This = impl_from_IWbemUnsecuredApartment(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    if (!ref)
    {
        InterlockedDecrement(&live_objects);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

/* --- IUnsecuredApartment ------------------------------------------- */

/*
 * STUB.  Real unsecapp.exe hands the caller a marshalled proxy for pObject
 * so that the *sink's* callbacks run in a neutral apartment instead of the
 * caller's.  We are already a different process from the caller, so the
 * honest minimal behaviour is to hand pObject back: the caller's Windows
 * code only cares that it gets a valid IUnknown back and that COM's
 * reference counting stays balanced.
 *
 * pObject has already been unmarshalled into this process by COM, so it is
 * a local proxy here; returning it simply re-marshals it to the caller.
 */
static HRESULT WINAPI ua_CreateObjectStub(IWbemUnsecuredApartment *iface,
                                          IUnknown *pObject, IUnknown **ppStub)
{
    trace("CreateObjectStub(%p)", pObject);

    if (!ppStub) return E_POINTER;
    *ppStub = NULL;
    if (!pObject) return E_POINTER;

    *ppStub = pObject;
    IUnknown_AddRef(pObject);
    return S_OK;
}

/*
 * STUB.  Same idea as CreateObjectStub, for IWbemObjectSink.  dwFlags is
 * WMI_UNSECURED_APARTMENT_* (0 == "block until the sink is called"), and
 * wszReserved is always NULL.  We ignore both and return the sink itself.
 */
static HRESULT WINAPI ua_CreateSinkStub(IWbemUnsecuredApartment *iface,
                                        IUnknown *pSink, DWORD dwFlags,
                                        LPCWSTR wszReserved, IUnknown **ppStub)
{
    trace("CreateSinkStub(%p, %lu, %p)", pSink, dwFlags, wszReserved);

    if (!ppStub) return E_POINTER;
    *ppStub = NULL;
    if (!pSink) return E_POINTER;

    *ppStub = pSink;
    IUnknown_AddRef(pSink);
    return S_OK;
}

static const IWbemUnsecuredApartmentVtbl UnsecuredApartmentVtbl =
{
    ua_QueryInterface,
    ua_AddRef,
    ua_Release,
    ua_CreateObjectStub,
    ua_CreateSinkStub
};

/* ------------------------------------------------------------------ *
 * Class factory
 * ------------------------------------------------------------------ */

typedef struct CUnsecuredApartmentFactory
{
    IClassFactory IClassFactory_iface;  /* must be first */
    LONG ref;
} CUnsecuredApartmentFactory;

static inline CUnsecuredApartmentFactory *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, CUnsecuredApartmentFactory, IClassFactory_iface);
}

static HRESULT WINAPI cf_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *ppv = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI cf_AddRef(IClassFactory *iface)
{
    CUnsecuredApartmentFactory *This = impl_from_IClassFactory(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI cf_Release(IClassFactory *iface)
{
    CUnsecuredApartmentFactory *This = impl_from_IClassFactory(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    if (!ref) HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

static HRESULT WINAPI cf_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    CUnsecuredApartment *obj;
    HRESULT hr;

    if (!ppv) return E_POINTER;
    *ppv = NULL;

    if (outer) return CLASS_E_NOAGGREGATION;

    obj = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*obj));
    if (!obj) return E_OUTOFMEMORY;

    obj->IWbemUnsecuredApartment_iface.lpVtbl = &UnsecuredApartmentVtbl;
    obj->ref = 1;
    InterlockedIncrement(&live_objects);

    hr = ua_QueryInterface(&obj->IWbemUnsecuredApartment_iface, riid, ppv);
    ua_Release(&obj->IWbemUnsecuredApartment_iface);

    trace("CreateInstance -> %08lx", (unsigned long)hr);
    return hr;
}

static HRESULT WINAPI cf_LockServer(IClassFactory *iface, BOOL lock)
{
    (void)iface; (void)lock;
    return S_OK;
}

static const IClassFactoryVtbl UnsecuredApartmentFactoryVtbl =
{
    cf_QueryInterface,
    cf_AddRef,
    cf_Release,
    cf_CreateInstance,
    cf_LockServer
};

/* ------------------------------------------------------------------ *
 * DLL entry points
 * ------------------------------------------------------------------ */

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hinst);
    return TRUE;
}

__declspec(dllexport)
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    CUnsecuredApartmentFactory *factory;
    HRESULT hr;

    if (!ppv) return E_POINTER;
    *ppv = NULL;

    if (!IsEqualCLSID(rclsid, &CLSID_UnsecuredApartment))
    {
        trace("DllGetClassObject: class not registered in this DLL");
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    factory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*factory));
    if (!factory) return E_OUTOFMEMORY;

    factory->IClassFactory_iface.lpVtbl = &UnsecuredApartmentFactoryVtbl;
    factory->ref = 1;

    hr = cf_QueryInterface(&factory->IClassFactory_iface, riid, ppv);
    cf_Release(&factory->IClassFactory_iface);
    return hr;
}

__declspec(dllexport)
HRESULT WINAPI DllCanUnloadNow(void)
{
    return live_objects ? S_FALSE : S_OK;
}

/* Not part of the COM contract: wmi-unsecapp-srv.exe polls this to decide
 * whether it is still needed. */
__declspec(dllexport)
LONG WINAPI WmiUnsecappLiveObjects(void)
{
    return live_objects;
}

/* ------------------------------------------------------------------ *
 * Self-registration (regsvr32).  install.sh does the same thing with
 * `wine reg add`; this exists so the DLL is also usable via regsvr32.
 * ------------------------------------------------------------------ */

static const WCHAR clsid_key[] =
    L"CLSID\\{49bd2028-1523-11d1-ad79-00c04fd8fdff}";

static HRESULT set_value(HKEY root, const WCHAR *sub, const WCHAR *name,
                         const WCHAR *value, DWORD type)
{
    HKEY key;
    LONG r = RegCreateKeyExW(root, sub, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    r = RegSetValueExW(key, name, 0, type, (const BYTE *)value,
                       (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(r);
}

__declspec(dllexport)
HRESULT WINAPI DllRegisterServer(void)
{
    WCHAR path[MAX_PATH], sub[MAX_PATH];
    HRESULT r;

    if (!GetModuleFileNameW(NULL, path, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    r = set_value(HKEY_CLASSES_ROOT, clsid_key, NULL,
                  L"WMI Unsecured Apartment (Wine shim)", REG_SZ);
    if (r != S_OK) return r;

    lstrcpynW(sub, clsid_key, MAX_PATH);
    if (set_value(HKEY_CLASSES_ROOT, sub, L"InprocServer32",
                  path, REG_SZ) != S_OK) return E_FAIL;
    lstrcatW(sub, L"\\InprocServer32");
    if (set_value(HKEY_CLASSES_ROOT, sub, L"ThreadingModel",
                  L"Both", REG_SZ) != S_OK) return E_FAIL;

    /* LocalServer32 (the important one) is written by install.sh, because
       the EXE path is a build-layout decision, not a DLL one. */
    return S_OK;
}

__declspec(dllexport)
HRESULT WINAPI DllUnregisterServer(void)
{
    RegDeleteTreeW(HKEY_CLASSES_ROOT, clsid_key);
    return S_OK;
}

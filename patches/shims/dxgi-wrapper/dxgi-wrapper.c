/*
 * dxgi-wrapper.c — a native dxgi.dll that forwards to DXVK's and makes its adapter
 * objects answer QueryInterface(IID_IDXCoreAdapter).
 *
 * Why: the suite's apps enumerate GPUs through DXGI and then ask each adapter for
 * IDXCoreAdapter, which on Windows is the same object (a DXGI adapter *is* a DXCore
 * adapter). DXVK's IDXGIAdapter does not implement it, so every raster-app launch logs
 *
 *   warn:  DxgiAdapter::QueryInterface: Unknown interface query
 *   warn:  f0db4c7f-fe5a-42a2-bd62-f2a6cf6fc83e      <- IID_IDXCoreAdapter
 *
 * and the raster app's own GPU probe helper then reports "# devices: 0" even though DXGI itself
 * enumerates both adapters and D3D11 creates a device at feature level 11_0.
 *
 * The IDXCoreAdapter handed back is the one from this project's own dxcore shim
 * (probe/other/dxcore-shim/dxcore-shim.c, installed as dxcore.dll), matched to the DXGI
 * adapter by vendor/device id. Everything else is a straight pass-through, so D3D9/D3D10/
 * D3D11 and the 18 working apps are unaffected.
 *
 * NOTE on the vtables: mingw-w64 16.x on this machine declares IDXGIAdapter1Vtbl with
 * GetParent and no GetDevice, and IDXGIFactory2Vtbl without the two window-resize
 * methods — neither matches the layout Windows and DXVK actually use. So both vtables are
 * declared here, in the real Windows order, and every call into DXVK goes through them.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <initguid.h>          /* make every DEFINE_GUID below a definition */
#include <dxgi.h>              /* DXGI_ADAPTER_DESC / _DESC1, IID_IDXGIAdapter*, … */
#include <dxgi1_2.h>           /* IID_IDXGIFactory2 only — see the vtable note above */
#include <stdint.h>
#include <string.h>

/* Wine's DXCore interfaces: vtable order and base IIDs, nothing re-declared. */
#include </usr/include/wine/windows/dxcore_interface.h>

/* GUIDs Wine's header lacks (Microsoft's published dxcore_interface.h has them). */
DEFINE_GUID(CLSID_DXCoreAdapterFactory,
    0x52868710, 0x5a9a, 0x473d, 0x8c, 0x9b, 0x55, 0xb5, 0xbf, 0x36, 0xd3, 0xb3);
DEFINE_GUID(IID_IDXCoreAdapter1,
    0xa0783366, 0xcfa3, 0x43be, 0x9d, 0x79, 0x55, 0xb2, 0xda, 0x97, 0xc6, 0x3c);

/* ------------------------------------------------------------------ */
/* Real Windows vtable layouts                                          */

typedef struct IDXGIAdapter1VtblReal {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *);
    ULONG   (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(void *, REFGUID, UINT, const void *);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(void *, REFGUID, const IUnknown *);
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(void *, REFGUID, UINT *, void *);
    HRESULT (STDMETHODCALLTYPE *SetParent)(void *, REFIID, void *);
    HRESULT (STDMETHODCALLTYPE *GetDevice)(void *, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *EnumOutputs)(void *, UINT, void **);
    HRESULT (STDMETHODCALLTYPE *GetDesc)(void *, DXGI_ADAPTER_DESC *);
    HRESULT (STDMETHODCALLTYPE *CheckInterfaceSupport)(void *, REFGUID, LARGE_INTEGER *);
    HRESULT (STDMETHODCALLTYPE *GetDesc1)(void *, DXGI_ADAPTER_DESC1 *);
} IDXGIAdapter1VtblReal;

typedef struct IDXGIFactory2VtblReal {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *);
    ULONG   (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(void *, REFGUID, UINT, const void *);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(void *, REFGUID, const IUnknown *);
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(void *, REFGUID, UINT *, void *);
    HRESULT (STDMETHODCALLTYPE *EnumAdapters)(void *, UINT, void **);
    HRESULT (STDMETHODCALLTYPE *MakeWindowAssociation)(void *, HWND, UINT);
    HRESULT (STDMETHODCALLTYPE *GetWindowAssociation)(void *, HWND *);
    HRESULT (STDMETHODCALLTYPE *CreateSwapChain)(void *, void *, void *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateSoftwareAdapter)(void *, HMODULE, void **);
    HRESULT (STDMETHODCALLTYPE *EnumAdapters1)(void *, UINT, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *IsCurrent)(void *);
    HRESULT (STDMETHODCALLTYPE *SetWindowResizeBehavior)(void *, HWND, UINT);
    HRESULT (STDMETHODCALLTYPE *GetWindowResizeBehavior)(void *, HWND, UINT *);
    BOOL    (STDMETHODCALLTYPE *IsWindowedStereoEnabled)(void *);
    HRESULT (STDMETHODCALLTYPE *CreateSwapChainForHwnd)(void *, void *, HWND, const void *, const void *, void **);
} IDXGIFactory2VtblReal;

typedef struct IDXGIAdapter1Real { const IDXGIAdapter1VtblReal *lpVtbl; } IDXGIAdapter1Real;
typedef struct IDXGIFactory2Real { const IDXGIFactory2VtblReal *lpVtbl; } IDXGIFactory2Real;

#define ADAPTER_VTBL(a)  ((const IDXGIAdapter1VtblReal *)(a)->lpVtbl)
#define FACTORY_VTBL(f) ((const IDXGIFactory2VtblReal *)(f)->lpVtbl)

/* ------------------------------------------------------------------ */
/* The real DXVK dxgi, loaded lazily from dxgi-dxvk.dll                 */

typedef HRESULT (WINAPI *PFN_CreateDXGIFactory)(REFIID, void **);
typedef HRESULT (WINAPI *PFN_CreateDXGIFactory1)(REFIID, void **);
typedef HRESULT (WINAPI *PFN_CreateDXGIFactory2)(REFIID, void **);
typedef HRESULT (WINAPI *PFN_DeclareAdapterRemovalSupport)(void);
typedef HRESULT (WINAPI *PFN_GetDebugInterface1)(REFIID, void **);
typedef HRESULT (WINAPI *PFN_D3D12GetInterface)(REFCLSID, REFIID, void **);

static struct {
    HMODULE dxgi, dxcore;
    PFN_CreateDXGIFactory         CreateDXGIFactory;
    PFN_CreateDXGIFactory1        CreateDXGIFactory1;
    PFN_CreateDXGIFactory2        CreateDXGIFactory2;
    PFN_DeclareAdapterRemovalSupport DeclareAdapterRemovalSupport;
    PFN_GetDebugInterface1        GetDebugInterface1;
    PFN_D3D12GetInterface         D3D12GetInterface;
    LONG done;
} g;

static BOOL ensure_real(void)
{
    if (g.done) return g.CreateDXGIFactory2 != NULL;
    InterlockedExchange(&g.done, 1);
    g.dxgi = LoadLibraryW(L"dxgi-dxvk.dll");
    if (g.dxgi) {
        g.CreateDXGIFactory = (PFN_CreateDXGIFactory)GetProcAddress(g.dxgi, "CreateDXGIFactory");
        g.CreateDXGIFactory1 = (PFN_CreateDXGIFactory1)GetProcAddress(g.dxgi, "CreateDXGIFactory1");
        g.CreateDXGIFactory2 = (PFN_CreateDXGIFactory2)GetProcAddress(g.dxgi, "CreateDXGIFactory2");
        g.DeclareAdapterRemovalSupport =
            (PFN_DeclareAdapterRemovalSupport)GetProcAddress(g.dxgi, "DXGIDeclareAdapterRemovalSupport");
        g.GetDebugInterface1 = (PFN_GetDebugInterface1)GetProcAddress(g.dxgi, "DXGIGetDebugInterface1");
    }
    if (!g.CreateDXGIFactory2) { if (g.dxgi) FreeLibrary(g.dxgi); g.dxgi = NULL; return FALSE; }
    /* The DXCore side is optional: without it the wrapper still forwards everything. */
    g.dxcore = LoadLibraryW(L"dxcore.dll");
    if (g.dxcore)
        g.D3D12GetInterface = (PFN_D3D12GetInterface)GetProcAddress(g.dxcore, "D3D12GetInterface");
    return TRUE;
}

/* DXVK reaches back into "dxgi.dll" while it builds its own factory — that is this DLL.
 * Without this guard the wrapper calls DXVK, DXVK calls back into the wrapper, and the
 * factory creation deadlocks (verified: dxgi-probe hung at CreateDXGIFactory1). While the
 * flag is set, every export of ours hands DXVK's result straight back, unwrapped. */
static __declspec(thread) int inside_dxvk;

static IDXGIFactory2Real *factory_real_via(int which, REFIID riid, HRESULT *hr_out)
{
    IDXGIFactory2Real *real = NULL;
    HRESULT hr;
    inside_dxvk++;
    switch (which) {
        case 1:  hr = g.CreateDXGIFactory1(riid, (void **)&real); break;
        case 2:  hr = g.CreateDXGIFactory2(riid, (void **)&real); break;
        default: hr = g.CreateDXGIFactory (&IID_IDXGIFactory, (void **)&real);
                 if (SUCCEEDED(hr)) {
                     /* the legacy entry point only gives IDXGIFactory; ask the real
                      * object for IDXGIFactory2 so the wrapper can forward everything */
                     IDXGIFactory2Real *as2 = NULL;
                     HRESULT h2 = FACTORY_VTBL(real)->QueryInterface(real, &IID_IDXGIFactory2,
                                                                          (void **)&as2);
                     if (SUCCEEDED(h2)) {
                         FACTORY_VTBL(real)->Release(real);
                         real = as2;
                     } else {
                         /* leave `real` as the IDXGIFactory; the wrapper only ever calls
                          * slots that exist on both interfaces */
                     }
                 }
                 break;
    }
    inside_dxvk--;
    *hr_out = hr;
    return real;
}

/* ------------------------------------------------------------------ */
/* Match a DXGI adapter to the DXCore adapter                           */

/* Returns an AddRef'd IDXCoreAdapter for the given DXGI adapter, or NULL. */
static IDXCoreAdapter *dxcore_adapter_for(const DXGI_ADAPTER_DESC1 *d)
{
    if (!ensure_real() || !g.D3D12GetInterface) return NULL;

    IDXCoreAdapterFactory *factory = NULL;
    if (FAILED(g.D3D12GetInterface(&CLSID_DXCoreAdapterFactory,
                                   &IID_IDXCoreAdapterFactory, (void **)&factory)))
        return NULL;

    IDXCoreAdapterList *list = NULL;
    HRESULT hr = IDXCoreAdapterFactory_CreateAdapterList(factory, 0, NULL,
                                    &IID_IDXCoreAdapterList, (void **)&list);
    IDXCoreAdapterFactory_Release(factory);
    if (FAILED(hr)) return NULL;

    IDXCoreAdapter *found = NULL;
    for (uint32_t i = 0; found == NULL; i++) {
        IDXCoreAdapter *a = NULL;
        if (FAILED(IDXCoreAdapterList_GetAdapter(list, i, &IID_IDXCoreAdapter, (void **)&a)))
            break;
        DXCoreHardwareID hw;
        memset(&hw, 0, sizeof(hw));
        if (SUCCEEDED(IDXCoreAdapter_GetProperty(a, HardwareID, sizeof(hw), &hw)) &&
            hw.vendorID == d->VendorId && hw.deviceID == d->DeviceId) {
            found = a;                     /* keep the reference we already own */
        } else {
            IDXCoreAdapter_Release(a);
        }
    }
    IDXCoreAdapterList_Release(list);
    return found;
}

/* ------------------------------------------------------------------ */
/* Wrappers                                                             */

#define MAX_CACHED 8

typedef struct AdapterWrap {
    const IDXGIAdapter1VtblReal *lpVtbl;
    IDXGIAdapter1Real         *real;       /* DXVK's adapter */
    LONG                        ref;
    DXGI_ADAPTER_DESC1          desc;
} AdapterWrap;

typedef struct FactoryWrap {
    const IDXGIFactory2VtblReal *lpVtbl;
    IDXGIFactory2Real           *real;       /* DXVK's factory */
    LONG                         ref;
    AdapterWrap                 *cached[MAX_CACHED];
} FactoryWrap;

/* ---- IDXGIAdapter1 ------------------------------------------------ */

static HRESULT STDMETHODCALLTYPE Adapter_QueryInterface(void *self, REFIID riid, void **out)
{
    AdapterWrap *w = (AdapterWrap *)self;
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDXGIAdapter) ||
        IsEqualIID(riid, &IID_IDXGIAdapter1)) {
        *out = w;
        ADAPTER_VTBL(w)->AddRef(w);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_IDXCoreAdapter) || IsEqualIID(riid, &IID_IDXCoreAdapter1)) {
        IDXCoreAdapter *a = dxcore_adapter_for(&w->desc);
        if (!a) return E_NOINTERFACE;
        *out = a;
        return S_OK;                        /* the reference transfers to the caller */
    }
    return ADAPTER_VTBL(w->real)->QueryInterface(w->real, riid, out);
}

static ULONG STDMETHODCALLTYPE Adapter_AddRef(void *self)
{
    AdapterWrap *w = (AdapterWrap *)self;
    return (ULONG)InterlockedIncrement(&w->ref);
}

static ULONG STDMETHODCALLTYPE Adapter_Release(void *self)
{
    AdapterWrap *w = (AdapterWrap *)self;
    LONG n = InterlockedDecrement(&w->ref);
    if (n == 0) {
        ADAPTER_VTBL(w->real)->Release(w->real);
        HeapFree(GetProcessHeap(), 0, w);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE Adapter_SetPrivateData(void *s, REFGUID g, UINT d, const void *b)
{ AdapterWrap *w = s; return ADAPTER_VTBL(w->real)->SetPrivateData(w->real, g, d, b); }
static HRESULT STDMETHODCALLTYPE Adapter_SetPrivateDataInterface(void *s, REFGUID g, const IUnknown *o)
{ AdapterWrap *w = s; return ADAPTER_VTBL(w->real)->SetPrivateDataInterface(w->real, g, o); }
static HRESULT STDMETHODCALLTYPE Adapter_GetPrivateData(void *s, REFGUID g, UINT *d, void *b)
{ AdapterWrap *w = s; return ADAPTER_VTBL(w->real)->GetPrivateData(w->real, g, d, b); }
static HRESULT STDMETHODCALLTYPE Adapter_SetParent(void *s, REFIID i, void *p)
{ AdapterWrap *w = s; return ADAPTER_VTBL(w->real)->SetParent(w->real, i, p); }
static HRESULT STDMETHODCALLTYPE Adapter_GetDevice(void *s, REFIID i, void **o)
{ AdapterWrap *w = s; return ADAPTER_VTBL(w->real)->GetDevice(w->real, i, o); }
static HRESULT STDMETHODCALLTYPE Adapter_EnumOutputs(void *s, UINT i, void **o)
{ AdapterWrap *w = s; return ADAPTER_VTBL(w->real)->EnumOutputs(w->real, i, o); }
static HRESULT STDMETHODCALLTYPE Adapter_GetDesc(void *s, DXGI_ADAPTER_DESC *d)
{ AdapterWrap *w = s; return ADAPTER_VTBL(w->real)->GetDesc(w->real, d); }
static HRESULT STDMETHODCALLTYPE Adapter_CheckInterfaceSupport(void *s, REFGUID g, LARGE_INTEGER *v)
{ AdapterWrap *w = s; return ADAPTER_VTBL(w->real)->CheckInterfaceSupport(w->real, g, v); }
static HRESULT STDMETHODCALLTYPE Adapter_GetDesc1(void *s, DXGI_ADAPTER_DESC1 *d)
{
    AdapterWrap *w = (AdapterWrap *)s;
    if (d) *d = w->desc;    /* cached at wrap time: the wrapper outlives nothing else */
    return S_OK;
}

static const IDXGIAdapter1VtblReal adapter1_vtbl = {
    Adapter_QueryInterface, Adapter_AddRef, Adapter_Release,
    Adapter_SetPrivateData, Adapter_SetPrivateDataInterface, Adapter_GetPrivateData,
    Adapter_SetParent, Adapter_GetDevice,
    Adapter_EnumOutputs, Adapter_GetDesc, Adapter_CheckInterfaceSupport,
    Adapter_GetDesc1,
};

static AdapterWrap *adapter_wrap_new(IDXGIAdapter1Real *real)
{
    AdapterWrap *w = HeapAlloc(GetProcessHeap(), 0, sizeof(*w));
    if (!w) return NULL;
    memset(&w->desc, 0, sizeof(w->desc));
    ADAPTER_VTBL(real)->GetDesc1(real, &w->desc);
    w->lpVtbl = &adapter1_vtbl;
    w->real = real;
    w->ref = 1;
    return w;
}

/* ---- IDXGIFactory2 (also serves IDXGIFactory and IDXGIFactory1) --- */

static HRESULT STDMETHODCALLTYPE Factory_QueryInterface(void *self, REFIID riid, void **out)
{
    FactoryWrap *w = (FactoryWrap *)self;
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDXGIFactory) ||
        IsEqualIID(riid, &IID_IDXGIFactory1) || IsEqualIID(riid, &IID_IDXGIFactory2)) {
        *out = w;
        FACTORY_VTBL(w)->AddRef(w);
        return S_OK;
    }
    return FACTORY_VTBL(w->real)->QueryInterface(w->real, riid, out);
}

static ULONG STDMETHODCALLTYPE Factory_AddRef(void *self)
{
    FactoryWrap *w = (FactoryWrap *)self;
    return (ULONG)InterlockedIncrement(&w->ref);
}

static ULONG STDMETHODCALLTYPE Factory_Release(void *self)
{
    FactoryWrap *w = (FactoryWrap *)self;
    LONG n = InterlockedDecrement(&w->ref);
    if (n == 0) {
        for (int i = 0; i < MAX_CACHED; i++)
            if (w->cached[i]) { ADAPTER_VTBL(w->cached[i])->Release(w->cached[i]); w->cached[i] = NULL; }
        FACTORY_VTBL(w->real)->Release(w->real);
        HeapFree(GetProcessHeap(), 0, w);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE Factory_SetPrivateData(void *s, REFGUID g, UINT d, const void *b)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->SetPrivateData(w->real, g, d, b); }
static HRESULT STDMETHODCALLTYPE Factory_SetPrivateDataInterface(void *s, REFGUID g, const IUnknown *o)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->SetPrivateDataInterface(w->real, g, o); }
static HRESULT STDMETHODCALLTYPE Factory_GetPrivateData(void *s, REFGUID g, UINT *d, void *b)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->GetPrivateData(w->real, g, d, b); }

static HRESULT STDMETHODCALLTYPE Factory_EnumAdapters(void *s, UINT i, void **o)
{
    FactoryWrap *w = (FactoryWrap *)s;
    IDXGIAdapter1Real *real = NULL;
    HRESULT hr = FACTORY_VTBL(w->real)->EnumAdapters1(w->real, i, &IID_IDXGIAdapter1, (void **)&real);
    if (FAILED(hr)) return hr;
    AdapterWrap *aw = adapter_wrap_new(real);
    ADAPTER_VTBL(real)->Release(real);
    if (!aw) return E_OUTOFMEMORY;
    *o = aw;
    return S_OK;                       /* the caller owns the wrapper's first reference */
}

static HRESULT STDMETHODCALLTYPE Factory_EnumAdapters1(void *s, UINT i, REFIID riid, void **o)
{
    FactoryWrap *w = (FactoryWrap *)s;
    /* One stable wrapper per index: an app that asks for the same adapter twice must get
     * the same pointer, or IUnknown identity breaks. */
    if (i < MAX_CACHED && w->cached[i]) {
        *o = w->cached[i];
        ADAPTER_VTBL(w->cached[i])->AddRef(w->cached[i]);
        return S_OK;
    }
    IDXGIAdapter1Real *real = NULL;
    HRESULT hr = FACTORY_VTBL(w->real)->EnumAdapters1(w->real, i, &IID_IDXGIAdapter1, (void **)&real);
    if (FAILED(hr)) return hr;
    AdapterWrap *aw = adapter_wrap_new(real);      /* refcount 1 */
    ADAPTER_VTBL(real)->Release(real);
    if (!aw) return E_OUTOFMEMORY;
    if (i < MAX_CACHED) {
        w->cached[i] = aw;                         /* the cache keeps that one reference */
        ADAPTER_VTBL(aw)->AddRef(aw);              /* …and the caller takes another */
    }
    *o = aw;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Factory_MakeWindowAssociation(void *s, HWND h, UINT f)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->MakeWindowAssociation(w->real, h, f); }
static HRESULT STDMETHODCALLTYPE Factory_GetWindowAssociation(void *s, HWND *h)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->GetWindowAssociation(w->real, h); }
static HRESULT STDMETHODCALLTYPE Factory_CreateSwapChain(void *s, void *dev, void *desc, void **chain)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->CreateSwapChain(w->real, dev, desc, chain); }
static HRESULT STDMETHODCALLTYPE Factory_CreateSoftwareAdapter(void *s, HMODULE m, void **o)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->CreateSoftwareAdapter(w->real, m, o); }
static HRESULT STDMETHODCALLTYPE Factory_IsCurrent(void *s)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->IsCurrent(w->real); }
static HRESULT STDMETHODCALLTYPE Factory_SetWindowResizeBehavior(void *s, HWND h, UINT b)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->SetWindowResizeBehavior(w->real, h, b); }
static HRESULT STDMETHODCALLTYPE Factory_GetWindowResizeBehavior(void *s, HWND h, UINT *b)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->GetWindowResizeBehavior(w->real, h, b); }
static BOOL STDMETHODCALLTYPE Factory_IsWindowedStereoEnabled(void *s)
{ FactoryWrap *w = s; return FACTORY_VTBL(w->real)->IsWindowedStereoEnabled(w->real); }
static HRESULT STDMETHODCALLTYPE Factory_CreateSwapChainForHwnd(void *s, void *dev, HWND hwnd,
        const void *desc, const void *fs, void **chain)
{
    FactoryWrap *w = s;
    return FACTORY_VTBL(w->real)->CreateSwapChainForHwnd(w->real, dev, hwnd, desc, fs, chain);
}

static const IDXGIFactory2VtblReal factory2_vtbl = {
    Factory_QueryInterface, Factory_AddRef, Factory_Release,
    Factory_SetPrivateData, Factory_SetPrivateDataInterface, Factory_GetPrivateData,
    Factory_EnumAdapters, Factory_MakeWindowAssociation, Factory_GetWindowAssociation,
    Factory_CreateSwapChain, Factory_CreateSoftwareAdapter,
    Factory_EnumAdapters1, Factory_IsCurrent,
    Factory_SetWindowResizeBehavior, Factory_GetWindowResizeBehavior,
    Factory_IsWindowedStereoEnabled, Factory_CreateSwapChainForHwnd,
};

/* ------------------------------------------------------------------ */
/* The five exports DXVK's dxgi.dll has                                 */

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}

static HRESULT create_factory(int which, REFIID riid, void **out)
{
    if (!ensure_real()) return E_NOINTERFACE;

    /* Re-entered by DXVK itself: hand back its own object, unwrapped. */
    if (inside_dxvk) {
        HRESULT hr = 0;
        IDXGIFactory2Real *real = factory_real_via(which, riid, &hr);
        if (FAILED(hr)) return hr;
        *out = real;
        return S_OK;
    }

    HRESULT hr = 0;
    IDXGIFactory2Real *real = factory_real_via(which == 1 ? 1 : 2, &IID_IDXGIFactory2, &hr);
    if (FAILED(hr)) return hr;

    FactoryWrap *w = HeapAlloc(GetProcessHeap(), 0, sizeof(*w));
    if (!w) { FACTORY_VTBL(real)->Release(real); return E_OUTOFMEMORY; }
    w->lpVtbl = &factory2_vtbl;
    w->real = real;
    w->ref = 1;
    memset(w->cached, 0, sizeof(w->cached));
    hr = FACTORY_VTBL(w)->QueryInterface(w, riid, out);   /* caller gets its reference */
    w->ref--;                                               /* drop ours */
    if (w->ref == 0) {
        FACTORY_VTBL(real)->Release(real);
        HeapFree(GetProcessHeap(), 0, w);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE CreateDXGIFactory(REFIID riid, void **out)  { return create_factory(0, riid, out); }
HRESULT STDMETHODCALLTYPE CreateDXGIFactory1(REFIID riid, void **out) { return create_factory(1, riid, out); }
HRESULT STDMETHODCALLTYPE CreateDXGIFactory2(REFIID riid, void **out) { return create_factory(2, riid, out); }

HRESULT STDMETHODCALLTYPE DXGIDeclareAdapterRemovalSupport(void)
{
    if (!ensure_real() || !g.DeclareAdapterRemovalSupport) return E_NOINTERFACE;
    inside_dxvk++;
    HRESULT hr = g.DeclareAdapterRemovalSupport();
    inside_dxvk--;
    return hr;
}

HRESULT STDMETHODCALLTYPE DXGIGetDebugInterface1(REFIID riid, void **out)
{
    if (!ensure_real() || !g.GetDebugInterface1) return E_NOINTERFACE;
    inside_dxvk++;
    HRESULT hr = g.GetDebugInterface1(riid, out);
    inside_dxvk--;
    return hr;
}
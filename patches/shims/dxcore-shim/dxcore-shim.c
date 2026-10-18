/*
 * dxcore-shim.c -- native dxcore.dll for the suite Wine prefix.
 *
 * Wine 11.19's builtin dxcore.dll stubs IDXCoreAdapterFactory::CreateAdapterList
 * (dlls/dxcore/dxcore.c, fixme:dxcore:dxcore_adapter_factory_CreateAdapterList),
 * so the suite's apps enumerate zero GPU devices through DXCore and refuse to start
 * (the raster image editor's GPU probe helper: "# devices: 0"; the puppet app hangs on
 * !m_devices.empty()).  DXGI itself works fine in this prefix (DXVK 3.1.1
 * exposes the Radeon), so this shim re-exports DXCore on top of DXGI:
 *
 *   DXCoreCreateAdapterFactory  (what Wine's builtin exports; what the apps
 *                                demonstrably reaches -- the fixme above)
 *   dxcore_create_adapter_factory  (Wine's internal name; alias, harmless)
 *   D3D12GetInterface             (the documented D3D12->DXCore entry; handles
 *                                CLSID_DXCoreAdapterFactory.  Wine's builtin
 *                                d3d12core.dll stubs this export outright)
 *
 * Interfaces, vtable order and enum values come from Wine's own headers
 * (/usr/include/wine/windows/dxcore_interface.h), included verbatim.  The
 * two interfaces Wine's header does not define (IDXCoreAdapterFactory1,
 * IDXCoreAdapter1, per Microsoft's published DXCore headers) are declared
 * here as layout-compatible extensions of the Wine vtable structs: every
 * base slot keeps Wine's exact signature, the new method is appended after
 * the base vtable, so QI for the extended IID can return the same object.
 *
 * Adapter data path:
 *   CreateDXGIFactory1 -> IDXGIFactory1::EnumAdapters1, one IDXCoreAdapter
 *   per DXGI adapter, holding a real reference on the IDXGIAdapter1.
 *   All properties are answered from DXGI_ADAPTER_DESC1 (+ IDXGIAdapter2
 *   preemption granularity when available).
 *
 * Threading: every object is refcounted with Interlocked*; the factory is a
 * process-wide singleton, matching Wine's own builtin (and the documented
 * DXCore behaviour that repeated factory creation returns the same object).
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <initguid.h>           /* turn every DEFINE_GUID below into a definition */
#include <dxgi.h>
#include <dxgi1_2.h>

#include <stdint.h>
#include <string.h>

/* Wine's DXCore headers, included (not copied): vtable order, enums, base IIDs. */
#include </usr/include/wine/windows/dxcore_interface.h>

/* ------------------------------------------------------------------ */
/* GUIDs Wine's header lacks (present in Microsoft's published DXCore   */
/* headers; see MicrosoftDocs/win32 desktop-src/dxcore/dxcore_interface */

/* CLSID of the DXCore adapter factory, as passed to D3D12GetInterface.
 * D3D12GetInterface below additionally accepts any request whose *riid*
 * is a factory IID, so a wrong CLSID constant cannot blank the adapter
 * list; only a genuinely unknown riid is refused. */
DEFINE_GUID(CLSID_DXCoreAdapterFactory,
    0x52868710, 0x5a9a, 0x473d, 0x8c, 0x9b, 0x55, 0xb5, 0xbf, 0x36, 0xd3, 0xb3);

/* IDXCoreAdapterFactory1 = IDXCoreAdapterFactory + CreateAdapterListByWorkload */
DEFINE_GUID(IID_IDXCoreAdapterFactory1,
    0xd5682e19, 0x6d21, 0x401c, 0x82, 0x7a, 0x9a, 0x51, 0xa4, 0xea, 0x35, 0xd7);

/* IDXCoreAdapter1 = IDXCoreAdapter + GetPropertyWithInput */
DEFINE_GUID(IID_IDXCoreAdapter1,
    0xa0783366, 0xcfa3, 0x43be, 0x9d, 0x79, 0x55, 0xb2, 0xda, 0x97, 0xc6, 0x3c);

/* ------------------------------------------------------------------ */
/* DXCoreWorkload (Microsoft header; Wine's header predates it).        */
/* Values: Graphics = 0, Compute = 1, Media = 2, MachineLearning = 3.   */
/* CreateAdapterListByWorkload ignores the filters and returns the      */
/* same list as CreateAdapterList (documented in DXCORE-SHIM.md).       */

/* IDXCoreAdapterFactory1 vtable: Wine's complete base vtable, one method
 * appended.  Embedding struct IDXCoreAdapterFactoryVtbl keeps every base
 * slot's signature sourced from Wine's header. */
struct dxcore_factory1_vtbl
{
    struct IDXCoreAdapterFactoryVtbl base;
    HRESULT (STDMETHODCALLTYPE *CreateAdapterListByWorkload)(IDXCoreAdapterFactory *iface,
            uint32_t workload, uint32_t runtime_filter, uint32_t hardware_type_filter,
            REFIID riid, void **out);
};

/* IDXCoreAdapter1 vtable: same pattern, GetPropertyWithInput appended. */
struct dxcore_adapter1_vtbl
{
    struct IDXCoreAdapterVtbl base;
    HRESULT (STDMETHODCALLTYPE *GetPropertyWithInput)(IDXCoreAdapter *iface,
            DXCoreAdapterProperty property, size_t input_size, const void *input,
            size_t output_size, void *output);
};

static ULONG STDMETHODCALLTYPE adapter_AddRef(IDXCoreAdapter *iface);
static HRESULT STDMETHODCALLTYPE factory_QueryInterface(IDXCoreAdapterFactory *iface,
        REFIID riid, void **out);
static ULONG STDMETHODCALLTYPE factory_AddRef(IDXCoreAdapterFactory *iface);

/* ------------------------------------------------------------------ */

struct dxcore_factory;

struct dxcore_adapter
{
    IDXCoreAdapter iface;               /* must be first */
    LONG ref;
    IDXGIAdapter1 *dxgi;                /* held reference */
    struct dxcore_factory *factory;     /* held reference, for GetFactory */
    DXGI_ADAPTER_DESC1 desc;            /* cached at creation */
    DXGI_ADAPTER_DESC2 desc2;           /* cached if IDXGIAdapter2 available */
    BOOL have_desc2;
};

struct dxcore_adapter_list
{
    IDXCoreAdapterList iface;           /* must be first */
    LONG ref;
    struct dxcore_factory *factory;     /* held reference */
    uint32_t count;
    struct dxcore_adapter **adapters;   /* owned references */
};

struct dxcore_factory
{
    IDXCoreAdapterFactory iface;        /* really a dxcore_factory1_vtbl; first
                                           slots are layout-identical */
    LONG ref;
};

/* ================================================================== */
/* IDXCoreAdapter                                                      */
/* ================================================================== */

#define ADAPTER(x)  ((struct dxcore_adapter *)(x))

static HRESULT STDMETHODCALLTYPE adapter_QueryInterface(IDXCoreAdapter *iface,
        REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown)
            || IsEqualGUID(riid, &IID_IDXCoreAdapter)
            || IsEqualGUID(riid, &IID_IDXCoreAdapter1))
    {
        *out = iface;
        adapter_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE adapter_AddRef(IDXCoreAdapter *iface)
{
    struct dxcore_adapter *adapter = ADAPTER(iface);
    return (ULONG)InterlockedIncrement(&adapter->ref);
}

static ULONG STDMETHODCALLTYPE adapter_Release(IDXCoreAdapter *iface)
{
    struct dxcore_adapter *adapter = ADAPTER(iface);
    LONG ref = InterlockedDecrement(&adapter->ref);

    if (!ref)
    {
        if (adapter->dxgi)
            IDXGIAdapter1_Release(adapter->dxgi);
        if (adapter->factory)
            IDXCoreAdapterFactory_Release(&adapter->factory->iface);
        HeapFree(GetProcessHeap(), 0, adapter);
    }
    return (ULONG)ref;
}

static BOOL STDMETHODCALLTYPE adapter_IsValid(IDXCoreAdapter *iface)
{
    /* A DXCore adapter can become invalid when its device is removed; our
     * adapters mirror a live IDXGIAdapter1 reference, so they stay valid. */
    return TRUE;
}

/* We expose every DXGI adapter as a D3D11/D3D12 graphics + compute
 * adapter.  Both DXGI adapters in the suite prefix (the RADV Radeon and
 * DXVK's second adapter) create D3D11 devices at feature level 11_0 and
 * run compute shaders (verified: probe/other/dxgi-probe/run-D64.txt). */
static BOOL attribute_supported(const GUID *attribute)
{
    return IsEqualGUID(attribute, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS)
        || IsEqualGUID(attribute, &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS)
        || IsEqualGUID(attribute, &DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE);
}

static BOOL STDMETHODCALLTYPE adapter_IsAttributeSupported(IDXCoreAdapter *iface,
        REFGUID attribute)
{
    /* Unknown attributes report FALSE; CreateAdapterList still keeps such
     * adapters (see factory_build_adapter_list) so a filter naming an
     * attribute we do not know cannot empty the apps' device list. */
    return attribute_supported(attribute);
}

static BOOL STDMETHODCALLTYPE adapter_IsPropertySupported(IDXCoreAdapter *iface,
        DXCoreAdapterProperty property)
{
    return property >= InstanceLuid && property <= HardwareIDParts;
}

/* Property payload sizes, kept in one table so GetPropertySize and
 * GetProperty can never disagree. */
static size_t property_size(DXCoreAdapterProperty property)
{
    switch (property)
    {
    case InstanceLuid:                    return sizeof(LUID);
    case DriverVersion:                   return 16;  /* two uint64, see below */
    case DriverDescription:               return 128 * sizeof(WCHAR); /* DXGI len */
    case HardwareID:                      return sizeof(DXCoreHardwareID);
    case KmdModelVersion:                 return sizeof(uint64_t);
    case ComputePreemptionGranularity:
    case GraphicsPreemptionGranularity:   return sizeof(uint32_t);
    case DedicatedAdapterMemory:
    case DedicatedSystemMemory:
    case SharedSystemMemory:              return sizeof(uint64_t);
    case AcgCompatible:
    case IsHardware:
    case IsIntegrated:
    case IsDetachable:                    return sizeof(BOOL);
    case HardwareIDParts:                 return sizeof(DXCoreHardwareIDParts);
    default:                              return 0;
    }
}

static HRESULT STDMETHODCALLTYPE adapter_GetProperty(IDXCoreAdapter *iface,
        DXCoreAdapterProperty property, size_t buffer_size, void *buffer)
{
    struct dxcore_adapter *adapter = ADAPTER(iface);
    const DXGI_ADAPTER_DESC1 *d = &adapter->desc;
    size_t need = property_size(property);

    if (!need)
        return E_INVALIDARG;
    if (!buffer || buffer_size < need)
        return E_INVALIDARG;

    switch (property)
    {
    case InstanceLuid:
        *(LUID *)buffer = d->AdapterLuid;
        return S_OK;
    case DriverVersion:
    {
        /* DXGI exposes no driver version.  Synthesize a stable value from
         * what it does know (DXVK 3.1.1 in this prefix): {high=0,
         * low=0x0003_0001_0001_0000}.  Layout: two uint64.  Not the real
         * UMD version -- flagged in DXCORE-SHIM.md. */
        uint64_t *v = (uint64_t *)buffer;
        v[0] = 0;
        v[1] = 0x0003000100010000ull;
        return S_OK;
    }
    case DriverDescription:
        /* d->Description is a 128-WCHAR DXGI array, already NUL-padded. */
        memcpy(buffer, d->Description, sizeof(d->Description));
        return S_OK;
    case HardwareID:
    {
        DXCoreHardwareID *hw = (DXCoreHardwareID *)buffer;
        hw->vendorID  = d->VendorId;
        hw->deviceID  = d->DeviceId;
        hw->subSysID  = d->SubSysId;
        hw->revision  = d->Revision;
        return S_OK;
    }
    case KmdModelVersion:
        *(uint64_t *)buffer = 0;
        return S_OK;
    case ComputePreemptionGranularity:
        *(uint32_t *)buffer = adapter->have_desc2
            ? (uint32_t)adapter->desc2.ComputePreemptionGranularity : 0;
        return S_OK;
    case GraphicsPreemptionGranularity:
        *(uint32_t *)buffer = adapter->have_desc2
            ? (uint32_t)adapter->desc2.GraphicsPreemptionGranularity : 0;
        return S_OK;
    case DedicatedAdapterMemory:
        *(uint64_t *)buffer = d->DedicatedVideoMemory;
        return S_OK;
    case DedicatedSystemMemory:
        *(uint64_t *)buffer = d->DedicatedSystemMemory;
        return S_OK;
    case SharedSystemMemory:
        *(uint64_t *)buffer = d->SharedSystemMemory;
        return S_OK;
    case AcgCompatible:
        /* DXVK is a user-mode driver; no Acg-compatible kernel driver. */
        *(BOOL *)buffer = FALSE;
        return S_OK;
    case IsHardware:
        *(BOOL *)buffer = !(d->Flags & DXGI_ADAPTER_FLAG_SOFTWARE);
        return S_OK;
    case IsIntegrated:
        /* DXGI has no iGPU flag; heuristic: no dedicated VRAM -> integrated. */
        *(BOOL *)buffer = (d->DedicatedVideoMemory == 0);
        return S_OK;
    case IsDetachable:
        *(BOOL *)buffer = FALSE;
        return S_OK;
    case HardwareIDParts:
    {
        DXCoreHardwareIDParts *p = (DXCoreHardwareIDParts *)buffer;
        p->vendorID     = d->VendorId;
        p->deviceID     = d->DeviceId;
        /* PCI DWORD convention: SubSysId = (subsystem << 16) | subvendor */
        p->subSystemID  = d->SubSysId >> 16;
        p->subVendorID  = d->SubSysId & 0xffff;
        p->revisionID   = d->Revision;
        return S_OK;
    }
    default:
        return E_INVALIDARG;
    }
}

static HRESULT STDMETHODCALLTYPE adapter_GetPropertySize(IDXCoreAdapter *iface,
        DXCoreAdapterProperty property, size_t *buffer_size)
{
    size_t need = property_size(property);

    if (!need || !buffer_size)
        return E_INVALIDARG;
    *buffer_size = need;
    return S_OK;
}

static BOOL STDMETHODCALLTYPE adapter_IsQueryStateSupported(IDXCoreAdapter *iface,
        DXCoreAdapterState state)
{
    return state == IsDriverUpdateInProgress || state == AdapterMemoryBudget;
}

static HRESULT STDMETHODCALLTYPE adapter_QueryState(IDXCoreAdapter *iface,
        DXCoreAdapterState state, size_t state_details_size, const void *state_details,
        size_t buffer_size, void *buffer)
{
    struct dxcore_adapter *adapter = ADAPTER(iface);

    switch (state)
    {
    case IsDriverUpdateInProgress:
        if (!buffer || buffer_size < sizeof(BOOL))
            return E_INVALIDARG;
        *(BOOL *)buffer = FALSE;
        return S_OK;

    case AdapterMemoryBudget:
    {
        DXCoreAdapterMemoryBudget *budget = (DXCoreAdapterMemoryBudget *)buffer;
        uint64_t local, nonlocal;

        if (state_details_size < sizeof(DXCoreAdapterMemoryBudgetNodeSegmentGroup)
                || !state_details || !budget
                || buffer_size < sizeof(DXCoreAdapterMemoryBudget))
            return E_INVALIDARG;

        /* Single-node DXGI adapter: every node index maps to node 0. */
        local    = adapter->desc.DedicatedVideoMemory;
        nonlocal = adapter->desc.SharedSystemMemory;

        budget->budget = local;              /* Local segment */
        if (((const DXCoreAdapterMemoryBudgetNodeSegmentGroup *)state_details)->segmentGroup == NonLocal)
            budget->budget = nonlocal;
        budget->currentUsage         = 0;    /* DXVK does not expose usage */
        budget->availableForReservation = budget->budget;
        budget->currentReservation   = 0;
        return S_OK;
    }

    default:
        return E_INVALIDARG;
    }
}

static BOOL STDMETHODCALLTYPE adapter_IsSetStateSupported(IDXCoreAdapter *iface,
        DXCoreAdapterState state)
{
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE adapter_SetState(IDXCoreAdapter *iface,
        DXCoreAdapterState state, size_t state_details_size, const void *state_details,
        size_t buffer_size, const void *buffer)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE adapter_GetFactory(IDXCoreAdapter *iface,
        REFIID riid, void **out)
{
    struct dxcore_adapter *adapter = ADAPTER(iface);

    if (!out)
        return E_POINTER;
    return IDXCoreAdapterFactory_QueryInterface(&adapter->factory->iface, riid, out);
}

static HRESULT STDMETHODCALLTYPE adapter_GetPropertyWithInput(IDXCoreAdapter *iface,
        DXCoreAdapterProperty property, size_t input_size, const void *input,
        size_t output_size, void *output)
{
    /* Property streams / input-conditioned queries are not implemented.
     * Callers are expected to have checked IsPropertySupported; returning
     * E_NOTIMPL here matches Wine's stub style and does not crash. */
    return E_NOTIMPL;
}

static const struct dxcore_adapter1_vtbl adapter_vtbl =
{
    {
        adapter_QueryInterface,
        adapter_AddRef,
        adapter_Release,
        adapter_IsValid,
        adapter_IsAttributeSupported,
        adapter_IsPropertySupported,
        adapter_GetProperty,
        adapter_GetPropertySize,
        adapter_IsQueryStateSupported,
        adapter_QueryState,
        adapter_IsSetStateSupported,
        adapter_SetState,
        adapter_GetFactory,
    },
    adapter_GetPropertyWithInput,
};

/* Takes the factory reference on success (adapter holds it). */
static struct dxcore_adapter *adapter_create(IDXGIAdapter1 *dxgi,
        struct dxcore_factory *factory)
{
    struct dxcore_adapter *adapter;
    IDXGIAdapter2 *a2 = NULL;

    adapter = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
    if (!adapter)
        return NULL;

    adapter->iface.lpVtbl = &adapter_vtbl.base;
    adapter->ref     = 1;
    adapter->factory = factory;
    IDXCoreAdapterFactory_AddRef(&factory->iface);

    adapter->dxgi = dxgi;
    IDXGIAdapter1_AddRef(dxgi);

    memset(&adapter->desc, 0, sizeof(adapter->desc));
    IDXGIAdapter1_GetDesc1(dxgi, &adapter->desc);

    if (SUCCEEDED(IDXGIAdapter1_QueryInterface(dxgi, &IID_IDXGIAdapter2, (void **)&a2)) && a2)
    {
        adapter->have_desc2 = TRUE;
        IDXGIAdapter2_GetDesc2(a2, &adapter->desc2);
        IDXGIAdapter2_Release(a2);
    }

    return adapter;
}

/* ================================================================== */
/* IDXCoreAdapterList                                                  */
/* ================================================================== */

#define ADAPTER_LIST(x) ((struct dxcore_adapter_list *)(x))

static HRESULT STDMETHODCALLTYPE list_QueryInterface(IDXCoreAdapterList *iface,
        REFIID riid, void **out)
{
    struct dxcore_adapter_list *list = ADAPTER_LIST(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDXCoreAdapterList))
    {
        *out = iface;
        list->iface.lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE list_AddRef(IDXCoreAdapterList *iface)
{
    struct dxcore_adapter_list *list = ADAPTER_LIST(iface);
    return (ULONG)InterlockedIncrement(&list->ref);
}

static ULONG STDMETHODCALLTYPE list_Release(IDXCoreAdapterList *iface)
{
    struct dxcore_adapter_list *list = ADAPTER_LIST(iface);
    LONG ref = InterlockedDecrement(&list->ref);

    if (!ref)
    {
        uint32_t i;
        for (i = 0; i < list->count; ++i)
            IDXCoreAdapter_Release(&list->adapters[i]->iface);
        HeapFree(GetProcessHeap(), 0, list->adapters);
        if (list->factory)
            IDXCoreAdapterFactory_Release(&list->factory->iface);
        HeapFree(GetProcessHeap(), 0, list);
    }
    return (ULONG)ref;
}

static HRESULT STDMETHODCALLTYPE list_GetAdapter(IDXCoreAdapterList *iface,
        uint32_t index, REFIID riid, void **out)
{
    struct dxcore_adapter_list *list = ADAPTER_LIST(iface);

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (index >= list->count)
        return DXGI_ERROR_NOT_FOUND;   /* matches DXCore enumeration semantics */
    return IDXCoreAdapter_QueryInterface(&list->adapters[index]->iface, riid, out);
}

static uint32_t STDMETHODCALLTYPE list_GetAdapterCount(IDXCoreAdapterList *iface)
{
    return ADAPTER_LIST(iface)->count;
}

static BOOL STDMETHODCALLTYPE list_IsStale(IDXCoreAdapterList *iface)
{
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE list_GetFactory(IDXCoreAdapterList *iface,
        REFIID riid, void **out)
{
    struct dxcore_adapter_list *list = ADAPTER_LIST(iface);

    if (!out)
        return E_POINTER;
    return IDXCoreAdapterFactory_QueryInterface(&list->factory->iface, riid, out);
}

static HRESULT STDMETHODCALLTYPE list_Sort(IDXCoreAdapterList *iface,
        uint32_t num_preferences, const DXCoreAdapterPreference *preferences)
{
    /* We report IsAdapterPreferenceSupported == FALSE for every
     * preference, so well-behaved callers never reach Sort.  Leave the
     * DXGI enumeration order untouched. */
    return E_NOTIMPL;
}

static BOOL STDMETHODCALLTYPE list_IsAdapterPreferenceSupported(IDXCoreAdapterList *iface,
        DXCoreAdapterPreference preference)
{
    return FALSE;
}

static const struct IDXCoreAdapterListVtbl list_vtbl =
{
    list_QueryInterface,
    list_AddRef,
    list_Release,
    list_GetAdapter,
    list_GetAdapterCount,
    list_IsStale,
    list_GetFactory,
    list_Sort,
    list_IsAdapterPreferenceSupported,
};

/* ================================================================== */
/* IDXCoreAdapterFactory (+ IDXCoreAdapterFactory1 slot)               */
/* ================================================================== */

#define FACTORY(x) ((struct dxcore_factory *)(x))

static HRESULT STDMETHODCALLTYPE factory_QueryInterface(IDXCoreAdapterFactory *iface,
        REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown)
            || IsEqualGUID(riid, &IID_IDXCoreAdapterFactory)
            || IsEqualGUID(riid, &IID_IDXCoreAdapterFactory1))
    {
        *out = iface;
        IDXCoreAdapterFactory_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_AddRef(IDXCoreAdapterFactory *iface)
{
    return (ULONG)InterlockedIncrement(&FACTORY(iface)->ref);
}

static ULONG STDMETHODCALLTYPE factory_Release(IDXCoreAdapterFactory *iface)
{
    /* Process-wide singleton (as on Windows and in Wine's builtin): the
     * factory object is never freed, so a stray Release cannot dangle
     * another thread's reference.  Refcount is clamped at 1. */
    struct dxcore_factory *f = FACTORY(iface);
    LONG ref = InterlockedDecrement(&f->ref);

    if (ref < 1)
    {
        InterlockedExchange(&f->ref, 1);
        ref = 1;
    }
    return (ULONG)ref;
}

/* Create the DXGI-backed adapter list.  Shared by CreateAdapterList and
 * CreateAdapterListByWorkload (which ignores its workload/filters). */
static HRESULT factory_build_adapter_list(struct dxcore_factory *factory,
        uint32_t num_attributes, const GUID *filter_attributes,
        REFIID riid, void **out)
{
    typedef HRESULT (WINAPI *pfn_create_factory1)(REFIID, void **);
    pfn_create_factory1 create_factory1;
    HMODULE dxgi;
    IDXGIFactory1 *dxgif = NULL;
    struct dxcore_adapter **adapters = NULL;
    uint32_t count = 0, capacity = 0, i;
    struct dxcore_adapter_list *list;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;

    /* One LoadLibrary reference on dxgi.dll is deliberately kept for the
     * process lifetime: our IDXGIAdapter1 objects (and their vtables) live
     * inside dxgi.dll, so it must never unload while they can be reached. */
    dxgi = LoadLibraryA("dxgi.dll");
    if (!dxgi)
        return E_FAIL;
    create_factory1 = (pfn_create_factory1)(void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    if (!create_factory1)
        return E_FAIL;

    hr = create_factory1(&IID_IDXGIFactory1, (void **)&dxgif);
    if (FAILED(hr) || !dxgif)
        return FAILED(hr) ? hr : E_FAIL;

    /* Enumerate DXGI adapters; wrap each in an IDXCoreAdapter.
     *
     * Attribute filter policy (documented in DXCORE-SHIM.md): every
     * attribute we can interpret (D3D11/D3D12 graphics, D3D12 core
     * compute) is advertised as supported on every adapter via
     * IsAttributeSupported, so a conforming "must support X" filter keeps
     * the adapter; an attribute we cannot interpret never excludes one
     * either.  The filter can therefore never narrow the list below what
     * DXGI enumerates, and the apps cannot be starved to zero devices. */
    (void)num_attributes;
    (void)filter_attributes;

    for (i = 0; ; ++i)
    {
        IDXGIAdapter1 *a = NULL;
        struct dxcore_adapter *adapter;

        hr = IDXGIFactory1_EnumAdapters1(dxgif, i, &a);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr) || !a)
            break;

        adapter = adapter_create(a, factory);
        IDXGIAdapter1_Release(a);        /* adapter holds its own reference */
        if (!adapter)
            continue;

        if (count == capacity)
        {
            uint32_t newcap = capacity ? capacity * 2 : 4;
            /* HeapReAlloc(NULL, ...) is invalid: allocate the first block
             * with HeapAlloc, realloc only afterwards. */
            struct dxcore_adapter **grown = adapters
                ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, adapters,
                              newcap * sizeof(*grown))
                : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            newcap * sizeof(*grown));
            if (!grown)
            {
                IDXCoreAdapter_Release(&adapter->iface);
                break;
            }
            adapters = grown;
            capacity = newcap;
        }
        adapters[count++] = adapter;
    }

    IDXGIFactory1_Release(dxgif);       /* adapter objects keep DXGI state alive */

    list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*list));
    if (!list)
    {
        for (i = 0; i < count; ++i)
            IDXCoreAdapter_Release(&adapters[i]->iface);
        HeapFree(GetProcessHeap(), 0, adapters);
        return E_OUTOFMEMORY;
    }

    list->iface.lpVtbl = &list_vtbl;
    list->ref     = 1;
    list->factory = factory;
    IDXCoreAdapterFactory_AddRef(&factory->iface);
    list->count   = count;
    list->adapters = adapters;           /* ownership moved */

    hr = IDXCoreAdapterList_QueryInterface(&list->iface, riid, out);
    IDXCoreAdapterList_Release(&list->iface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_CreateAdapterList(IDXCoreAdapterFactory *iface,
        uint32_t num_attributes, const GUID *filter_attributes,
        REFIID riid, void **out)
{
    return factory_build_adapter_list(FACTORY(iface), num_attributes,
            filter_attributes, riid, out);
}

static HRESULT STDMETHODCALLTYPE factory_GetAdapterByLuid(IDXCoreAdapterFactory *iface,
        REFLUID adapter_luid, REFIID riid, void **out)
{
    typedef HRESULT (WINAPI *pfn_create_factory1)(REFIID, void **);
    pfn_create_factory1 create_factory1;
    HMODULE dxgi;
    IDXGIFactory1 *dxgif = NULL;
    uint32_t i;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    /* Wine's C-mode REFLUID is `const LUID *`; the ABI is a pointer. */
    if (!adapter_luid)
        return E_INVALIDARG;

    dxgi = LoadLibraryA("dxgi.dll");     /* module ref kept for process lifetime */
    if (!dxgi)
        return E_FAIL;
    create_factory1 = (pfn_create_factory1)(void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    if (!create_factory1)
        return E_FAIL;
    hr = create_factory1(&IID_IDXGIFactory1, (void **)&dxgif);
    if (FAILED(hr) || !dxgif)
        return FAILED(hr) ? hr : E_FAIL;

    hr = DXGI_ERROR_NOT_FOUND;
    for (i = 0; ; ++i)
    {
        IDXGIAdapter1 *a = NULL;
        DXGI_ADAPTER_DESC1 d;

        if (FAILED(IDXGIFactory1_EnumAdapters1(dxgif, i, &a)) || !a)
            break;
        memset(&d, 0, sizeof(d));
        IDXGIAdapter1_GetDesc1(a, &d);
        if (d.AdapterLuid.LowPart == adapter_luid->LowPart
                && d.AdapterLuid.HighPart == adapter_luid->HighPart)
        {
            struct dxcore_adapter *adapter = adapter_create(a, FACTORY(iface));
            IDXGIAdapter1_Release(a);
            if (adapter)
            {
                hr = IDXCoreAdapter_QueryInterface(&adapter->iface, riid, out);
                IDXCoreAdapter_Release(&adapter->iface);
            }
            break;
        }
        IDXGIAdapter1_Release(a);
    }

    IDXGIFactory1_Release(dxgif);
    return hr;
}

static BOOL STDMETHODCALLTYPE factory_IsNotificationTypeSupported(IDXCoreAdapterFactory *iface,
        DXCoreNotificationType type)
{
    /* No event support: every notification type is reported unsupported,
     * so RegisterEventNotification is never called by a conforming caller. */
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE factory_RegisterEventNotification(IDXCoreAdapterFactory *iface,
        IUnknown *dxcore_object, DXCoreNotificationType type,
        PFN_DXCORE_NOTIFICATION_CALLBACK callback, void *callback_context,
        uint32_t *event_cookie)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE factory_UnregisterEventNotification(IDXCoreAdapterFactory *iface,
        uint32_t event_cookie)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE factory_CreateAdapterListByWorkload(IDXCoreAdapterFactory *iface,
        uint32_t workload, uint32_t runtime_filter, uint32_t hardware_type_filter,
        REFIID riid, void **out)
{
    /* Workload/workload filters are ignored: return the full DXGI-backed
     * list, same as CreateAdapterList. */
    return factory_build_adapter_list(FACTORY(iface), 0, NULL, riid, out);
}

static const struct dxcore_factory1_vtbl factory_vtbl =
{
    {
        factory_QueryInterface,
        factory_AddRef,
        factory_Release,
        factory_CreateAdapterList,
        factory_GetAdapterByLuid,
        factory_IsNotificationTypeSupported,
        factory_RegisterEventNotification,
        factory_UnregisterEventNotification,
    },
    factory_CreateAdapterListByWorkload,
};

/* Process-wide singleton factory, bound to its vtable (defined here, after
 * the vtable, so the initializer can take its address).  Refcount starts at
 * 0; first QI AddRefs, and factory_Release clamps at 1 so the singleton is
 * never freed. */
static struct dxcore_factory singleton = { { &factory_vtbl.base }, 0 };

/* ================================================================== */
/* Exports                                                             */
/* ================================================================== */

__declspec(dllexport) HRESULT WINAPI DXCoreCreateAdapterFactory(REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    return IDXCoreAdapterFactory_QueryInterface(&singleton.iface, riid, out);
}

/* Wine's internal C name for the same entry point (dlls/dxcore).  Wine's
 * headers do not declare it, but exporting the alias is free and covers
 * consumers that GetProcAddress it by Wine's name. */
__declspec(dllexport) HRESULT WINAPI dxcore_create_adapter_factory(REFIID riid, void **out)
{
    return DXCoreCreateAdapterFactory(riid, out);
}

/* Documented D3D12 -> DXCore entry point.  Wine's builtin d3d12core.dll
 * stubs this export; the apps may call it on dxcore.dll or d3d12core.dll.
 * Handles CLSID_DXCoreAdapterFactory; because the CLSID constant cannot be
 * cross-checked against Wine's headers, any riid that names a factory
 * interface is honoured regardless of the CLSID passed. */
__declspec(dllexport) HRESULT WINAPI D3D12GetInterface(REFCLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(clsid, &CLSID_DXCoreAdapterFactory)
            || IsEqualGUID(riid, &IID_IDXCoreAdapterFactory)
            || IsEqualGUID(riid, &IID_IDXCoreAdapterFactory1))
        return DXCoreCreateAdapterFactory(riid, out);
    return E_NOINTERFACE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst; (void)reason; (void)reserved;
    return TRUE;
}

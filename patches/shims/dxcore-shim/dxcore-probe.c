/*
 * dxcore-probe.c -- exercise the native dxcore.dll shim (and, as a
 * baseline, Wine's builtin stub) from both 64- and 32-bit processes.
 *
 * Every fact is printed on its own line prefixed "PROBE:".  Exit status is
 * always 0 so a broken shim still produces a full report.
 *
 * What it does:
 *   1. resolves D3D12GetInterface and DXCoreCreateAdapterFactory from
 *      dxcore.dll (LoadLibraryA picks up the DllOverrides winner, so the
 *      same binary measures baseline and shim);
 *   2. D3D12GetInterface(CLSID_DXCoreAdapterFactory, IID_IDXCoreAdapterFactory1)
 *      -> prints the HRESULT;
 *   3. CreateAdapterList(NULL, 0) -> prints the HRESULT and list count;
 *   4. for every adapter: QI for IDXCoreAdapter1, IsValid,
 *      IsAttributeSupported (each DXCORE_ADAPTER_ATTRIBUTE_*), every
 *      property (IsPropertySupported / GetPropertySize / GetProperty with
 *      the decoded value), QueryState(AdapterMemoryBudget), GetFactory,
 *      GetAdapterByLuid round trip -- one PROBE: line per fact.
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <initguid.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include </usr/include/wine/windows/dxcore_interface.h>

/* GUIDs Wine's dxcore_interface.h does not define (see dxcore-shim.c). */
DEFINE_GUID(CLSID_DXCoreAdapterFactory,
    0x52868710, 0x5a9a, 0x473d, 0x8c, 0x9b, 0x55, 0xb5, 0xbf, 0x36, 0xd3, 0xb3);
DEFINE_GUID(IID_IDXCoreAdapterFactory1,
    0xd5682e19, 0x6d21, 0x401c, 0x82, 0x7a, 0x9a, 0x51, 0xa4, 0xea, 0x35, 0xd7);
DEFINE_GUID(IID_IDXCoreAdapter1,
    0xa0783366, 0xcfa3, 0x43be, 0x9d, 0x79, 0x55, 0xb2, 0xda, 0x97, 0xc6, 0x3c);

typedef HRESULT (WINAPI *PFN_D3D12GetInterface)(REFCLSID, REFIID, void **);
typedef HRESULT (WINAPI *PFN_DXCoreCreateAdapterFactory)(REFIID, void **);

#define P(...) do { printf("PROBE: " __VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

static const char *hr_name(HRESULT hr)
{
    switch (hr) {
    case S_OK:            return "S_OK";
    case S_FALSE:         return "S_FALSE";
    case E_FAIL:          return "E_FAIL";
    case E_INVALIDARG:    return "E_INVALIDARG";
    case E_NOINTERFACE:   return "E_NOINTERFACE";
    case E_OUTOFMEMORY:   return "E_OUTOFMEMORY";
    case E_NOTIMPL:       return "E_NOTIMPL";
    case E_POINTER:       return "E_POINTER";
    case DXGI_ERROR_NOT_FOUND: return "DXGI_ERROR_NOT_FOUND";
    default:              return "";
    }
}

static void print_hr(const char *what, HRESULT hr)
{
    const char *n = hr_name(hr);
    if (*n)
        P("%s = 0x%08lX (%s)", what, (unsigned long)hr, n);
    else
        P("%s = 0x%08lX", what, (unsigned long)hr);
}

static void print_property(IDXCoreAdapter *adapter, uint32_t index,
        DXCoreAdapterProperty property)
{
    static const char *names[] = {
        "InstanceLuid", "DriverVersion", "DriverDescription", "HardwareID",
        "KmdModelVersion", "ComputePreemptionGranularity",
        "GraphicsPreemptionGranularity", "DedicatedAdapterMemory",
        "DedicatedSystemMemory", "SharedSystemMemory", "AcgCompatible",
        "IsHardware", "IsIntegrated", "IsDetachable", "HardwareIDParts",
    };
    const char *name = (property < sizeof(names)/sizeof(names[0]))
        ? names[property] : "?";
    unsigned char buf[512];
    size_t size = 0;
    HRESULT hr;
    char what[200];

    if (!IDXCoreAdapter_IsPropertySupported(adapter, property)) {
        P("adapter %u IsPropertySupported(%u %s) = FALSE", index, property, name);
        return;
    }
    P("adapter %u IsPropertySupported(%u %s) = TRUE", index, property, name);

    hr = IDXCoreAdapter_GetPropertySize(adapter, property, &size);
    snprintf(what, sizeof(what), "adapter %u GetPropertySize(%s) hr", index, name);
    print_hr(what, hr);
    if (FAILED(hr)) return;
    P("adapter %u GetPropertySize(%s) = %zu bytes", index, name, size);

    if (size > sizeof(buf)) {
        P("adapter %u GetProperty(%s) skipped: size %zu > probe buffer", index, name, size);
        return;
    }
    memset(buf, 0, sizeof(buf));
    hr = IDXCoreAdapter_GetProperty(adapter, property, size, buf);
    snprintf(what, sizeof(what), "adapter %u GetProperty(%s) hr", index, name);
    print_hr(what, hr);
    if (FAILED(hr)) return;

    switch (property) {
    case InstanceLuid:
        P("adapter %u Property(%s) = LUID 0x%08lX%08lX", index, name,
          ((LUID *)buf)->HighPart, ((LUID *)buf)->LowPart);
        break;
    case DriverVersion:
        P("adapter %u Property(%s) = {0x%016llX, 0x%016llX} (synthesized: DXVK 3.1.1)",
          index, name, (unsigned long long)((uint64_t *)buf)[0],
          (unsigned long long)((uint64_t *)buf)[1]);
        break;
    case DriverDescription: {
        char utf8[256];
        WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)buf, -1, utf8, sizeof(utf8), NULL, NULL);
        P("adapter %u Property(%s) = \"%s\"", index, name, utf8);
        break;
    }
    case HardwareID: {
        DXCoreHardwareID *hw = (DXCoreHardwareID *)buf;
        P("adapter %u Property(%s) = vendor 0x%04X device 0x%04X subsys 0x%08X rev 0x%04X",
          index, name, hw->vendorID, hw->deviceID, hw->subSysID, hw->revision);
        break;
    }
    case KmdModelVersion:
        P("adapter %u Property(%s) = 0x%016llX", index, name,
          (unsigned long long)*(uint64_t *)buf);
        break;
    case ComputePreemptionGranularity:
    case GraphicsPreemptionGranularity:
        P("adapter %u Property(%s) = %lu", index, name, (unsigned long)*(uint32_t *)buf);
        break;
    case DedicatedAdapterMemory:
    case DedicatedSystemMemory:
    case SharedSystemMemory:
        P("adapter %u Property(%s) = %llu bytes (%llu MiB)", index, name,
          (unsigned long long)*(uint64_t *)buf,
          (unsigned long long)*(uint64_t *)buf / (1024ULL * 1024ULL));
        break;
    case AcgCompatible:
    case IsHardware:
    case IsIntegrated:
    case IsDetachable:
        P("adapter %u Property(%s) = %s", index, name, *(BOOL *)buf ? "TRUE" : "FALSE");
        break;
    case HardwareIDParts: {
        DXCoreHardwareIDParts *p = (DXCoreHardwareIDParts *)buf;
        P("adapter %u Property(%s) = vendor 0x%04X device 0x%04X subsystem 0x%04X"
          " subvendor 0x%04X rev 0x%04X",
          index, name, p->vendorID, p->deviceID, p->subSystemID,
          p->subVendorID, p->revisionID);
        break;
    }
    default:
        P("adapter %u Property(%s) = <%zu raw bytes>", index, name, size);
        break;
    }
}

static void probe_adapter(IDXCoreAdapterList *list, uint32_t index)
{
    IDXCoreAdapter *adapter = NULL;
    void *adapter1 = NULL;                 /* IDXCoreAdapter1* (no Wine type) */
    IDXCoreAdapterFactory *factory = NULL;
    HRESULT hr;
    char what[200];
    uint32_t i;

    hr = IDXCoreAdapterList_GetAdapter(list, index, &IID_IDXCoreAdapter, (void **)&adapter);
    snprintf(what, sizeof(what), "list GetAdapter(%u, IID_IDXCoreAdapter) hr", index);
    print_hr(what, hr);
    if (FAILED(hr) || !adapter) return;

    hr = IDXCoreAdapter_QueryInterface(adapter, &IID_IDXCoreAdapter1, &adapter1);
    snprintf(what, sizeof(what), "adapter %u QI(IID_IDXCoreAdapter1) hr", index);
    print_hr(what, hr);
    if (adapter1) IDXCoreAdapter_Release((IDXCoreAdapter *)adapter1);

    P("adapter %u IsValid() = %s", index,
      IDXCoreAdapter_IsValid(adapter) ? "TRUE" : "FALSE");

    P("adapter %u IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS) = %s",
      index, IDXCoreAdapter_IsAttributeSupported(adapter,
        &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS) ? "TRUE" : "FALSE");
    P("adapter %u IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS) = %s",
      index, IDXCoreAdapter_IsAttributeSupported(adapter,
        &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS) ? "TRUE" : "FALSE");
    P("adapter %u IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE) = %s",
      index, IDXCoreAdapter_IsAttributeSupported(adapter,
        &DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE) ? "TRUE" : "FALSE");

    for (i = 0; i <= 14; ++i)
        print_property(adapter, index, (DXCoreAdapterProperty)i);

    /* QueryState: AdapterMemoryBudget for node 0 / Local. */
    P("adapter %u IsQueryStateSupported(AdapterMemoryBudget) = %s", index,
      IDXCoreAdapter_IsQueryStateSupported(adapter, AdapterMemoryBudget) ? "TRUE" : "FALSE");
    P("adapter %u IsQueryStateSupported(IsDriverUpdateInProgress) = %s", index,
      IDXCoreAdapter_IsQueryStateSupported(adapter, IsDriverUpdateInProgress) ? "TRUE" : "FALSE");
    {
        DXCoreAdapterMemoryBudgetNodeSegmentGroup grp = { 0, Local };
        DXCoreAdapterMemoryBudget budget;
        memset(&budget, 0, sizeof(budget));
        hr = IDXCoreAdapter_QueryState(adapter, AdapterMemoryBudget, sizeof(grp), &grp,
                                       sizeof(budget), &budget);
        snprintf(what, sizeof(what), "adapter %u QueryState(AdapterMemoryBudget, Local) hr", index);
        print_hr(what, hr);
        if (SUCCEEDED(hr))
            P("adapter %u Budget = %llu, currentUsage = %llu, available = %llu,"
              " reservation = %llu", index,
              (unsigned long long)budget.budget,
              (unsigned long long)budget.currentUsage,
              (unsigned long long)budget.availableForReservation,
              (unsigned long long)budget.currentReservation);
    }
    P("adapter %u IsSetStateSupported(AdapterMemoryBudget) = %s", index,
      IDXCoreAdapter_IsSetStateSupported(adapter, AdapterMemoryBudget) ? "TRUE" : "FALSE");

    hr = IDXCoreAdapter_GetFactory(adapter, &IID_IDXCoreAdapterFactory, (void **)&factory);
    snprintf(what, sizeof(what), "adapter %u GetFactory(IID_IDXCoreAdapterFactory) hr", index);
    print_hr(what, hr);
    if (factory) IDXCoreAdapterFactory_Release(factory);

    /* GetAdapterByLuid round trip: read InstanceLuid, look it back up. */
    {
        LUID luid;
        IDXCoreAdapter *again = NULL;
        IDXCoreAdapterFactory *fac2 = NULL;

        memset(&luid, 0, sizeof(luid));
        hr = IDXCoreAdapter_GetProperty(adapter, InstanceLuid, sizeof(luid), &luid);
        hr = IDXCoreAdapterList_GetFactory(list, &IID_IDXCoreAdapterFactory, (void **)&fac2);
        if (SUCCEEDED(hr) && fac2) {
            hr = IDXCoreAdapterFactory_GetAdapterByLuid(fac2, &luid,
                    &IID_IDXCoreAdapter, (void **)&again);
            snprintf(what, sizeof(what), "factory GetAdapterByLuid(LUID 0x%08lX%08lX) hr",
                     (unsigned long)luid.HighPart, (unsigned long)luid.LowPart);
            print_hr(what, hr);
            if (SUCCEEDED(hr) && again)
                P("factory GetAdapterByLuid returned adapter (identity round trip %s)",
                  (again == adapter) ? "same object (refcounted)" : "separate object");
            if (again) IDXCoreAdapter_Release(again);
            IDXCoreAdapterFactory_Release(fac2);
        }
    }

    IDXCoreAdapter_Release(adapter);
}

int main(int argc, char **argv)
{
    HMODULE dxcore;
    PFN_D3D12GetInterface get_interface;
    PFN_DXCoreCreateAdapterFactory create_factory;
    IDXCoreAdapterFactory *factory = NULL;
    void *factory1 = NULL;                 /* IDXCoreAdapterFactory1* (no Wine type) */
    IDXCoreAdapterList *list = NULL;
    HRESULT hr;
    uint32_t count, i;

    (void)argc; (void)argv;

    P("================ dxcore-probe ================");
    P("pid = %lu, %s build", (unsigned long)GetCurrentProcessId(),
#ifdef _WIN64
      "64-bit"
#else
      "32-bit"
#endif
      );

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    print_hr("CoInitializeEx(COINIT_MULTITHREADED) hr", hr);

    dxcore = LoadLibraryA("dxcore.dll");
    if (!dxcore) {
        P("VERDICT: LoadLibraryA(\"dxcore.dll\") FAILED, GetLastError=%lu",
          (unsigned long)GetLastError());
        P("================ end (exit 0) ================");
        return 0;
    }
    {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(dxcore, path, sizeof(path));
        path[n < sizeof(path) ? n : sizeof(path) - 1] = '\0';
        P("LoadLibraryA(\"dxcore.dll\") OK -> %s", path);
    }

    get_interface = (PFN_D3D12GetInterface)(void *)GetProcAddress(dxcore, "D3D12GetInterface");
    P("GetProcAddress(dxcore.dll, \"D3D12GetInterface\") -> %s",
      get_interface ? "found" : "NOT EXPORTED (Wine builtin has no such export)");

    create_factory = (PFN_DXCoreCreateAdapterFactory)(void *)GetProcAddress(dxcore,
        "DXCoreCreateAdapterFactory");
    P("GetProcAddress(dxcore.dll, \"DXCoreCreateAdapterFactory\") -> %s",
      create_factory ? "found" : "missing");

    /* 1. D3D12GetInterface(CLSID_DXCoreAdapterFactory, IID_IDXCoreAdapterFactory1) */
    if (get_interface) {
        hr = get_interface(&CLSID_DXCoreAdapterFactory, &IID_IDXCoreAdapterFactory1,
                           &factory1);
        print_hr("D3D12GetInterface(CLSID_DXCoreAdapterFactory, IID_IDXCoreAdapterFactory1) hr", hr);
        if (SUCCEEDED(hr) && factory1) {
            /* IDXCoreAdapterFactory1 extends the base vtable, so the base
             * macro is valid on the extended pointer (same layout prefix). */
            hr = IDXCoreAdapterFactory_QueryInterface((IDXCoreAdapterFactory *)factory1,
                    &IID_IDXCoreAdapterFactory, (void **)&factory);
            print_hr("QI(IID_IDXCoreAdapterFactory) on result hr", hr);
        } else {
            factory1 = NULL;
        }
    } else {
        P("D3D12GetInterface(CLSID_DXCoreAdapterFactory, IID_IDXCoreAdapterFactory1) hr = <skipped: export missing>");
    }

    /* 2. the classic entry point */
    if (!factory && create_factory) {
        hr = create_factory(&IID_IDXCoreAdapterFactory, (void **)&factory);
        print_hr("DXCoreCreateAdapterFactory(IID_IDXCoreAdapterFactory) hr", hr);
    }

    if (!factory) {
        P("VERDICT: no factory from either entry point; cannot enumerate.");
        P("================ end (exit 0) ================");
        return 0;
    }
    if (!factory1)
        IDXCoreAdapterFactory_QueryInterface(factory, &IID_IDXCoreAdapterFactory1, &factory1);
    /* (factory1 keeps only the extended pointer; released below.) */
    P("factory (IDXCoreAdapterFactory) = %p, (IDXCoreAdapterFactory1) = %p",
      (void *)factory, (void *)factory1);

    /* 3. CreateAdapterList(NULL, 0) */
    hr = IDXCoreAdapterFactory_CreateAdapterList(factory, 0, NULL,
            &IID_IDXCoreAdapterList, (void **)&list);
    print_hr("CreateAdapterList(num_attributes=0, filter=NULL) hr", hr);
    if (FAILED(hr) || !list) {
        P("ADAPTER COUNT = 0");
        P("VERDICT: CreateAdapterList failed or returned NULL.");
        P("================ end (exit 0) ================");
        return 0;
    }

    count = IDXCoreAdapterList_GetAdapterCount(list);
    P("ADAPTER COUNT = %u", count);
    P("list IsStale() = %s", IDXCoreAdapterList_IsStale(list) ? "TRUE" : "FALSE");
    P("list IsAdapterPreferenceSupported(Hardware) = %s",
      IDXCoreAdapterList_IsAdapterPreferenceSupported(list, Hardware) ? "TRUE" : "FALSE");

    /* out-of-range GetAdapter behaviour */
    {
        IDXCoreAdapter *nope = NULL;
        hr = IDXCoreAdapterList_GetAdapter(list, count, &IID_IDXCoreAdapter, (void **)&nope);
        print_hr("list GetAdapter(count, ...) [out of range] hr", hr);
        if (nope) { P(" (unexpected: got an adapter past the end)"); IDXCoreAdapter_Release(nope); }
    }

    for (i = 0; i < count; ++i)
        probe_adapter(list, i);

    /* filtered list: request the D3D12 graphics attribute. */
    {
        IDXCoreAdapterList *filtered = NULL;
        GUID attr = DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS;
        hr = IDXCoreAdapterFactory_CreateAdapterList(factory, 1, &attr,
                &IID_IDXCoreAdapterList, (void **)&filtered);
        print_hr("CreateAdapterList(num_attributes=1, D3D12_GRAPHICS) hr", hr);
        if (filtered) {
            P("filtered ADAPTER COUNT = %u (filter must not starve the list)",
              IDXCoreAdapterList_GetAdapterCount(filtered));
            IDXCoreAdapterList_Release(filtered);
        }
    }

    IDXCoreAdapterList_Release(list);
    if (factory1) IDXCoreAdapterFactory_Release((IDXCoreAdapterFactory *)factory1);
    IDXCoreAdapterFactory_Release(factory);

    P("VERDICT: ADAPTER COUNT = %u", count);
    P("================ end (exit 0) ================");
    return 0;
}

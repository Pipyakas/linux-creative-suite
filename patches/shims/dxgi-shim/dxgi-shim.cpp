/*
 * dxgi-shim.cpp — a native DXGI that reports a GPU *with an attached display*.
 *
 * Why: the raster image editor and the puppet app refuse to start here, and their
 * own GPU probe helper prints
 *
 *     Start platform native
 *       # displays:                   0
 *       # devices:                    0
 *
 * DXVK's DXGI enumerates the real adapters (an AMD Radeon and the Intel iGPU) but reports
 * zero outputs for every one of them, because this machine exposes no Vulkan display
 * device: the Radeon has no display connectors and the attached panel belongs to the
 * Intel.  Wine's own Win32 display layer *does* see one monitor (EnumDisplayMonitors -> 1,
 * 1366x768, \\.\DISPLAY1), so only DXGI is missing it.
 *
 * Design: a pass-through DXGI.  CreateDXGIFactory* returns a factory wrapper around the
 * real DXGI's factory (DXVK's, installed as dxgi-dxvk.dll); the wrappers forward every
 * call they do not deliberately answer, so D3D11/D3D12 swapchain creation and every other
 * consumer keep working exactly as before.  Only these are answered locally:
 *
 *   factory : EnumAdapters / EnumAdapters1 / EnumAdapterByLuid / EnumAdapterByGpuPreference
 *             -> the wrapped adapter (so callers cannot get an unwrapped, output-less one)
 *   adapter : EnumOutputs  -> one synthetic, AttachedToDesktop=TRUE output
 *             GetDesc{,1,2,3} -> the real adapter's, unless LCS_SHIM_GPU overrides it
 *   output  : GetDesc / GetDesc1 / GetDisplayModeList{,1} / FindClosestMatchingMode{,1}
 *
 * Slots: none are written by hand.  This translation unit implements mingw-w64's
 * IDXGIFactory7 / IDXGIAdapter4 / IDXGIOutput6 in C++, so the compiler lays the vtables out
 * from the headers, and those headers were verified to match the ABI two independent ways
 * (see RESULTS.md and vtbl-order.py):
 *
 *   - Wine 11.19's include/dxgi*.idl and mingw's dxgi*.h agree method-for-method and
 *     slot-for-slot on all three interfaces;
 *   - calling the *installed* DXVK factory slot by slot while reading DXVK's own log lines
 *     matches those headers for every slot that reports a name (16..24, plus 7/9/10/12/13/
 *     14/25/26/28/30 identified by their return values).
 *
 * The previous version of this file hand-rolled an index table that was off by four
 * (it put IDXGIFactory::EnumAdapters at slot 3 instead of 7), which is why it never
 * enumerated an adapter.  Do not reintroduce a hand-written index table.
 *
 * Build: ./build.sh      Install/remove: ./install.sh [--uninstall]
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
/* Wine's DXCore header, included (not copied) the same way ../dxcore-shim does:
 * it is the only DXCore declaration set on this machine that is known to agree with
 * what the apps expect, and mingw ships none.  Absolute path because it is not a
 * mingw header.
 * initguid.h first: it turns the header's DEFINE_GUIDs into definitions in this module
 * instead of unresolved externals (the same order the dxcore shim uses). */
#include <initguid.h>
#include </usr/include/wine/windows/dxcore_interface.h>

#include <cstdlib>
#include <cstdio>
#include <cwchar>
#include <cstdarg>

/* --------------------------------------------------------- the real DXGI (DXVK) */

namespace {

HMODULE real_module()
{
    static HMODULE mod;
    static bool tried;
    if (!tried) {
        tried = true;
        /* Only ever dxgi-dxvk.dll: loading "dxgi.dll" here would load this shim again.
         * LOAD_LIBRARY_SEARCH_SYSTEM32 keeps an application-directory dxgi-dxvk.dll out. */
        mod = LoadLibraryExW( L"dxgi-dxvk.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32 );
        if (!mod)
            mod = LoadLibraryW( L"\\??\\C:\\windows\\system32\\dxgi-dxvk.dll" );
    }
    return mod;
}

/* ------------------------------------------------------------------- config */

struct Config
{
    bool  override_identity;   /* true only when LCS_SHIM_GPU was set */
    WCHAR gpu[128];
    UINT  vendor, device, refresh, vram_mb;

    Config() : override_identity(false), vendor(0x10de), device(0x2503),
               refresh(60), vram_mb(12288)
    {
        gpu[0] = 0;
        const wchar_t *v = _wgetenv( L"LCS_SHIM_GPU" );
        if (v && *v) {
            override_identity = true;
            wcsncpy( gpu, v, 127 );
            gpu[127] = 0;
        }
        v = _wgetenv( L"LCS_SHIM_VENDOR" ); if (v && *v) vendor = (UINT)wcstoul( v, NULL, 0 );
        v = _wgetenv( L"LCS_SHIM_DEVICE" ); if (v && *v) device = (UINT)wcstoul( v, NULL, 0 );
        v = _wgetenv( L"LCS_SHIM_REFRESH" ); if (v && *v) refresh = (UINT)wcstoul( v, NULL, 0 );
        v = _wgetenv( L"LCS_SHIM_VRAM_MB" ); if (v && *v) vram_mb = (UINT)wcstoul( v, NULL, 0 );
    }
};

const Config &cfg()
{
    static const Config c;
    return c;
}

/* ------------------------------------------------------------------- tracing */
/* Set LCS_SHIM_TRACE=1 and every entry point the shim answers logs a line to
 * %TEMP%\dxgi-shim-trace.log.  This exists because "the suite still says # displays: 0" is
 * not a diagnosis: the useful question is whether the apps ever call EnumOutputs at all,
 * and only the caller can answer it. */
void trace( const char *fmt, ... )
{
    static int enabled = -1;
    static FILE *log;
    if (enabled < 0) {
        wchar_t path[MAX_PATH];
        path[0] = 0;
        const wchar_t *explicit_path = _wgetenv( L"LCS_SHIM_TRACE_FILE" );
        const wchar_t *v = _wgetenv( L"LCS_SHIM_TRACE" );
        enabled = (explicit_path && *explicit_path) ||
                  (v && *v && *v != L'0');
        if (enabled) {
            if (explicit_path && *explicit_path) {
                wcsncpy( path, explicit_path, MAX_PATH - 1 );
                path[MAX_PATH - 1] = 0;
            } else {
                /* a fixed, always-writable place: %TEMP% varies and GetTempPathW can
                 * hand back a short 8.3 path that is awkward to find from the host */
                wcscpy( path, L"C:\\dxgi-shim-trace.log" );
            }
            log = _wfopen( path, L"a" );
        }
    }
    if (!enabled || !log) return;
    SYSTEMTIME st;
    GetLocalTime( &st );
    fprintf( log, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds );
    va_list ap;
    va_start( ap, fmt );
    vfprintf( log, fmt, ap );
    va_end( ap );
    fputc( '\n', log );
    fflush( log );
}

const char *guid_name( REFIID riid )
{
    static const struct { REFIID iid; const char *name; } kNames[] = {
        { __uuidof(IDXGIObject),         "IDXGIObject" },
        { __uuidof(IDXGIFactory),        "IDXGIFactory" },
        { __uuidof(IDXGIFactory1),       "IDXGIFactory1" },
        { __uuidof(IDXGIFactory2),       "IDXGIFactory2" },
        { __uuidof(IDXGIFactory3),       "IDXGIFactory3" },
        { __uuidof(IDXGIFactory4),       "IDXGIFactory4" },
        { __uuidof(IDXGIFactory5),       "IDXGIFactory5" },
        { __uuidof(IDXGIFactory6),       "IDXGIFactory6" },
        { __uuidof(IDXGIFactory7),       "IDXGIFactory7" },
        { __uuidof(IDXGIAdapter),        "IDXGIAdapter" },
        { __uuidof(IDXGIAdapter1),       "IDXGIAdapter1" },
        { __uuidof(IDXGIAdapter2),       "IDXGIAdapter2" },
        { __uuidof(IDXGIAdapter3),       "IDXGIAdapter3" },
        { __uuidof(IDXGIAdapter4),       "IDXGIAdapter4" },
        { __uuidof(IDXGIOutput),         "IDXGIOutput" },
        { __uuidof(IDXGIOutput1),        "IDXGIOutput1" },
        { __uuidof(IDXGIOutput6),        "IDXGIOutput6" },
        { __uuidof(IUnknown),            "IUnknown" },
    };
    for (const auto &e : kNames)
        if (IsEqualIID( riid, e.iid )) return e.name;
    static char buf[64];
    sprintf( buf, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             (unsigned long)riid.Data1, riid.Data2, riid.Data3,
             riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3],
             riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7] );
    return buf;
}

/* The one screen this machine actually has, as Wine reports it. */
void screen_rect( RECT *r )
{
    r->left   = 0;
    r->top    = 0;
    r->right  = GetSystemMetrics( SM_CXSCREEN );
    r->bottom = GetSystemMetrics( SM_CYSCREEN );
    if (r->right <= 0)  r->right  = 1366;
    if (r->bottom <= 0) r->bottom = 768;
}

/* ------------------------------------------------------------------- objects */

class ShimOutput;
class ShimAdapter;

/* ------------------------------------------------------------------- DXCore */
/* IDXCoreAdapter1 = IDXCoreAdapter + GetPropertyWithInput.  Wine's header stops at
 * IDXCoreAdapter; the IID and the extra slot come from Microsoft's published header and
 * match probe/other/dxcore-shim's dxcore_adapter1_vtbl. */
const GUID IID_IDXCoreAdapter1_ = { 0xa0783366,0xcfa3,0x43be,
                                    { 0x9d,0x79,0x55,0xb2,0xda,0x97,0xc6,0x3c } };

struct IDXCoreAdapter1 : public IDXCoreAdapter
{
    virtual HRESULT STDMETHODCALLTYPE GetPropertyWithInput( DXCoreAdapterProperty property,
            size_t input_size, const void *input, size_t output_size, void *output ) = 0;
};

/* --------------------------------------------------------- Wine's own DXGI adapter */
/* IWineDXGIAdapter (uuid 17399d75-…, from Wine's include/wine/winedxgi.idl) is
 * IDXGIAdapter4 plus get_adapter_info().  It is not a vendor-specific interface: Wine's own
 * d3d12.dll asks for it to identify the adapter it was handed, and E_NOINTERFACE there is
 * what makes D3D12CreateDevice fail with "Invalid adapter" before vkd3d is ever reached.
 *
 * Declared by hand because the IDL is not shipped as a C header; the layout is
 * wine_dxgi_adapter_info from that IDL (2 GUIDs, 2 DWORDs, LUID = 48 bytes). */
const GUID IID_IWineDXGIAdapter_ = { 0x17399d75,0x964e,0x4c03,
                                     { 0x99,0xf8,0x9d,0x4f,0xd1,0x96,0xdd,0x62 } };

struct wine_dxgi_adapter_info
{
    GUID  driver_uuid;
    GUID  device_uuid;
    DWORD vendor_id;
    DWORD device_id;
    LUID  luid;
};

/* The DXCore view of one of our wrapped adapters.
 *
 * This is not cosmetic.  the GPU probe helper enumerates adapters through DXGI, calls GetDesc1,
 * and then queries each adapter for IID_IDXCoreAdapter -- and only that decides whether the
 * adapter becomes a "device" at all.  Measured with LCS_SHIM_TRACE=1: with the stock DXVK
 * adapter underneath, both queries return E_NOINTERFACE and the GPU probe helper stops there, never
 * calling EnumOutputs even once, then prints "# displays: 0 / # devices: 0".  So a DXGI that
 * only reports outputs cannot move those numbers; the adapter has to answer DXCore too.
 *
 * The property payloads mirror probe/other/dxcore-shim/dxcore-shim.c so the two shims
 * describe the same adapter identically.  Anything the apps do not ask about is still
 * answered with a well-formed value rather than an error. */
class ShimDxCoreAdapter final : public IDXCoreAdapter1
{
public:
    ShimDxCoreAdapter( IDXGIAdapter4 *real, const DXGI_ADAPTER_DESC1 &desc )
        : m_real( real ), m_desc( desc )
    {
        m_real->AddRef();
        m_real->GetDesc2( &m_desc2 );
    }

    HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void **out ) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID( riid, __uuidof(IUnknown) ) ||
            IsEqualIID( riid, IID_IDXCoreAdapter ) ||
            IsEqualIID( riid, IID_IDXCoreAdapter1_ )) {
            *out = static_cast<IDXCoreAdapter1 *>( this );
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement( &m_ref ); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement( &m_ref );
        if (r < 1) InterlockedExchange( &m_ref, 0x40000000 );
        return (ULONG)r;
    }
    BOOL STDMETHODCALLTYPE IsValid() override { return TRUE; }

    BOOL STDMETHODCALLTYPE IsAttributeSupported( REFGUID attribute ) override
    {
        return IsEqualGUID( attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS ) ||
               IsEqualGUID( attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS ) ||
               IsEqualGUID( attribute, DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE );
    }
    BOOL STDMETHODCALLTYPE IsPropertySupported( DXCoreAdapterProperty property ) override
    {
        return property >= DXCoreAdapterProperty::InstanceLuid &&
               property <= DXCoreAdapterProperty::HardwareIDParts;
    }
    HRESULT STDMETHODCALLTYPE GetPropertySize( DXCoreAdapterProperty property,
            size_t *size ) override
    {
        size_t n = property_size( property );
        if (!n || !size) return E_INVALIDARG;
        *size = n;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetProperty( DXCoreAdapterProperty property,
            size_t buffer_size, void *buffer ) override
    {
        size_t need = property_size( property );
        if (!need) return E_INVALIDARG;
        if (!buffer || buffer_size < need) return E_INVALIDARG;
        switch (property) {
            case DXCoreAdapterProperty::InstanceLuid:
                *(LUID *)buffer = m_desc.AdapterLuid; return S_OK;
            case DXCoreAdapterProperty::DriverVersion:
            {
                uint64_t *v = (uint64_t *)buffer;
                v[0] = 0;
                v[1] = 0x0003000100010000ull;   /* stable stand-in, see DXCORE-SHIM.md */
                return S_OK;
            }
            case DXCoreAdapterProperty::DriverDescription:
                memcpy( buffer, m_desc.Description, sizeof(m_desc.Description) );
                return S_OK;
            case DXCoreAdapterProperty::HardwareID:
            {
                DXCoreHardwareID *hw = (DXCoreHardwareID *)buffer;
                hw->vendorID = m_desc.VendorId;
                hw->deviceID = m_desc.DeviceId;
                hw->subSysID = m_desc.SubSysId;
                hw->revision = m_desc.Revision;
                return S_OK;
            }
            case DXCoreAdapterProperty::KmdModelVersion:
                *(uint64_t *)buffer = 0; return S_OK;
            case DXCoreAdapterProperty::ComputePreemptionGranularity:
                *(uint32_t *)buffer = (uint32_t)m_desc2.ComputePreemptionGranularity;
                return S_OK;
            case DXCoreAdapterProperty::GraphicsPreemptionGranularity:
                *(uint32_t *)buffer = (uint32_t)m_desc2.GraphicsPreemptionGranularity;
                return S_OK;
            case DXCoreAdapterProperty::DedicatedAdapterMemory:
                *(uint64_t *)buffer = m_desc.DedicatedVideoMemory; return S_OK;
            case DXCoreAdapterProperty::DedicatedSystemMemory:
                *(uint64_t *)buffer = m_desc.DedicatedSystemMemory; return S_OK;
            case DXCoreAdapterProperty::SharedSystemMemory:
                *(uint64_t *)buffer = m_desc.SharedSystemMemory; return S_OK;
            case DXCoreAdapterProperty::AcgCompatible:
                *(BOOL *)buffer = FALSE; return S_OK;
            case DXCoreAdapterProperty::IsHardware:
                *(BOOL *)buffer = !(m_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE); return S_OK;
            case DXCoreAdapterProperty::IsIntegrated:
                *(BOOL *)buffer = (m_desc.DedicatedVideoMemory == 0); return S_OK;
            case DXCoreAdapterProperty::IsDetachable:
                *(BOOL *)buffer = FALSE; return S_OK;
            case DXCoreAdapterProperty::HardwareIDParts:
            {
                DXCoreHardwareIDParts *p = (DXCoreHardwareIDParts *)buffer;
                p->vendorID    = m_desc.VendorId;
                p->deviceID    = m_desc.DeviceId;
                p->subSystemID = m_desc.SubSysId >> 16;
                p->subVendorID = m_desc.SubSysId & 0xffff;
                p->revisionID  = m_desc.Revision;
                return S_OK;
            }
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyWithInput( DXCoreAdapterProperty property,
            size_t, const void *, size_t output_size, void *output ) override
    {
        return GetProperty( property, output_size, output );
    }
    BOOL STDMETHODCALLTYPE IsQueryStateSupported( DXCoreAdapterState state ) override
    {
        return state == DXCoreAdapterState::IsDriverUpdateInProgress ||
               state == DXCoreAdapterState::AdapterMemoryBudget;
    }
    HRESULT STDMETHODCALLTYPE QueryState( DXCoreAdapterState state, size_t,
            const void *details, size_t buffer_size, void *buffer ) override
    {
        if (state == DXCoreAdapterState::IsDriverUpdateInProgress) {
            if (buffer_size < sizeof(BOOL) || !buffer) return E_INVALIDARG;
            *(BOOL *)buffer = FALSE;
            return S_OK;
        }
        if (state == DXCoreAdapterState::AdapterMemoryBudget) {
            DXCoreAdapterMemoryBudgetNodeSegmentGroup group =
                details ? *(const DXCoreAdapterMemoryBudgetNodeSegmentGroup *)details
                        : DXCoreAdapterMemoryBudgetNodeSegmentGroup{ 0,
                              DXCoreSegmentGroup::Local };
            if (buffer_size < sizeof(DXCoreAdapterMemoryBudget) || !buffer)
                return E_INVALIDARG;
            DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
            HRESULT hr = m_real->QueryVideoMemoryInfo( group.nodeIndex,
                    group.segmentGroup == DXCoreSegmentGroup::Local
                        ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL
                        : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &info );
            if (FAILED( hr )) return hr;
            DXCoreAdapterMemoryBudget *b = (DXCoreAdapterMemoryBudget *)buffer;
            b->budget                  = info.Budget;
            b->currentUsage            = info.CurrentUsage;
            b->availableForReservation = info.AvailableForReservation;
            b->currentReservation      = info.CurrentReservation;
            return S_OK;
        }
        return E_INVALIDARG;
    }
    BOOL STDMETHODCALLTYPE IsSetStateSupported( DXCoreAdapterState state ) override
    {
        return state == DXCoreAdapterState::AdapterMemoryBudget;
    }
    HRESULT STDMETHODCALLTYPE SetState( DXCoreAdapterState state, size_t,
            const void *details, size_t buffer_size, const void *buffer ) override
    {
        if (state != DXCoreAdapterState::AdapterMemoryBudget) return E_INVALIDARG;
        if (buffer_size < sizeof(uint64_t) || !buffer) return E_INVALIDARG;
        DXCoreAdapterMemoryBudgetNodeSegmentGroup group =
            details ? *(const DXCoreAdapterMemoryBudgetNodeSegmentGroup *)details
                    : DXCoreAdapterMemoryBudgetNodeSegmentGroup{ 0, DXCoreSegmentGroup::Local };
        return m_real->SetVideoMemoryReservation( group.nodeIndex,
                group.segmentGroup == DXCoreSegmentGroup::Local
                    ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                *(const uint64_t *)buffer );
    }
    /* Same source of truth as the app would get from dxcore.dll: hand back that factory
     * rather than a second, possibly disagreeing one. */
    HRESULT STDMETHODCALLTYPE GetFactory( REFIID riid, void **out ) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        HMODULE mod = LoadLibraryExW( L"dxcore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32 );
        if (!mod) mod = LoadLibraryW( L"dxcore.dll" );
        if (!mod) return E_NOINTERFACE;
        typedef HRESULT (WINAPI *pfn_create)( REFIID, void ** );
        pfn_create create = (pfn_create)GetProcAddress( mod, "DXCoreCreateAdapterFactory" );
        if (!create) return E_NOINTERFACE;
        return create( riid, out );
    }

private:
    static size_t property_size( DXCoreAdapterProperty property )
    {
        switch (property) {
            case DXCoreAdapterProperty::InstanceLuid:           return sizeof(LUID);
            case DXCoreAdapterProperty::DriverVersion:          return 16;
            case DXCoreAdapterProperty::DriverDescription:      return 128 * sizeof(WCHAR);
            case DXCoreAdapterProperty::HardwareID:             return sizeof(DXCoreHardwareID);
            case DXCoreAdapterProperty::KmdModelVersion:        return sizeof(uint64_t);
            case DXCoreAdapterProperty::ComputePreemptionGranularity:
            case DXCoreAdapterProperty::GraphicsPreemptionGranularity:
                                                                return sizeof(uint32_t);
            case DXCoreAdapterProperty::DedicatedAdapterMemory:
            case DXCoreAdapterProperty::DedicatedSystemMemory:
            case DXCoreAdapterProperty::SharedSystemMemory:      return sizeof(uint64_t);
            case DXCoreAdapterProperty::AcgCompatible:
            case DXCoreAdapterProperty::IsHardware:
            case DXCoreAdapterProperty::IsIntegrated:
            case DXCoreAdapterProperty::IsDetachable:            return sizeof(BOOL);
            case DXCoreAdapterProperty::HardwareIDParts:         return sizeof(DXCoreHardwareIDParts);
        }
        return 0;
    }

    IDXGIAdapter4    *m_real;
    DXGI_ADAPTER_DESC1 m_desc;
    DXGI_ADAPTER_DESC2 m_desc2 = {};
    LONG              m_ref = 1;
};

/* Every object here is process-lifetime: allocated once, never freed.  A wrapper that is
 * destroyed while a caller still holds a forwarded inner pointer is a crash waiting to
 * happen, and these objects are tiny. */
class ShimFactory final : public IDXGIFactory7
{
public:
    explicit ShimFactory( IDXGIFactory7 *real ) : m_real( real ) {}

    HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void **out ) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE SetPrivateData( REFGUID g, UINT n, const void *p ) override
        { return m_real->SetPrivateData( g, n, p ); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface( REFGUID g, const IUnknown *o ) override
        { return m_real->SetPrivateDataInterface( g, o ); }
    HRESULT STDMETHODCALLTYPE GetPrivateData( REFGUID g, UINT *n, void *p ) override
        { return m_real->GetPrivateData( g, n, p ); }
    HRESULT STDMETHODCALLTYPE GetParent( REFIID riid, void **parent ) override
        { return m_real->GetParent( riid, parent ); }
    HRESULT STDMETHODCALLTYPE EnumAdapters( UINT i, IDXGIAdapter **out ) override;
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation( HWND w, UINT f ) override
        { return m_real->MakeWindowAssociation( w, f ); }
    HRESULT STDMETHODCALLTYPE GetWindowAssociation( HWND *w ) override
        { return m_real->GetWindowAssociation( w ); }
    HRESULT STDMETHODCALLTYPE CreateSwapChain( IUnknown *d, DXGI_SWAP_CHAIN_DESC *desc,
                                              IDXGISwapChain **sc ) override
        { return m_real->CreateSwapChain( d, desc, sc ); }
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter( HMODULE m, IDXGIAdapter **out ) override
        { return m_real->CreateSoftwareAdapter( m, out ); }
    HRESULT STDMETHODCALLTYPE EnumAdapters1( UINT i, IDXGIAdapter1 **out ) override;
    BOOL    STDMETHODCALLTYPE IsCurrent() override { return m_real->IsCurrent(); }
    BOOL    STDMETHODCALLTYPE IsWindowedStereoEnabled() override
        { return m_real->IsWindowedStereoEnabled(); }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd( IUnknown *d, HWND w,
            const DXGI_SWAP_CHAIN_DESC1 *desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fd,
            IDXGIOutput *restrict_to, IDXGISwapChain1 **sc ) override
        { return m_real->CreateSwapChainForHwnd( d, w, desc, fd, restrict_to, sc ); }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow( IUnknown *d, IUnknown *w,
            const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *restrict_to,
            IDXGISwapChain1 **sc ) override
        { return m_real->CreateSwapChainForCoreWindow( d, w, desc, restrict_to, sc ); }
    HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid( HANDLE h, LUID *l ) override
        { return m_real->GetSharedResourceAdapterLuid( h, l ); }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow( HWND w, UINT m, DWORD *c ) override
        { return m_real->RegisterStereoStatusWindow( w, m, c ); }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent( HANDLE h, DWORD *c ) override
        { return m_real->RegisterStereoStatusEvent( h, c ); }
    void    STDMETHODCALLTYPE UnregisterStereoStatus( DWORD c ) override
        { m_real->UnregisterStereoStatus( c ); }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow( HWND w, UINT m, DWORD *c ) override
        { return m_real->RegisterOcclusionStatusWindow( w, m, c ); }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent( HANDLE h, DWORD *c ) override
        { return m_real->RegisterOcclusionStatusEvent( h, c ); }
    void    STDMETHODCALLTYPE UnregisterOcclusionStatus( DWORD c ) override
        { m_real->UnregisterOcclusionStatus( c ); }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition( IUnknown *d,
            const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *restrict_to,
            IDXGISwapChain1 **sc ) override
        { return m_real->CreateSwapChainForComposition( d, desc, restrict_to, sc ); }
    UINT    STDMETHODCALLTYPE GetCreationFlags() override { return m_real->GetCreationFlags(); }
    HRESULT STDMETHODCALLTYPE EnumAdapterByLuid( LUID luid, REFIID riid, void **out ) override;
    HRESULT STDMETHODCALLTYPE EnumWarpAdapter( REFIID riid, void **out ) override
        { return m_real->EnumWarpAdapter( riid, out ); }
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport( DXGI_FEATURE f, void *data, UINT n ) override
        { return m_real->CheckFeatureSupport( f, data, n ); }
    HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference( UINT i,
            DXGI_GPU_PREFERENCE pref, REFIID riid, void **out ) override;
    HRESULT STDMETHODCALLTYPE RegisterAdaptersChangedEvent( HANDLE h, DWORD *c ) override
        { return m_real->RegisterAdaptersChangedEvent( h, c ); }
    HRESULT STDMETHODCALLTYPE UnregisterAdaptersChangedEvent( DWORD c ) override
        { return m_real->UnregisterAdaptersChangedEvent( c ); }

    /* Filled in by the constructor; never null. */
    void set_factory_interface( IDXGIFactory7 *f ) { m_real = f; }

private:
    HRESULT adapter_for( UINT index, REFIID riid, void **out );

    IDXGIFactory7 *m_real;
    LONG           m_ref = 1;
    ShimAdapter   *m_adapters[8] = {};
    UINT           m_adapter_count = 0;
};

class ShimAdapter final : public IDXGIAdapter4
{
public:
    explicit ShimAdapter( ShimFactory *owner ) : m_owner( owner ) {}

    HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void **out ) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE SetPrivateData( REFGUID g, UINT n, const void *p ) override
        { return m_real->SetPrivateData( g, n, p ); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface( REFGUID g, const IUnknown *o ) override
        { return m_real->SetPrivateDataInterface( g, o ); }
    HRESULT STDMETHODCALLTYPE GetPrivateData( REFGUID g, UINT *n, void *p ) override
        { return m_real->GetPrivateData( g, n, p ); }
    HRESULT STDMETHODCALLTYPE GetParent( REFIID riid, void **parent ) override;
    HRESULT STDMETHODCALLTYPE EnumOutputs( UINT index, IDXGIOutput **out ) override;
    HRESULT STDMETHODCALLTYPE GetDesc( DXGI_ADAPTER_DESC *d ) override;
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport( REFGUID guid, LARGE_INTEGER *ver ) override
        { return m_real->CheckInterfaceSupport( guid, ver ); }
    HRESULT STDMETHODCALLTYPE GetDesc1( DXGI_ADAPTER_DESC1 *d ) override;
    HRESULT STDMETHODCALLTYPE GetDesc2( DXGI_ADAPTER_DESC2 *d ) override;
    HRESULT STDMETHODCALLTYPE RegisterHardwareContentProtectionTeardownStatusEvent(
            HANDLE h, DWORD *c ) override
        { return m_real->RegisterHardwareContentProtectionTeardownStatusEvent( h, c ); }
    HRESULT STDMETHODCALLTYPE SetVideoMemoryReservation( UINT node,
            DXGI_MEMORY_SEGMENT_GROUP group, UINT64 n ) override
        { return m_real->SetVideoMemoryReservation( node, group, n ); }
    HRESULT STDMETHODCALLTYPE RegisterVideoMemoryBudgetChangeNotificationEvent(
            HANDLE h, DWORD *c ) override
        { return m_real->RegisterVideoMemoryBudgetChangeNotificationEvent( h, c ); }
    void    STDMETHODCALLTYPE UnregisterHardwareContentProtectionTeardownStatus(
            DWORD c ) override
        { m_real->UnregisterHardwareContentProtectionTeardownStatus( c ); }
    void    STDMETHODCALLTYPE UnregisterVideoMemoryBudgetChangeNotification(
            DWORD c ) override
        { m_real->UnregisterVideoMemoryBudgetChangeNotification( c ); }
    HRESULT STDMETHODCALLTYPE QueryVideoMemoryInfo( UINT node,
            DXGI_MEMORY_SEGMENT_GROUP group,
            DXGI_QUERY_VIDEO_MEMORY_INFO *info ) override
        { return m_real->QueryVideoMemoryInfo( node, group, info ); }
    HRESULT STDMETHODCALLTYPE GetDesc3( DXGI_ADAPTER_DESC3 *d ) override;

    void set_real( IDXGIAdapter4 *a ) { m_real = a; }
    ShimOutput *output();
    ShimDxCoreAdapter *dxcore();
    const DXGI_ADAPTER_DESC1 &desc() const { return m_desc; }
    IDXGIAdapter4 *real() const { return m_real; }
    void adapter_desc( DXGI_ADAPTER_DESC1 *d1 ) const;   /* identity used by all GetDesc* */
    /* IWineDXGIAdapter::get_adapter_info -- see the class comment on the IID below.
     * It MUST stay virtual and MUST stay declared after every IDXGIAdapter4 override, or
     * the compiler will not put it in slot 19 where Wine's d3d12 looks for it. */
    virtual HRESULT STDMETHODCALLTYPE get_adapter_info( wine_dxgi_adapter_info *info );

private:
    ShimFactory  *m_owner;
    IDXGIAdapter4 *m_real = nullptr;
    LONG          m_ref = 1;
    ShimOutput   *m_output = nullptr;
    ShimDxCoreAdapter *m_dxcore = nullptr;
    DXGI_ADAPTER_DESC1 m_desc = {};
};

class ShimOutput final : public IDXGIOutput6
{
public:
    explicit ShimOutput( ShimAdapter *owner ) : m_owner( owner ) {}

    HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void **out ) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE SetPrivateData( REFGUID, UINT, const void * ) override
        { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface( REFGUID, const IUnknown * ) override
        { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPrivateData( REFGUID, UINT *, void * ) override
        { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetParent( REFIID riid, void **parent ) override;
    HRESULT STDMETHODCALLTYPE GetDesc( DXGI_OUTPUT_DESC *d ) override;
    HRESULT STDMETHODCALLTYPE GetDisplayModeList( DXGI_FORMAT fmt, UINT flags,
            UINT *count, DXGI_MODE_DESC *modes ) override;
    HRESULT STDMETHODCALLTYPE FindClosestMatchingMode( const DXGI_MODE_DESC *want,
            DXGI_MODE_DESC *got, IUnknown *device ) override;
    HRESULT STDMETHODCALLTYPE WaitForVBlank() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE TakeOwnership( IUnknown *, BOOL ) override
        { return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE; }
    void    STDMETHODCALLTYPE ReleaseOwnership() override {}
    HRESULT STDMETHODCALLTYPE GetGammaControlCapabilities(
            DXGI_GAMMA_CONTROL_CAPABILITIES *c ) override
        { if (c) ZeroMemory( c, sizeof(*c) ); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetGammaControl( const DXGI_GAMMA_CONTROL * ) override
        { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetGammaControl( DXGI_GAMMA_CONTROL *g ) override
        { if (g) ZeroMemory( g, sizeof(*g) ); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetDisplaySurface( IDXGISurface * ) override
        { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDisplaySurfaceData( IDXGISurface * ) override
        { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics( DXGI_FRAME_STATISTICS *s ) override
        { if (s) ZeroMemory( s, sizeof(*s) ); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDisplayModeList1( DXGI_FORMAT fmt, UINT flags,
            UINT *count, DXGI_MODE_DESC1 *modes ) override;
    HRESULT STDMETHODCALLTYPE FindClosestMatchingMode1( const DXGI_MODE_DESC1 *want,
            DXGI_MODE_DESC1 *got, IUnknown *device ) override;
    HRESULT STDMETHODCALLTYPE GetDisplaySurfaceData1( IDXGIResource * ) override
        { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DuplicateOutput( IUnknown *,
            IDXGIOutputDuplication ** ) override
        { return E_NOTIMPL; }
    BOOL    STDMETHODCALLTYPE SupportsOverlays() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE CheckOverlaySupport( DXGI_FORMAT, IUnknown *, UINT *f ) override
        { if (f) *f = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE CheckOverlayColorSpaceSupport( DXGI_FORMAT,
            DXGI_COLOR_SPACE_TYPE, IUnknown *, UINT *f ) override
        { if (f) *f = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE DuplicateOutput1( IUnknown *, UINT, UINT,
            const DXGI_FORMAT *, IDXGIOutputDuplication ** ) override
        { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDesc1( DXGI_OUTPUT_DESC1 *d ) override;
    HRESULT STDMETHODCALLTYPE CheckHardwareCompositionSupport( UINT *f ) override
        { if (f) *f = 0; return S_OK; }

private:
    ShimAdapter *m_owner;
    LONG         m_ref = 1;
};

const LONG kPermanentRef = 0x40000000;   /* process-lifetime objects never free */

/* ---------------------------------------------------------------- factory impl */

HRESULT ShimFactory::QueryInterface( REFIID riid, void **out )
{
    if (!out) return E_POINTER;
    *out = nullptr;
    /* Everything up to IDXGIFactory7 is this object, so a caller can never escape to the
     * real factory and get an unwrapped (output-less) adapter out of it. */
    if (IsEqualIID( riid, __uuidof(IUnknown) ) ||
        IsEqualIID( riid, __uuidof(IDXGIObject) ) ||
        IsEqualIID( riid, __uuidof(IDXGIFactory) ) ||
        IsEqualIID( riid, __uuidof(IDXGIFactory1) ) ||
        IsEqualIID( riid, __uuidof(IDXGIFactory2) ) ||
        IsEqualIID( riid, __uuidof(IDXGIFactory3) ) ||
        IsEqualIID( riid, __uuidof(IDXGIFactory4) ) ||
        IsEqualIID( riid, __uuidof(IDXGIFactory5) ) ||
        IsEqualIID( riid, __uuidof(IDXGIFactory6) ) ||
        IsEqualIID( riid, __uuidof(IDXGIFactory7) )) {
        *out = static_cast<IDXGIFactory7 *>( this );
        AddRef();
        return S_OK;
    }
    /* Anything else (IDXGIVkInteropFactory, IDXGIFactoryMedia, ...) is DXVK-internal and
     * must stay the real object. */
    HRESULT hr = m_real->QueryInterface( riid, out );
    trace( "  factory QI(%s) -> FORWARDED, hr=0x%08lx", guid_name( riid ),
           (unsigned long)hr );
    return hr;
}

ULONG ShimFactory::AddRef() { return (ULONG)InterlockedIncrement( &m_ref ); }
ULONG ShimFactory::Release()
{
    LONG r = InterlockedDecrement( &m_ref );
    if (r < 1) InterlockedExchange( &m_ref, kPermanentRef );
    return (ULONG)r;
}

HRESULT ShimFactory::adapter_for( UINT index, REFIID riid, void **out )
{
    if (!out) return E_POINTER;
    *out = nullptr;
    if (index >= 8) { trace( "  EnumAdapters(%u, %s) -> NOT_FOUND (index out of range)",
                             index, guid_name( riid ) ); return DXGI_ERROR_NOT_FOUND; }

    if (!m_adapters[index]) {
        IDXGIAdapter4 *real = NULL;
        HRESULT hr = m_real->EnumAdapters1( index, (IDXGIAdapter1 **)&real );
        if (FAILED( hr ) || !real) return FAILED( hr ) ? hr : DXGI_ERROR_NOT_FOUND;
        ShimAdapter *a = new ShimAdapter( this );
        a->set_real( real );
        m_adapters[index] = a;
        if (index + 1 > m_adapter_count) m_adapter_count = index + 1;
        trace( "  adapter %u wrapped (real=%p)", index, (void *)real );
    }
    HRESULT hr = m_adapters[index]->QueryInterface( riid, out );
    trace( "  EnumAdapters(%u, %s) -> hr=0x%08lx out=%p", index, guid_name( riid ),
           (unsigned long)hr, out ? *out : NULL );
    return hr;
}

HRESULT ShimFactory::EnumAdapters( UINT index, IDXGIAdapter **out )
{
    return adapter_for( index, __uuidof(IDXGIAdapter), (void **)out );
}

HRESULT ShimFactory::EnumAdapters1( UINT index, IDXGIAdapter1 **out )
{
    return adapter_for( index, __uuidof(IDXGIAdapter1), (void **)out );
}

HRESULT ShimFactory::EnumAdapterByLuid( LUID luid, REFIID riid, void **out )
{
    for (UINT i = 0; i < 8; i++) {
        IDXGIAdapter4 *real = NULL;
        if (FAILED( m_real->EnumAdapters1( i, (IDXGIAdapter1 **)&real ) ) || !real) break;
        DXGI_ADAPTER_DESC1 desc = {};
        real->GetDesc1( &desc );
        if (desc.AdapterLuid.LowPart == luid.LowPart &&
            desc.AdapterLuid.HighPart == luid.HighPart)
            return adapter_for( i, riid, out );
    }
    /* Not an adapter this machine actually has: let the real factory answer. */
    return m_real->EnumAdapterByLuid( luid, riid, out );
}

HRESULT ShimFactory::EnumAdapterByGpuPreference( UINT index,
        DXGI_GPU_PREFERENCE pref, REFIID riid, void **out )
{
    /* the GPU probe helper asks by preference, so this must hand back a wrapped adapter. */
    (void)pref;
    return adapter_for( index, riid, out );
}

/* ---------------------------------------------------------------- adapter impl */

HRESULT ShimAdapter::QueryInterface( REFIID riid, void **out )
{
    if (!out) return E_POINTER;
    *out = nullptr;
    if (IsEqualIID( riid, __uuidof(IUnknown) ) ||
        IsEqualIID( riid, __uuidof(IDXGIObject) ) ||
        IsEqualIID( riid, __uuidof(IDXGIAdapter) ) ||
        IsEqualIID( riid, __uuidof(IDXGIAdapter1) ) ||
        IsEqualIID( riid, __uuidof(IDXGIAdapter2) ) ||
        IsEqualIID( riid, __uuidof(IDXGIAdapter3) ) ||
        IsEqualIID( riid, __uuidof(IDXGIAdapter4) )) {
        *out = static_cast<IDXGIAdapter4 *>( this );
        AddRef();
        trace( "  adapter QI(%s) -> this", guid_name( riid ) );
        return S_OK;
    }
    /* DXCore view: the GPU probe helper asks for this before it will count the adapter as a
     * device at all, and the installed (unpatched) DXVK adapter answers E_NOINTERFACE. */
    if (IsEqualIID( riid, IID_IDXCoreAdapter ) ||
        IsEqualIID( riid, IID_IDXCoreAdapter1_ )) {
        ShimDxCoreAdapter *dc = dxcore();
        trace( "  adapter QI(%s) -> our DXCore view", guid_name( riid ) );
        return dc->QueryInterface( riid, out );
    }
    /* Wine's d3d12 asks for this to identify the adapter; see the IDL note above. */
    if (IsEqualIID( riid, IID_IWineDXGIAdapter_ )) {
        *out = static_cast<IDXGIAdapter4 *>( this );
        AddRef();
        trace( "  adapter QI(IWineDXGIAdapter) -> this" );
        return S_OK;
    }
    /* DXVK's own interfaces (IDXGIDXVKAdapter, IWineDXGIAdapter) belong to the real
     * adapter: forwarding them is what keeps D3D11CreateDevice/D3D12CreateDevice working
     * on this adapter. */
    HRESULT hr = m_real->QueryInterface( riid, out );
    trace( "  adapter QI(%s) -> FORWARDED to real adapter, hr=0x%08lx",
           guid_name( riid ), (unsigned long)hr );
    return hr;
}

ULONG ShimAdapter::AddRef() { return (ULONG)InterlockedIncrement( &m_ref ); }
ULONG ShimAdapter::Release()
{
    LONG r = InterlockedDecrement( &m_ref );
    if (r < 1) InterlockedExchange( &m_ref, kPermanentRef );
    return (ULONG)r;
}

HRESULT ShimAdapter::GetParent( REFIID riid, void **parent )
{
    if (!parent) return E_POINTER;
    return m_owner->QueryInterface( riid, parent );
}

ShimOutput *ShimAdapter::output()
{
    if (!m_output) m_output = new ShimOutput( this );
    return m_output;
}

ShimDxCoreAdapter *ShimAdapter::dxcore()
{
    if (!m_dxcore) {
        adapter_desc( &m_desc );
        m_dxcore = new ShimDxCoreAdapter( m_real, m_desc );
    }
    return m_dxcore;
}

/* IWineDXGIAdapter::get_adapter_info.  Wine's d3d12 matches this against the physical
 * Vulkan device by PCI IDs (vendor_id/device_id) and falls back to the LUID, so the PCI
 * IDs and the LUID must be the real adapter's.  driver_uuid/device_uuid are left zero:
 * Vulkan's VkPhysicalDeviceIDProperties needs the Vulkan headers to read and Wine accepts
 * the PCI-ID match on its own (the same choice probe/other/DXVK-PATCH.md documents). */
HRESULT ShimAdapter::get_adapter_info( wine_dxgi_adapter_info *info )
{
    if (!info) return E_POINTER;
    DXGI_ADAPTER_DESC1 d;
    adapter_desc( &d );
    ZeroMemory( info, sizeof(*info) );
    info->vendor_id = d.VendorId;
    info->device_id = d.DeviceId;
    info->luid      = d.AdapterLuid;
    trace( "  adapter get_adapter_info -> vendor=0x%04x device=0x%04x luid=%08lx:%08lx",
           (unsigned)d.VendorId, (unsigned)d.DeviceId,
           (unsigned long)d.AdapterLuid.HighPart, (unsigned long)d.AdapterLuid.LowPart );
    return S_OK;
}

HRESULT ShimAdapter::EnumOutputs( UINT index, IDXGIOutput **out )
{
    if (!out) return E_POINTER;
    *out = nullptr;
    if (index != 0) return DXGI_ERROR_NOT_FOUND;

    /* The real DXGI's output first, whenever it has one.  DXVK already enumerates Wine's
     * monitors as outputs for the adapter they belong to (measured: adapter 0 gives
     * \\.\DISPLAY1, 1366x768, AttachedToDesktop=1, 7 modes, 10 bpc), and a real output is
     * strictly better than the stand-in below.  Synthesising one unconditionally would have
     * *downgraded* the Radeon from 7 modes/10 bpc to 1 mode/8 bpc. */
    IDXGIOutput *real_out = NULL;
    if (SUCCEEDED( m_real->EnumOutputs( index, &real_out ) ) && real_out) {
        *out = real_out;
        trace( "  adapter EnumOutputs(%u) -> the real DXGI's output %p", index, (void *)real_out );
        return S_OK;
    }

    /* Only when the real adapter genuinely has no output does the shim supply one: this is
     * the Intel iGPU here, whose monitors all belong to the Radeon. */
    *out = static_cast<IDXGIOutput *>( output() );
    m_output->AddRef();
    trace( "  adapter EnumOutputs(%u) -> the real adapter has none, supplying the synthetic "
           "output %p", index, (void *)*out );
    return S_OK;
}

void ShimAdapter::adapter_desc( DXGI_ADAPTER_DESC1 *d ) const
{
    ZeroMemory( d, sizeof(*d) );
    /* LCS_SHIM_GPU unset: report exactly what the real adapter reports, so the LUID,
     * PCI ids and memory all stay consistent with what D3D11/D3D12 see. */
    m_real->GetDesc1( d );
    if (cfg().override_identity) {
        ZeroMemory( d->Description, sizeof(d->Description) );
        wcsncpy( d->Description, cfg().gpu, 127 );
        d->VendorId           = cfg().vendor;
        d->DeviceId           = cfg().device;
        d->SubSysId           = 0;
        d->Revision           = 0xA1;
        d->DedicatedVideoMemory   = (SIZE_T)cfg().vram_mb << 20;
        d->DedicatedSystemMemory  = 0;
        d->SharedSystemMemory     = (SIZE_T)2048 << 20;
        d->Flags              = 0;
    }
}

HRESULT ShimAdapter::GetDesc( DXGI_ADAPTER_DESC *d )
{
    if (!d) return E_POINTER;
    DXGI_ADAPTER_DESC1 d1;
    adapter_desc( &d1 );
    ZeroMemory( d, sizeof(*d) );
    wcsncpy( d->Description, d1.Description, 127 );
    d->VendorId = d1.VendorId;
    d->DeviceId = d1.DeviceId;
    d->SubSysId = d1.SubSysId;
    d->Revision = d1.Revision;
    d->DedicatedVideoMemory  = d1.DedicatedVideoMemory;
    d->DedicatedSystemMemory = d1.DedicatedSystemMemory;
    d->SharedSystemMemory    = d1.SharedSystemMemory;
    d->AdapterLuid           = d1.AdapterLuid;
    return S_OK;
}

HRESULT ShimAdapter::GetDesc1( DXGI_ADAPTER_DESC1 *d )
{
    if (!d) return E_POINTER;
    adapter_desc( d );
    char name[128];
    int i = 0;
    for (; i < 127 && d->Description[i]; i++) name[i] = (char)d->Description[i];
    name[i] = 0;
    trace( "  adapter GetDesc1 -> \"%s\" vendor=0x%04x device=0x%04x%s", name,
           (unsigned)d->VendorId, (unsigned)d->DeviceId,
           cfg().override_identity ? " (LCS_SHIM_GPU override)" : " (real)" );
    return S_OK;
}

HRESULT ShimAdapter::GetDesc2( DXGI_ADAPTER_DESC2 *d )
{
    if (!d) return E_POINTER;
    ZeroMemory( d, sizeof(*d) );
    m_real->GetDesc2( d );
    if (cfg().override_identity) {
        DXGI_ADAPTER_DESC1 d1;
        adapter_desc( &d1 );
        wcsncpy( d->Description, d1.Description, 127 );
        d->VendorId = d1.VendorId;
        d->DeviceId = d1.DeviceId;
        d->SubSysId = d1.SubSysId;
        d->Revision = d1.Revision;
        d->DedicatedVideoMemory  = d1.DedicatedVideoMemory;
        d->DedicatedSystemMemory = d1.DedicatedSystemMemory;
        d->SharedSystemMemory    = d1.SharedSystemMemory;
    }
    return S_OK;
}

HRESULT ShimAdapter::GetDesc3( DXGI_ADAPTER_DESC3 *d )
{
    if (!d) return E_POINTER;
    ZeroMemory( d, sizeof(*d) );
    m_real->GetDesc3( d );
    if (cfg().override_identity) {
        DXGI_ADAPTER_DESC1 d1;
        adapter_desc( &d1 );
        wcsncpy( d->Description, d1.Description, 127 );
        d->VendorId = d1.VendorId;
        d->DeviceId = d1.DeviceId;
        d->SubSysId = d1.SubSysId;
        d->Revision = d1.Revision;
        d->DedicatedVideoMemory  = d1.DedicatedVideoMemory;
        d->DedicatedSystemMemory = d1.DedicatedSystemMemory;
        d->SharedSystemMemory    = d1.SharedSystemMemory;
    }
    return S_OK;
}

/* ----------------------------------------------------------------- output impl */

HRESULT ShimOutput::QueryInterface( REFIID riid, void **out )
{
    if (!out) return E_POINTER;
    *out = nullptr;
    trace( "  output QI(%s)", guid_name( riid ) );
    if (IsEqualIID( riid, __uuidof(IUnknown) ) ||
        IsEqualIID( riid, __uuidof(IDXGIObject) ) ||
        IsEqualIID( riid, __uuidof(IDXGIOutput) ) ||
        IsEqualIID( riid, __uuidof(IDXGIOutput1) ) ||
        IsEqualIID( riid, __uuidof(IDXGIOutput2) ) ||
        IsEqualIID( riid, __uuidof(IDXGIOutput3) ) ||
        IsEqualIID( riid, __uuidof(IDXGIOutput4) ) ||
        IsEqualIID( riid, __uuidof(IDXGIOutput5) ) ||
        IsEqualIID( riid, __uuidof(IDXGIOutput6) )) {
        *out = static_cast<IDXGIOutput6 *>( this );
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG ShimOutput::AddRef() { return (ULONG)InterlockedIncrement( &m_ref ); }
ULONG ShimOutput::Release()
{
    LONG r = InterlockedDecrement( &m_ref );
    if (r < 1) InterlockedExchange( &m_ref, kPermanentRef );
    return (ULONG)r;
}

HRESULT ShimOutput::GetParent( REFIID riid, void **parent )
{
    if (!parent) return E_POINTER;
    return m_owner->QueryInterface( riid, parent );
}

HRESULT ShimOutput::GetDesc( DXGI_OUTPUT_DESC *d )
{
    if (!d) return E_POINTER;
    trace( "  output GetDesc -> \\\\.\\DISPLAY1 AttachedToDesktop=TRUE" );
    ZeroMemory( d, sizeof(*d) );
    wcscpy( d->DeviceName, L"\\\\.\\DISPLAY1" );
    screen_rect( &d->DesktopCoordinates );
    d->AttachedToDesktop = TRUE;
    d->Rotation          = DXGI_MODE_ROTATION_IDENTITY;
    d->Monitor           = MonitorFromPoint( POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY );
    return S_OK;
}

HRESULT ShimOutput::GetDesc1( DXGI_OUTPUT_DESC1 *d )
{
    if (!d) return E_POINTER;
    ZeroMemory( d, sizeof(*d) );
    wcscpy( d->DeviceName, L"\\\\.\\DISPLAY1" );
    screen_rect( &d->DesktopCoordinates );
    d->AttachedToDesktop = TRUE;
    d->Rotation          = DXGI_MODE_ROTATION_IDENTITY;
    d->Monitor           = MonitorFromPoint( POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY );
    d->BitsPerColor      = 8;
    d->ColorSpace        = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    /* sRGB / Rec.709 primaries */
    d->RedPrimary[0]   = 0.6400f; d->RedPrimary[1]   = 0.3300f;
    d->GreenPrimary[0] = 0.3000f; d->GreenPrimary[1] = 0.6000f;
    d->BluePrimary[0]  = 0.1500f; d->BluePrimary[1]  = 0.0600f;
    d->WhitePoint[0]   = 0.3127f; d->WhitePoint[1]   = 0.3290f;
    d->MinLuminance          = 0.50f;
    d->MaxLuminance          = 270.0f;
    d->MaxFullFrameLuminance = 270.0f;
    return S_OK;
}

void fill_mode( DXGI_MODE_DESC1 *m, DXGI_FORMAT fmt )
{
    ZeroMemory( m, sizeof(*m) );
    RECT r; screen_rect( &r );
    m->Width           = (UINT)(r.right - r.left);
    m->Height          = (UINT)(r.bottom - r.top);
    m->RefreshRate.Numerator   = cfg().refresh;
    m->RefreshRate.Denominator = 1;
    m->Format          = (fmt == DXGI_FORMAT_UNKNOWN) ? DXGI_FORMAT_R8G8B8A8_UNORM : fmt;
    m->ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    m->Scaling          = DXGI_MODE_SCALING_UNSPECIFIED;
    m->Stereo           = FALSE;
}

HRESULT ShimOutput::GetDisplayModeList1( DXGI_FORMAT fmt, UINT flags,
        UINT *count, DXGI_MODE_DESC1 *modes )
{
    if (!count) return E_POINTER;
    if (!modes) { *count = 1; return S_OK; }
    if (*count < 1) { *count = 1; return DXGI_ERROR_MORE_DATA; }
    if (flags & DXGI_ENUM_MODES_INTERLACED) {
        /* only one mode exists here; the flag only widens a list we do not have */
    }
    fill_mode( modes, fmt );
    *count = 1;
    return S_OK;
}

HRESULT ShimOutput::GetDisplayModeList( DXGI_FORMAT fmt, UINT flags,
        UINT *count, DXGI_MODE_DESC *modes )
{
    (void)flags;
    if (!count) return E_POINTER;
    if (!modes) { *count = 1; return S_OK; }
    if (*count < 1) { *count = 1; return DXGI_ERROR_MORE_DATA; }
    DXGI_MODE_DESC1 m1;
    fill_mode( &m1, fmt );
    ZeroMemory( modes, sizeof(*modes) );
    modes->Width           = m1.Width;
    modes->Height          = m1.Height;
    modes->RefreshRate     = m1.RefreshRate;
    modes->Format          = m1.Format;
    modes->ScanlineOrdering = m1.ScanlineOrdering;
    modes->Scaling          = m1.Scaling;
    *count = 1;
    return S_OK;
}

HRESULT ShimOutput::FindClosestMatchingMode1( const DXGI_MODE_DESC1 *,
        DXGI_MODE_DESC1 *got, IUnknown * )
{
    if (!got) return E_POINTER;
    fill_mode( got, DXGI_FORMAT_UNKNOWN );
    return S_OK;
}

HRESULT ShimOutput::FindClosestMatchingMode( const DXGI_MODE_DESC *,
        DXGI_MODE_DESC *got, IUnknown * )
{
    if (!got) return E_POINTER;
    DXGI_MODE_DESC1 m1;
    fill_mode( &m1, DXGI_FORMAT_UNKNOWN );
    ZeroMemory( got, sizeof(*got) );
    got->Width           = m1.Width;
    got->Height          = m1.Height;
    got->RefreshRate     = m1.RefreshRate;
    got->Format          = m1.Format;
    got->ScanlineOrdering = m1.ScanlineOrdering;
    got->Scaling          = m1.Scaling;
    return S_OK;
}

/* ------------------------------------------------------------- entry points */

ShimFactory *the_factory()
{
    /* Built once per process, on first use. */
    static ShimFactory *factory;
    static bool failed;
    if (factory || failed) return factory;
    HMODULE mod = real_module();
    if (!mod) { failed = true; return nullptr; }
    typedef HRESULT (WINAPI *pfn_create)( REFIID, void ** );
    pfn_create create = (pfn_create)GetProcAddress( mod, "CreateDXGIFactory1" );
    if (!create) { failed = true; return nullptr; }
    /* If dxgi-dxvk.dll turned out to be a copy of this shim, calling through would recurse
     * until the stack dies.  Detect it and answer DXGI_ERROR_UNSUPPORTED instead. */
    if ((FARPROC)create == (FARPROC)&CreateDXGIFactory1) { failed = true; return nullptr; }
    IDXGIFactory7 *real = NULL;
    if (FAILED( create( __uuidof(IDXGIFactory7), (void **)&real ) ) || !real) {
        failed = true;
        return nullptr;
    }
    factory = new ShimFactory( real );
    return factory;
}

HRESULT WINAPI shim_CreateDXGIFactory( REFIID riid, void **out )
{
    if (!out) return E_POINTER;
    *out = nullptr;
    ShimFactory *f = the_factory();
    trace( "CreateDXGIFactory(%s) real=%s", guid_name( riid ),
           f ? "ok" : "UNAVAILABLE" );
    if (!f) return DXGI_ERROR_UNSUPPORTED;
    return f->QueryInterface( riid, out );
}

HRESULT WINAPI shim_CreateDXGIFactory1( REFIID riid, void **out )
{
    return shim_CreateDXGIFactory( riid, out );
}

HRESULT WINAPI shim_CreateDXGIFactory2( UINT flags, REFIID riid, void **out )
{
    (void)flags;
    return shim_CreateDXGIFactory( riid, out );
}

HRESULT WINAPI shim_DXGIDeclareAdapterRemovalSupport( void )
{
    HMODULE mod = real_module();
    if (mod) {
        typedef HRESULT (WINAPI *pfn)( void );
        pfn f = (pfn)GetProcAddress( mod, "DXGIDeclareAdapterRemovalSupport" );
        if (f) return f();
    }
    return S_OK;
}

HRESULT WINAPI shim_DXGIGetDebugInterface1( UINT flags, REFIID riid, void **out )
{
    HMODULE mod = real_module();
    if (mod) {
        typedef HRESULT (WINAPI *pfn)( UINT, REFIID, void ** );
        pfn f = (pfn)GetProcAddress( mod, "DXGIGetDebugInterface1" );
        if (f) return f( flags, riid, out );
    }
    if (out) *out = nullptr;
    return E_NOINTERFACE;
}

} /* anonymous namespace */

extern "C" HRESULT WINAPI CreateDXGIFactory( REFIID riid, void **out )
{ return shim_CreateDXGIFactory( riid, out ); }
extern "C" HRESULT WINAPI CreateDXGIFactory1( REFIID riid, void **out )
{ return shim_CreateDXGIFactory1( riid, out ); }
extern "C" HRESULT WINAPI CreateDXGIFactory2( UINT flags, REFIID riid, void **out )
{ return shim_CreateDXGIFactory2( flags, riid, out ); }
extern "C" HRESULT WINAPI DXGIDeclareAdapterRemovalSupport( void )
{ return shim_DXGIDeclareAdapterRemovalSupport(); }
extern "C" HRESULT WINAPI DXGIGetDebugInterface1( UINT flags, REFIID riid, void **out )
{ return shim_DXGIGetDebugInterface1( flags, riid, out ); }

BOOL WINAPI DllMain( HINSTANCE, DWORD, LPVOID ) { return TRUE; }

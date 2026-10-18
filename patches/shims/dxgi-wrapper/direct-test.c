/* direct-test.c — call DXVK's dxgi without the wrapper in the way. */
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *PFN)(REFIID, void **);

/* mingw's IDXGIFactory1Vtbl here declares EnumAdapters1 without an RIID; the real
 * (DXVK/Windows) signature takes one. Declare the slot ourselves. */
struct factory1_vtbl_real {
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
};

int main(void)
{
    printf("PROBE: LoadLibraryW(dxvk-real\\dxgi.dll)\n"); fflush(stdout);
    HMODULE m = LoadLibraryW(L"C:\\windows\\system32\\dxvk-real\\dxgi.dll");
    printf("PROBE:   -> %p (err %lu)\n", (void *)m, GetLastError()); fflush(stdout);
    if (!m) return 1;

    PFN cf2 = (PFN)GetProcAddress(m, "CreateDXGIFactory2");
    PFN cf1 = (PFN)GetProcAddress(m, "CreateDXGIFactory1");
    printf("PROBE: GetProcAddress: CreateDXGIFactory2=%p CreateDXGIFactory1=%p\n",
           (void *)cf2, (void *)cf1); fflush(stdout);

    printf("PROBE: calling CreateDXGIFactory1 …\n"); fflush(stdout);
    IDXGIFactory1 *f = NULL;
    HRESULT hr = cf1(&IID_IDXGIFactory1, (void **)&f);
    printf("PROBE:   -> hr=0x%08lx factory=%p\n", (unsigned long)hr, (void *)f); fflush(stdout);
    if (FAILED(hr)) return 2;

    for (UINT i = 0; i < 4; i++) {
        IDXGIAdapter1 *a = NULL;
        HRESULT e = ((const struct factory1_vtbl_real *)f->lpVtbl)->EnumAdapters1(f, i, &IID_IDXGIAdapter1, (void **)&a);
        if (FAILED(e)) { printf("PROBE: EnumAdapters1(%u) hr=0x%08lx\n", i, (unsigned long)e); break; }
        DXGI_ADAPTER_DESC1 d; memset(&d, 0, sizeof(d));
        a->lpVtbl->GetDesc1(a, &d);
        char name[128]; int n = 0;
        for (const wchar_t *s = d.Description; *s && n < 127; s++) name[n++] = (char)*s;
        name[n] = 0;
        printf("PROBE: adapter %u: \"%s\" vendor=%04x device=%04x\n", i, name, d.VendorId, d.DeviceId);
        fflush(stdout);
        a->lpVtbl->Release(a);
    }
    f->lpVtbl->Release(f);
    printf("PROBE: DONE\n");
    return 0;
}
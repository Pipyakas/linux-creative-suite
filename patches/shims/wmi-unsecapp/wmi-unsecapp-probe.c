/*
 * wmi-unsecapp-probe.c -- tiny stand-in for what the raster app does at startup.
 *
 * Exists so the shim can be tested in ~2 s instead of a 70 s raster-app run.
 * It reproduces exactly the call Wine was failing on:
 *
 *     CoCreateInstance(CLSID {49bd2028-...}, NULL, CLSCTX_LOCAL_SERVER, ...)
 *
 * and then, when that succeeds, QueryInterface() for the two custom
 * interfaces and exercises their methods, which is where a hand-written
 * out-of-process COM server is most likely to fall over (marshalling of a
 * custom IID needs a proxy/stub).
 *
 * Usage: wmi-unsecapp-probe.exe
 * Exit status is 0 when CLSCTX_LOCAL_SERVER succeeded, 1 otherwise.
 */

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <stdio.h>

DEFINE_GUID(CLSID_UnsecuredApartment,
    0x49bd2028, 0x1523, 0x11d1, 0xad, 0x79, 0x00, 0xc0, 0x4f, 0xd8, 0xfd, 0xff);
DEFINE_GUID(IID_IUnsecuredApartment,
    0x1cfaba8c, 0x1523, 0x11d1, 0xad, 0x79, 0x00, 0xc0, 0x4f, 0xd8, 0xfd, 0xff);
DEFINE_GUID(IID_IWbemUnsecuredApartment,
    0x31739d04, 0x3471, 0x4cf4, 0x9a, 0x7c, 0x57, 0xa4, 0x4a, 0xe7, 0x19, 0x56);

static const char *why(HRESULT hr)
{
    switch ((unsigned long)hr)
    {
    case 0x80040154: return "REGDB_E_CLASSNOTREG";
    case 0x80004002: return "E_NOINTERFACE";
    case 0x80004001: return "E_NOTIMPL";
    case 0x8007000e: return "E_OUTOFMEMORY";
    case 0x80004005: return "E_FAIL";
    default:         return "";
    }
}

static void report(const char *what, HRESULT hr)
{
    printf("  %-42s hr=0x%08lx %s\n", what, (unsigned long)hr, why(hr));
    fflush(stdout);
}

static void try_context(const char *name, DWORD ctx)
{
    IUnknown *unk = NULL;
    HRESULT hr;

    printf("[%s] context 0x%lx\n", name, (unsigned long)ctx);

    /* What the raster app would ask for directly. */
    {
        void *p = NULL;
        hr = CoCreateInstance(&CLSID_UnsecuredApartment, NULL, ctx,
                              &IID_IUnsecuredApartment, &p);
        report("CoCreateInstance(IID_IUnsecuredApartment)", hr);
        if (SUCCEEDED(hr) && p) ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);

        p = NULL;
        hr = CoCreateInstance(&CLSID_UnsecuredApartment, NULL, ctx,
                              &IID_IWbemUnsecuredApartment, &p);
        report("CoCreateInstance(IID_IWbemUnsecuredApartment)", hr);
        if (SUCCEEDED(hr) && p) ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
    }

    hr = CoCreateInstance(&CLSID_UnsecuredApartment, NULL, ctx,
                          &IID_IUnknown, (void **)&unk);
    report("CoCreateInstance(IID_IUnknown)", hr);
    if (SUCCEEDED(hr) && unk)
    {
        IUnknown *unk2 = NULL;
        void *pv = NULL;

        hr = IUnknown_QueryInterface(unk, &IID_IUnsecuredApartment, (void **)&unk2);
        report("QueryInterface(IID_IUnsecuredApartment)", hr);
        if (SUCCEEDED(hr)) IUnknown_Release(unk2);

        unk2 = NULL;
        hr = IUnknown_QueryInterface(unk, &IID_IWbemUnsecuredApartment, (void **)&unk2);
        report("QueryInterface(IID_IWbemUnsecuredApartment)", hr);
        if (SUCCEEDED(hr)) IUnknown_Release(unk2);

        /*
         * GetModuleFileName on a proxy in our own process obviously cannot
         * tell us where the server lives; a successful call chain above is
         * the signal we care about.
         */
        (void)pv;
        IUnknown_Release(unk);
    }
    printf("\n");
}

int main(void)
{
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        printf("CoInitializeEx failed: 0x%08lx\n", (unsigned long)hr);
        return 1;
    }

    printf("=== wmi-unsecapp probe ===\n\n");
    try_context("INPROC_SERVER", CLSCTX_INPROC_SERVER);   /* 0x1 */
    try_context("LOCAL_SERVER", CLSCTX_LOCAL_SERVER);     /* 0x4 <-- the suite's */

    CoUninitialize();
    return 0;
}

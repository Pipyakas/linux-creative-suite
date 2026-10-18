# dxgi-wrapper

A native `dxgi.dll` (C, both bitnesses) that forwards everything to DXVK's real
`dxgi.dll` (kept under the private name `dxgi-dxvk.dll`) and adds
`QueryInterface(IID_IDXCoreAdapter)` on each adapter — on Windows a DXGI adapter *is*
a DXCore adapter, and the suite's apps ask for it after enumerating through DXGI.

- `dxgi-wrapper.c` — the wrapper
- `direct-test.c` — small direct-load test client
- `build.sh` — mingw build for 64- and 32-bit
- `install.sh` — swaps the DLL names in both bitnesses, `--show`/`--uninstall`

Status: builds and installs cleanly, but **not usable with DXVK**: DXVK enumerates
0 adapters under any DLL name other than exactly `dxgi.dll`, so the wrapper cannot
keep its own name. Kept for reference; the same idea ships as an optional patch in
`../../dxvk/dxvk-dxcore-adapter.patch`, which bakes it into DXVK's adapter instead.

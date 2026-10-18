# dxcore-shim

A native `dxcore.dll` (C, both bitnesses) re-exporting DXCore on top of DXGI. Wine
11.19's builtin `dxcore.dll` stubs `IDXCoreAdapterFactory::CreateAdapterList`, so the
suite's apps enumerate zero GPU devices through DXCore and refuse to start (`# devices: 0`;
the puppet app hangs on `!m_devices.empty()`). DXGI itself works, so this shim answers
`DXCoreCreateAdapterFactory` (and Wine's internal alias) by enumerating the DXGI
adapters instead.

- `dxcore-shim.c` — the shim
- `dxcore-probe.c` — test client (`dxcore-probe.exe`) that enumerates adapters and
  prints what DXCore reports
- `build.sh` — mingw build for 64- and 32-bit
- `install.sh` — installs into `system32`/`syswow64`, sets `*dxcore = native`,
  `--show`/`--uninstall` supported

Status: works (dxcore-probe goes from 0 to 2 adapters) but was left **out** of the
default install: it did not by itself unblock the raster editor or the puppet app,
and with it installed the compositor app never opened a window. Install by hand only
while debugging the GPU-enumeration path.

# dxgi-shim

A native `dxgi.dll` (C++, mingw) that wraps DXVK's DXGI and reports the adapter with
one attached output and a stable identity, so the suite's GPU probe helper sees
`# displays: 1 / # devices: 1` instead of zero and the apps stop refusing to start.

- `dxgi-shim.cpp` — the shim (vtable layout verified slot-by-slot against Wine's
  `dxgi*.idl`, mingw's `dxgi*.h` and the installed DXVK binary)
- `dxgi-shim.def` — the export list
- `build.sh` — builds `build/dxgi.dll` (`-static`, no exceptions/RTTI: a system DLL
  must import nothing beyond kernel32/user32)
- `install.sh` — backs up the current `dxgi.dll`, keeps the original as
  `dxgi-dxvk.dll` for the shim to forward to, sets `*dxgi = native,builtin`

Status: works (`adapters=2 outputs=2 attached=2`) and is safe to install; it is not
the whole story on its own — the apps ultimately needed a GPU *device* through the
DXCore path (see `../dxcore-shim/`) and the D3D12 feature-level override
(`../../vkd3d-proton/`). Optional override env vars: `LCS_SHIM_GPU`,
`LCS_SHIM_TRACE=1` (logs every answered entry point to `%TEMP%\dxgi-shim-trace.log`).

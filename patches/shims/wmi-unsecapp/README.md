# wmi-unsecapp

Implements Wine's missing "Unsecured Apartment" WMI COM class
(`CLSID_WbemUnsecuredApartment`, WineHQ bug 30439). The suite's WMI-using background
services ask for it with `CLSCTX_LOCAL_SERVER`, fail with `class not registered` and
null-deref, hijacking winedbg on every boot.

- `wmi-unsecapp.c` — in-process half (DllGetClassObject + class factory)
- `wmi-unsecapp-srv.c` — out-of-process half (LocalServer32), the context the apps use
- `wmi_unsecapp_ps.idl` + `wmi-unsecapp-ps.c` — hand-written proxy/stub so WMI
  interface pointers marshal correctly across processes
- `wmi-unsecapp-probe.c` — tiny test client (~2 s instead of a 70 s app run)
- `build.sh` / `build32.sh` / `install.sh` — 64-bit, 32-bit and installer; both
  registry views are registered, `--show`/`--uninstall` supported

Status: installed by default (`scripts/install.sh` runs it); services no longer crash.

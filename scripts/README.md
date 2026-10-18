# scripts

One line per file, in generic terms:

- `gui.sh` — drive a Wine app's GUI under KDE Plasma (Wayland) + Xwayland: pointer moves/clicks, key presses, typing, screenshots, and KWin's window list.
- `unblock.sh` — launch each configured app in turn and repeatedly raise its windows and answer whatever modal is in front until it reaches its workspace, logging verdicts (lit fraction, remaining dialogs) to `unblock.tsv`.
- `x11-windows.py` — minimal X11 window enumerator via ctypes: list mapped top-level windows, or watch for new ones over time.
- `x11-tree.py` — dump the full X window tree (depth, geometry, map state, override-redirect, class, name) at chosen sample times, with no name filtering.
- `window-lifetime.py` — poll the window tree and report how long each window matching a required name substring stays alive (first/last seen timestamps).
- `force-map.py` — repeatedly `XMapWindow` a large unmapped main window matched by a required name substring and log whether the map sticks and how long the window survives.
- `window-props.py` — catch a short-lived window matched by a required name substring and dump its X properties in-process before it is destroyed.
- `mk-edid.py` — generate a valid 128-byte EDID 1.4 blob for the 1366x768 panel, printed as a `wine reg add` argument for the prefix's monitor key.
- `win-click.c` — Win32 tool (build with mingw): print the top-level window tree filtered by a required class/caption substring, or post a real `WM_LBUTTONDOWN`/`WM_LBUTTONUP` pair at client coordinates from inside the prefix.
- `install.sh` — config-driven environment setup for the Wine prefix: tools, prefix init, native-DLL and graphics-driver fixes, pinned DXVK/D3D12 overrides, desktop-portal and KWin integration (it does not install any app).

`unblock.sh` and `install.sh` read `PREFIX`, `INSTALLER_DIR`, `SUITE_DIR` and the per-app
`APP_<codename>` entries from `config/apps.local.env` at the repo root (falling back to
`config/apps.example.env`).

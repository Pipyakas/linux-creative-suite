# XDG desktop portal configuration

Two small files that fix silent all-black screenshots under KDE Plasma on Wayland.

- `portals.conf` → `~/.config/xdg-desktop-portal/portals.conf`: force the KDE portal
  backend for every interface (`UseIn=default`, `kde.portal=true`, `gtk.portal=false`).
  The KDE backend is normally gated behind `UseIn=KDE`; with `XDG_CURRENT_DESKTOP`
  unset, `xdg-desktop-portal` fell back to gtk, which has no Screenshot implementation,
  so every capture silently produced an empty PNG.
- `10-kde-portal.conf` → `~/.config/systemd/user/xdg-desktop-portal.service.d/10-kde-portal.conf`:
  a systemd user drop-in that sets `XDG_CURRENT_DESKTOP=KDE` and
  `XDG_SESSION_DESKTOP=plasma` for the portal service, and raises `TimeoutStartSec=90`
  (the portal needs ~20 s here; systemd's default is 15 s).

After installing, restart the portal:

```sh
systemctl --user restart xdg-desktop-portal
```

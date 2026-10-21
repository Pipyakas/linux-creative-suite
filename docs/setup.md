# Setup: environment, install, prefix configuration

Everything published here runs a commercial creative suite under Wine on Linux. This page is
the environment and install guide; the verification harness is in
[automation.md](automation.md), and what actually runs is in [status.md](status.md).

The whole install and prefix configuration is driven by **`scripts/install.sh`**. It is
idempotent: re-running it re-applies every tweak below and installs only what is missing. Small
upstream-source changes that cannot be expressed as prefix settings live in **`patches/`** (the
one in use today is the D3D12 feature-level override, described below).

## Machine and requirements

Verified on: CachyOS, KDE Plasma 6 on Wayland with Xwayland, Wine **11.19** (plain, from the
distribution), Mesa 26.2, an Intel HD 620 iGPU driving the 1366x768 panel plus an AMD Radeon
R5 M445 dGPU (RADV) that has **no display connectors**, and a 64-bit Wine prefix here called
`$PREFIX`.

| Requirement | Why |
|---|---|
| **Plain Wine ≥ 11.17** | `KERNEL32.SetThreadpoolTimerEx` is a stub through 11.16 and implemented from 11.17 (`8fc5b43`). Every app in this suite calls it during startup and aborts with "unimplemented function" without it (WineHQ bug 57980). |
| **Never Proton** | Every Proton build tried stubs that function, so no Proton build can run the suite at all. A Proton fork would inherit the stub and be useless here. |
| **A prefix with a 32-bit layer** | The vendor's setup executable is 32-bit. WoW64-only Wine builds leave `syswow64` empty and the installer fails with "could not load kernel32.dll"; check that `$PREFIX/drive_c/windows/syswow64/kernel32.dll` exists. |
| `wine-mono`, `wine-gecko` | Without them Wine pops a modal Mono/Gecko installer on a fresh or migrated prefix, which blocks whatever app starts meanwhile. |
| `winetricks`, `cabextract`, `python3` | Native DLL installs; payload extraction; the planner/installer helpers in `scripts/install.sh`. |
| **~81 GB free** | Whole suite, installed with the installer's default options. The planner keeps a reserve and reports what did not fit instead of failing. |
| **A desktop session** | The apps render through Xwayland and the harness screenshots through the desktop portal. Run from X/Wayland, not from a bare SSH session; non-interactive shells must export `DISPLAY` and `XAUTHORITY` themselves (see [automation.md](automation.md)). |

Product paths are written `%SUITE_DIR%\<codename>` throughout this repo; the codenames are in
[config/apps.example.env](../config/apps.example.env) and the concrete paths for this machine
live in the git-ignored `config/apps.local.env`.

## Install

```bash
# Check what the media ships, what is installed and what fits
scripts/install.sh --list
scripts/install.sh --dry-run

# Everything, default options, resumable
scripts/install.sh

# Point at your own copy of the media / prefix / Wine build
scripts/install.sh --media /path/to/media --prefix "$PREFIX" --wine /path/to/wine
scripts/install.sh --reserve 20          # keep this much free space
scripts/install.sh --no-dxvk             # skip the DXVK install (not recommended)

# One app, or everything except one
scripts/install.sh --only <code>
scripts/install.sh --skip <code>
```

The installer media itself is not part of this repo; point `--media` at your own copy. Product
codes are the media's own short codes (`--list` prints them with sizes and install state).

`scripts/install.sh` drives the vendor's own silent installer. Two details matter:

* **One app per run.** The script temporarily reduces the suite manifest to a single product,
  installs it, restores the manifest, and repeats. That produces per-app results, lets one
  failure not abort the rest, and restores the manifest even if the run is killed.
* **Already-installed products are skipped**, so re-running resumes. Two products that need
  UWP/WinRT runtimes (and a 32-bit PDF-editor duplicate) cannot install under Wine and are
  excluded unless named explicitly.

**Landing the media's patched binaries.** The installer media patches binaries inside every app
and keeps the originals as `*.bak`; the patched ones are what run. Re-running the installer over
an existing install is a no-op ("there is no package to process") because it finds each product
already current — to actually re-patch, move each app folder aside first and pass
`--reinstall CODES`. The script deletes the leftover `*.bak` files afterwards (scoped to the
vendor trees; `--keep-baks` opts out).

**The PDF editor installs separately.** Through the suite installer it fails twice: the base
package returns `msiexec` 1639, and applying the shipped update package then returns 1603. Install
that product's own MSI directly together with its bundled transform (`.mst`) instead. That
succeeds and leaves it at its base build, which is the configuration verified in
[status.md](status.md). `scripts/install.sh` does not automate this step; point the matching `APP_`
entry at the executable it produces.

## Prefix configuration

These are the settings that took the suite from "mostly black windows" to the state in
[status.md](status.md). `scripts/install.sh` applies all of them; the "check" column is how to
confirm each one on a live prefix.

| # | Setting | Why it is needed | Check |
|---|---|---|---|
| 1 | `HKCU\Software\Wine\DllOverrides\msxml3 = builtin` (64-bit view); native `msxml3`/`msxml6` via winetricks for the 32-bit installer path | The **native 64-bit** msxml3 can never load here: winetricks ships no amd64 `system32\msxml3r.dll`, and its `DllMain` indexes a static table with `ptr >> 32`, which overflows for Wine's ~140 TB heap addresses. Without the override the 2026 apps log `create_server class {079aa557-…} not registered` and quit with no window. Also: remove any 32-bit `system32\msxml3r.dll` an older setup left behind — the wrong architecture only adds a second way for XML parsing to fail. | `grep 'msxml3' $PREFIX/user.reg` |
| 2 | `HKCU\Software\Wine\Drivers\Graphics = x11` (Wine's X11 driver, not the Wayland driver) | The vendor's embedded-Chromium surfaces (the editor's workspace surface, the PDF editor's browser panel) do not composite under the Wayland driver at all — they report "Cross process rendering is not supported!" and stay 100 % black while the ordinary menu bar paints. Measured: the editor's client area is 0.0000 lit on Wayland and paints its whole home screen on X11. `DISPLAY` must stay exported for the session. | `grep '"Graphics"' $PREFIX/user.reg` |
| 3 | **DXVK pinned to 2.4.1**, with `*d3d11` **and** `*dxgi` set to `native` | Two separate problems. (a) winetricks always installs the newest DXVK, and **3.1.1 renders nothing** for any Vulkan swapchain under this Wine — measured with a minimal D3D11 app: lit fraction 0.000 with 3.1.1 vs 1.000 with 2.4.1, same Wine, same driver, same compositor. The same back buffer presented through a GDI bitmap copy is 1.000, so the pixels exist and only DXVK 3.1.1's present is lost. (b) winetricks sets `*d3d11=native` but leaves `dxgi` alone, so Wine loads its own 252 KB DXGI on top of DXVK's 5.4 MB one; DXGI is what enumerates adapters, so the vendor's GPU probe sees an empty list (`Try DirectX: 0`) and one app asserts on it. | `grep -E 'd3d11|dxgi' $PREFIX/user.reg`; `vulkaninfo` for the ICD |
| 4 | **Patched vkd3d-proton reporting D3D12 feature level 12_0** (source change in `patches/`; the built DLLs are installed by `scripts/install.sh`) | The vendor gates on a D3D12 device at feature level 12_0, and Wine's bundled vkd3d tops out at 11_1 (`Feature level 0xc000 is not supported`). The patch makes `d3d12_device_determine_tiled_resources_tier()` return tier 2 even when the Vulkan driver reports `sparseBinding = false`, which unblocks 12_0. Effect: the GPU probe helper goes from `# displays: 0 / # devices: 0` to `# displays: 1 / # devices: 1`, and `puppet` goes from an error dialog to painting its home screen (0.9870). **Honest limits:** this is a lie for tiled-resource operations — real `CreateReservedResource`/tile-mapping work will fail — and it does **not** fix `raster`, which still exits with no window. | Run the vendor GPU probe helper from the prefix, or a D3D12 feature-level probe, and expect device version 12.0 |
| 5 | The vendor's background services **disabled** (`Start = 4` for both) | They crash at every Wine boot asking WMI for `CLSID_WbemUnsecuredApartment`, which Wine has never implemented (WineHQ bug 30439) and NULL-deref on. The crash also hijacks the debugger and takes down anything that needs it. Note `Start=2` means *auto*-start, the opposite. | `wine reg query '<service key>' /v Start` |
| 6 | `HKCU\Software\Wine\WineDbg\ShowCrashDialog = 0` | The editor spawns a helper executable that is not present in the installation at all and dies with an unhandled page fault; Wine's crash dialog is a modal debugger window on top of the still-running app, which reads as "the app is broken". Suppressing the dialog lets the app keep running. | `wine reg query 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog` |
| 7 | `HKCU\Environment\GF.DisableAcceleratedRenderer = 1` | The editor's "accelerated renderer" is a D3D swapchain drawn straight onto the window, and swapchain presents are the one path this Wine loses. With the key set, the editor paints through the ordinary window path instead (0.021 → 0.999 lit). It is applied through `HKCU\Environment` so every launch inherits it. | `wine reg query 'HKCU\Environment' /v GF.DisableAcceleratedRenderer` |
| 8 | `raster`'s install folder added to the Windows `PATH` (`HKLM\…\Session Manager\Environment\Path`) | `puppet`'s plugins import `HalideRuntime.dll`, which the vendor ships with `raster` rather than with `puppet`. Putting that folder on the `PATH` lets the loader find it; **no vendor file is copied or modified**. | `wine reg query '<env key>' /v Path` |
| 9 | Optional: a small COM shim that registers `CLSID_WbemUnsecuredApartment`, shipped with this repo's sources and its own `install.sh --uninstall` | Wine has never implemented that class, so the vendor's licensing/asset-management services crash without it. The shim is verified (`hr=0x00000000` in both 32- and 64-bit probes); it is independent of the display fixes. | run the shim's probe executable in both bitnesses |

Not applied by the current installer, on purpose: the DXCore shim (it works — 0 → 2 adapters — but
helped neither `raster` nor `puppet`, and with it installed `compositor` never opens a window) and
a DXGI wrapper (DXVK only works when its own file is named `dxgi.dll`, so a wrapper under that
name makes it hang in adapter enumeration). Details in [status.md](status.md) and
[benchmarks.md](benchmarks.md).

## Graphics notes for this class of machine

* **Everything renders on the panel-driven iGPU through DXVK.** The discrete adapter has no
  connectors. The material/3D group reports its own Vulkan-backed renderer in its UI (the texture
  painter shows a "(Vulkan)" build string), so it leans less on Wine's D3D translation — but its
  GPU requirements are stricter: one further 3D product from the same family failed the
  installer's system-requirements check (HyperDrive exit 195) on this hardware and is not
  installed here.
* **Both Vulkan drivers report `sparseBinding = false`** (Intel ANV and AMD RADV), which is why
  the feature-level override in row 4 has to fake the tier rather than enable it.
* **Do not force the discrete adapter awake.** On this machine it sits on a MUX'd link with no
  D3cold wake: an earlier "keep it awake" fix wedged the DRM/Vulkan stack and took every DXVK
  app with it, recoverable only by reboot. Leaving it to runtime-suspend is the configuration
  that produced the results in [status.md](status.md).
* On a multi-GPU desktop, select the GPU for DXVK with `DXVK_FILTER_DEVICE_NAME="<name>"`; on a
  hybrid laptop, `DRI_PRIME=<n>` selects the adapter for the process.

## Desktop-side fixes

### Screenshot portal (KDE)

Every UI measurement in this repo depends on being able to take a screenshot. On this system the
portal was **silently broken**: `XDG_CURRENT_DESKTOP` is unset, so `kde.portal` (`UseIn=KDE`)
was never selected and the portal fell back to the GTK backend for every interface — and that
backend has **no Screenshot implementation**. The screenshot tool then exited **0** and wrote a
valid, entirely black PNG, which looks exactly like an application that painted nothing. Weeks
of "the portal wedges, restart the compositor" were chasing this.

Two user-level fixes, both applied by `scripts/install.sh`:

`~/.config/systemd/user/xdg-desktop-portal.service.d/10-kde-portal.conf`

```ini
[Service]
Environment=XDG_CURRENT_DESKTOP=KDE
Environment=XDG_SESSION_DESKTOP=plasma
TimeoutStartSec=90
```

`~/.config/xdg-desktop-portal/portals.conf`

```ini
[default]
UseIn=default
kde.portal=true
gtk.portal=false
```

The long start timeout matters: the portal needs ~20 s to come up on this machine and systemd's
default `TimeoutStartSec` is 15 s, so `systemctl --user restart xdg-desktop-portal` used to
always end in a failed unit — which itself broke the screenshot path until the next login.

Verify:

```bash
systemctl --user is-active xdg-desktop-portal                 # active
gdbus introspect --session --dest org.freedesktop.portal.Desktop \
  --object-path /org/freedesktop/portal/desktop | grep -c Screenshot   # >= 1
spectacle -b -n -f -o /tmp/sp.png && magick /tmp/sp.png -format '%[fx:mean]\n' info:  # > 0
```

The harness in [automation.md](automation.md) exits non-zero on a blank capture for exactly this
reason — a blank frame must never be recorded as "nothing rendered".

### KWin scripts

Two problems on this machine are compositor-side, not app-side, and are handled by small KWin
scripting packages rather than by touching the apps:

* a captionless **embedded-browser overlay that never finishes loading** and covers the PDF
  editor's document window — the script moves it off-screen as soon as it maps;
* a **maximized-window size desync** after Wine and KWin race each other during app startup —
  re-sending the full size whenever a maximized window ends up smaller than the screen.

A package is a directory under `~/.local/share/kwin/scripts/<script-id>/` containing a
`metadata.json` (`"X-Plasma-API": "javascript"`, `"KPackageStructure": "KWin/Script"`) and
`contents/code/main.js`, installed and enabled by `scripts/install.sh`:

```bash
kwriteconfig6 --file kwinrc --group Plugins --key <script-id>Enabled true
qdbus6 org.kde.KWin /KWin reconfigure
```

Three traps learned here:

1. **The enable key must be written correctly or the script silently never loads.** A
   hand-edited `[Plugins]` entry that lands as part of a different key (or in the wrong group)
   produces no error anywhere — the script simply does not run. Verify by watching for the
   script's own log output (`journalctl --user | grep <tag>`); the harness relies on that for its
   window listings.
2. **A runtime `loadScript` refuses dashes in the name.** The package id may contain dashes, but
   the name passed to `qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript …` must
   be dash-free.
3. **After replacing a script's `main.js`, unload the running copy and reconfigure**, or the old
   code stays live until logout.

Loading a script by hand in a running session works without a compositor restart and is the
quickest way to test a change:

```bash
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript \
  ~/.local/share/kwin/scripts/<script-id>/contents/code/main.js <dashfree-name>
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start
```

## Post-install smoke test

```bash
wine --version                                        # 11.19 or newer; never a Proton build
grep -E 'd3d11|dxgi|msxml3|Graphics' "$PREFIX/user.reg"   # rows 1-3
scripts/gui.sh shot smoke                             # must be > 8 KB, else the portal is broken
scripts/unblock.sh <codename>                         # one app end to end, per-app verdict
```

The expected per-app results are the table in [status.md](status.md); each run appends a row and
writes a screenshot, and `--settle`/`ROUNDS` control how long the harness waits.

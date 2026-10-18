#!/usr/bin/env bash
# install.sh — prepare a Wine prefix for the creative suite: tools, prefix, native DLL
# fixes, graphics-driver registry steps, and the desktop-portal / KWin integration.
# It does NOT install any application; starting the installer is left to the user.
#
# What this script drops relative to the original suite-installer script at the repo
# root, and why — every item below is vendor-specific and cannot be expressed generically:
#   * suite-mode installer driving: the reduction of the media's suite manifest to one
#     product per run, the per-product code selection (--only/--skip/--reinstall),
#     reading and clearing the installer's own install-state database, the per-app
#     install loop, and the vendor-only installer command-line flags;
#   * vendor service-name registry edits (the vendor's background services NULL-deref
#     under Wine's unimplemented WMI apartment and hijack the debugger);
#   * per-app preference presets written into each app's own settings files;
#   * the per-app KWin title-matcher script (it matched one specific app's window title
#     to force that app maximized; the generic KWin script under patches/kwin/ is
#     installed instead);
#   * *.bak cleanup under the vendor's install trees after the media patches binaries;
#   * the Windows-PATH tweak that put one app's bundled DLL directory on another app's
#     loader path.
#
# Config: PREFIX, INSTALLER_DIR and SUITE_DIR come from config/apps.local.env at the
# repo root (sourced when present), falling back to config/apps.example.env defaults.
#
# Usage: install.sh [--prefix DIR] [--wine DIR] [--dry-run] [-h|--help]
set -euo pipefail

ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/linux-creative-suite"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

CFG="$ROOT/config/apps.local.env"
if [[ -f $CFG ]]; then
  # shellcheck disable=SC1090
  . "$CFG"
elif [[ -f $ROOT/config/apps.example.env ]]; then
  # shellcheck disable=SC1090
  . "$ROOT/config/apps.example.env"
fi
: "${PREFIX:=$HOME/.local/share/wineprefixes/suite}"
: "${INSTALLER_DIR:=}"
: "${SUITE_DIR:=}"

WINE_DIR=""
DRY_RUN=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

  --prefix DIR   Wine prefix (default: PREFIX from config/apps.local.env, else
                 $HOME/.local/share/wineprefixes/suite)
  --wine DIR     Wine or Proton dir containing bin/wine or files/bin/wine
                 (default: wine in PATH, else the newest plain build under
                 ~/.local/share/wine-builds/*, else a Proton build with a warning)
  --dry-run      print the plan, then exit without changing anything
  -h, --help     show this help
EOF
}

while (($#)); do
  case $1 in
    --prefix)  PREFIX=$(readlink -m "$2"); shift 2 ;;
    --wine)    WINE_DIR=$(readlink -f "$2"); shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

export WINEPREFIX="$PREFIX" WINEARCH=win64 WINEDEBUG=-all

# ---------------------------------------------------------------------------
# Wine selection: --wine DIR, else `wine` from PATH, else the newest plain Wine
# build in ~/.local/share/wine-builds, else a Proton build — with a warning.
# Proton still stubs KERNEL32.SetThreadpoolTimerEx, so a Proton build cannot run
# the apps; use plain Wine >= 11.17 (WineHQ bug 57980) or pass --wine DIR.
find_wine() {
  local d pat
  if [[ -n $WINE_DIR ]]; then
    for d in "$WINE_DIR/files/bin" "$WINE_DIR/bin" "$WINE_DIR"; do
      [[ -x $d/wine ]] && { echo "$d"; return; }
    done
    return 1
  fi
  command -v wine >/dev/null && return
  local plain=()
  mapfile -t plain < <(compgen -G "$HOME/.local/share/wine-builds/*/bin/wine" | sort -rV)
  ((${#plain[@]})) && { echo "${plain[0]%/wine}"; return; }
  local tools=(
    "$HOME/.local/share/Steam/compatibilitytools.d"
    "$HOME/.steam/steam/compatibilitytools.d"
    "$HOME/.var/app/com.valvesoftware.Steam/data/Steam/compatibilitytools.d"
  )
  for pat in 'Proton-CachyOS*' 'proton-cachyos*' 'GE-Proton*' 'Proton-GE*'; do
    for d in "${tools[@]}"; do
      # Newest version first; skip the x86_64_v3/v4 variants unless only they
      # exist. Dir names have spaces ("Proton-CachyOS Latest"), hence mapfile.
      local found=()
      mapfile -t found < <(compgen -G "$d/$pat/files/bin/wine" | sort -rV |
                           awk '!/_v[34]\//'; compgen -G "$d/$pat/files/bin/wine")
      if ((${#found[@]})); then
        echo "warning: using Proton's Wine (${found[0]%/files/bin/wine}):" >&2
        echo "  Proton stubs SetThreadpoolTimerEx, so a Proton build cannot run the apps;" >&2
        echo "  use plain Wine >= 11.17 (WineHQ bug 57980), or pass --wine DIR" >&2
        echo "${found[0]%/wine}"; return
      fi
    done
  done
  return 1
}
if WINE_BIN=$(find_wine); then
  if [[ -n $WINE_BIN ]]; then
    export PATH="$WINE_BIN:$PATH"
    # winetricks looks these up itself and would otherwise miss a non-PATH build.
    export WINE="$WINE_BIN/wine" WINESERVER="$WINE_BIN/wineserver"
  fi
else
  WINE_BIN=missing
fi

# Keep the session's Wayland/X11 handles available: Vulkan/DXVK want
# WAYLAND_DISPLAY, and the apps' Chromium-based HTML panel surfaces only composite
# under the X11 driver (see ensure_wine_drivers), so DISPLAY must stay exported.
: "${XDG_RUNTIME_DIR:=/run/user/$(id -u)}"
export XDG_RUNTIME_DIR
if [[ -z ${WAYLAND_DISPLAY:-} && -S $XDG_RUNTIME_DIR/wayland-0 ]]; then
  export WAYLAND_DISPLAY=wayland-0
fi
export DISPLAY="${DISPLAY:-:0}"
# Xwayland mints a NEW auth cookie at every login; without it X clients (wine,
# vulkaninfo) are refused connection.
if [[ -z ${XAUTHORITY:-} ]]; then
  for f in "$XDG_RUNTIME_DIR"/xauth_* "$HOME/.Xauthority"; do
    [[ -f $f ]] && { export XAUTHORITY=$f; break; }
  done
fi

check_wine_version() {
  if [[ $WINE_BIN == missing ]]; then return 0; fi
  local v
  v=$(wine --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+' || true)
  [[ -n $v ]] || return 0
  local major=${v%%.*} minor=${v##*.}
  if (( major > 11 )) || (( major == 11 && minor >= 17 )); then return 0; fi
  warn "Wine $v is older than 11.17: the suite's apps abort at startup on"
  warn "KERNEL32.SetThreadpoolTimerEx (WineHQ bug 57980). Install plain Wine >= 11.17"
  warn "(pacman -S wine wine-mono wine-gecko), or pass --wine DIR."
  return 0
}

if ((DRY_RUN)); then
  log "dry run — nothing will be changed"
  case $WINE_BIN in
    "")      echo "  wine:      $(command -v wine || echo 'not found')" ;;
    missing) echo "  wine:      not found" ;;
    *)       echo "  wine:      $WINE_BIN/wine" ;;
  esac
  echo "  prefix:    $WINEPREFIX"
  echo "  installer: ${INSTALLER_DIR:-<not configured>}"
  echo "  suite dir: ${SUITE_DIR:-<not configured>}"
  echo "  steps:"
  echo "    1. tools: wine, wine-mono, wine-gecko, winetricks, cabextract, python3"
  echo "    2. prefix init (wineboot --init) + 32-bit layer check"
  echo "    3. native msxml3/msxml6 (32-bit installer) + builtin msxml3 for 64-bit apps"
  echo "    4. Wine graphics driver x11; *dxgi native to match *d3d11"
  echo "    5. native gdiplus (apps whose UI fonts load through GDI+)"
  echo "    6. runtime fixes: WMI unsecured-apartment shim, renderer debug key,"
  echo "       Wine crash dialog off"
  echo "    7. pinned DXVK 2.4.1, D3D12 feature-level 12_0 override"
  echo "    8. desktop portal config + KWin HTML-panel-park script"
  exit 0
fi

ensure_tools() {
  local missing=()
  if [[ $WINE_BIN == missing ]]; then
    [[ -n $WINE_DIR ]] && die "no bin/wine or files/bin/wine under $WINE_DIR"
    if command -v pacman >/dev/null; then
      missing+=(wine wine-mono wine-gecko)
    else
      die "no Wine found. Install plain Wine >= 11.17, or pass --wine DIR."
    fi
  fi
  # Wine pops a modal "Wine Mono/Gecko Installer" on a prefix it created or
  # migrated, which blocks any app that starts in the meantime.
  if command -v pacman >/dev/null; then
    pacman -Qq wine-mono >/dev/null 2>&1 || missing+=(wine-mono)
    pacman -Qq wine-gecko >/dev/null 2>&1 || missing+=(wine-gecko)
  fi
  command -v winetricks >/dev/null || missing+=(winetricks)
  command -v cabextract >/dev/null || missing+=(cabextract)
  command -v python3 >/dev/null || missing+=(python)
  ((${#missing[@]})) || return 0
  command -v pacman >/dev/null || die "install these first: ${missing[*]}"
  log "Installing ${missing[*]}"
  sudo pacman -S --needed --noconfirm "${missing[@]}"
  WINE_BIN=""
  return 0
}

ensure_prefix() {
  if [[ ! -f $WINEPREFIX/system.reg ]]; then
    log "Creating Wine prefix $WINEPREFIX"
    mkdir -p "$WINEPREFIX"
    wine wineboot --init >/dev/null 2>&1 || true
    wineserver -w
  fi
  # Set-up.exe is 32-bit. Upstream WoW64-only Wine builds leave syswow64 empty and
  # fail with "could not load kernel32.dll"; a build with the 32-bit layer works.
  [[ -f $WINEPREFIX/drive_c/windows/syswow64/kernel32.dll ]] ||
    die "prefix has no 32-bit Windows layer ($(wine --version)). Use a Wine build that
includes it (or pass --wine DIR), delete $WINEPREFIX and re-run."
}

wine_reg() {
  # One registry write; failures are logged instead of aborting the whole setup.
  WINEPREFIX=$WINEPREFIX wine "$@" >>"$STATE_DIR/fixups.log" 2>&1 ||
    warn "registry step failed: wine $* (see $STATE_DIR/fixups.log)"
}

restart_wineserver() {
  WINEPREFIX=$WINEPREFIX timeout 30 wineserver -k >/dev/null 2>&1 || true
}

ensure_msxml() {
  # The 32-bit installer parses its requests with MSXML2.DOMDocument.3.0; Wine's
  # builtin msxml3 fails them with error 103, so the installer needs the native DLL.
  if ! grep -q '^"\*msxml3"="native' "$WINEPREFIX/user.reg"; then
    log "Installing native msxml3/msxml6 (winetricks)"
    winetricks -q --unattended msxml3 msxml6 >>"$STATE_DIR/winetricks.log" 2>&1
  fi
  local sys=$WINEPREFIX/drive_c/windows
  # Do NOT copy syswow64/msxml3r.dll into system32. That file is 32-bit (i386)
  # and a 64-bit msxml3 cannot use it; the wrong-architecture resource DLL only
  # adds a second way for the apps' XML settings parsing to fail. If a 32-bit copy
  # is sitting in system32 (older revisions did that), remove it.
  local r32=$sys/system32/msxml3r.dll
  if [[ -f $r32 ]] && ! file -b "$r32" | grep -q 'x86-64'; then
    log "Removing wrong-architecture (32-bit) msxml3r.dll from system32"
    rm -f "$r32"
  fi

  # Native 64-bit msxml3 CANNOT load here, and the 64-bit apps need a working MSXML3:
  # without it they log `apartment_add_dll couldn't load ... msxml3.dll` +
  # `create_server class {079aa557-4A18-424A-8EEE-E39F0A8D41B9} not registered` and
  # then quit with no window. Two structural reasons, both verified: the 64-bit
  # winetricks msxml3 needs an amd64 `system32\msxml3r.dll` that winetricks does not
  # ship, and its DllMain indexes a static table with `ptr >> 32`, which overflows the
  # table for Wine's ~140 TB heap addresses. Wine's *builtin* msxml3 always loads, so
  # force it for 64-bit apps. The 32-bit installer path keeps the native DLL: it is
  # 32-bit and loads fine with syswow64\msxml3r.dll.
  if ! grep -q '^"msxml3"="builtin' "$WINEPREFIX/user.reg"; then
    log "Forcing Wine's builtin msxml3 for 64-bit apps (native amd64 msxml3 cannot load)"
    wine_reg add 'HKCU\Software\Wine\DllOverrides' /v msxml3 /d builtin /f
    restart_wineserver
  fi
}

# Warn if the 64-bit builtin override is somehow gone again: the 64-bit apps then
# exit silently (they cannot create MSXML3's DOMDocument class).
check_msxml3_loadable() {
  [[ $WINE_BIN == missing ]] && return 0
  grep -q '^"msxml3"="builtin' "$WINEPREFIX/user.reg" && return 0
  warn "the builtin msxml3 override is missing again: the 64-bit apps will exit"
  warn "silently (no MSXML3 DOMDocument class). Re-run this script or set it by hand:"
  warn "  wine reg add 'HKCU\\Software\\Wine\\DllOverrides' /v msxml3 /d builtin /f"
  return 0
}

ensure_wine_drivers() {
  # Run Wine on the X11 driver, not the Wayland driver.
  #
  # The apps' Chromium-based HTML panel surfaces never composite at all under Wine's
  # Wayland driver: they report "Cross process rendering is not supported!" and every
  # pixel stays black, while ordinary GDI surfaces paint normally. Measured on this
  # machine: such a panel's client area is 0.0000 lit pixels on Wayland and paints
  # fully on X11.
  #
  # Xwayland must stay reachable, so DISPLAY has to keep exporting for the session;
  # a stale XAUTHORITY silently breaks every X client.
  if ! grep -q '^"Graphics"="x11"' "$WINEPREFIX/user.reg"; then
    log "Selecting Wine's X11 driver (Chromium panel surfaces never composite on Wayland)"
    wine_reg add 'HKCU\Software\Wine\Drivers' /v Graphics /t REG_SZ /d x11 /f
    restart_wineserver
  fi

  # DXVK's d3d11 with Wine's builtin dxgi is a mismatched stack: winetricks sets
  # "*d3d11"="native" but leaves dxgi alone, so Wine loads its own ~252 KB dxgi on top
  # of DXVK's ~5.4 MB one. DXGI is what enumerates adapters, and an empty adapter list
  # is what the apps assert on (`!m_devices.empty()`, WineHQ #59311) and what the GPU
  # probe helper reports as `Try DirectX: 0`. Load DXVK's dxgi too — the override is
  # only set once *d3d11 is native, so it always matches the d3d11 side.
  if grep -q '^"\*d3d11"="native' "$WINEPREFIX/user.reg" &&
     ! grep -q '^"\*dxgi"="native' "$WINEPREFIX/user.reg"; then
    log "Forcing DXVK's dxgi (native), matching the *d3d11 override"
    wine_reg add 'HKCU\Software\Wine\DllOverrides' /v '*dxgi' /d native /f
    restart_wineserver
  fi
  return 0
}

# Runtime fixes that are not DLL installs: the project's own COM shim, the video
# editor's renderer debug key, and Wine's crash dialog.
ensure_runtime_fixes() {
  [[ $WINE_BIN == missing ]] && return 0
  mkdir -p "$STATE_DIR"

  # Wine-side COM shim this project built and verified. Its install.sh has
  # --uninstall/--show and is skipped when the source tree is absent, so a plain
  # winetricks-only install still behaves as before. It provides
  # CLSID_WbemUnsecuredApartment, which Wine has never implemented (WineHQ bug
  # 30439): the vendor's WMI-using background services NULL-deref without it.
  local dir="$ROOT/patches/shims/wmi-unsecapp"
  if [[ -x $dir/install.sh ]]; then
    if PREFIX=$WINEPREFIX "$dir/install.sh" >>"$STATE_DIR/fixups.log" 2>&1; then
      log "Installed the WMI unsecured-apartment shim"
    else
      warn "Could not install the WMI shim (see $STATE_DIR/fixups.log)"
    fi
  fi

  # The video editor (nle) comes up with a 100% black client area: its "accelerated
  # renderer" is a D3D swapchain on the window, and on this machine nothing a Vulkan
  # swapchain presents ever reaches the compositor. The nle honours its own
  # debug-database key from the environment: GF.DisableAcceleratedRenderer=1 makes it
  # paint through the ordinary window path instead (measured 0.021 -> 0.999 lit
  # pixels). Wine imports HKCU\Environment into every process, so setting it here
  # makes it permanent for this prefix.
  wine_reg add 'HKCU\Environment' /v GF.DisableAcceleratedRenderer /t REG_SZ /d 1 /f
  log "Set GF.DisableAcceleratedRenderer=1 (the video editor paints its window again)"

  # Apps spawn helpers that crash under Wine. Wine's crash dialog is a modal winedbg
  # window that lands on top of the still-running host app, which reads as "the app
  # is broken". Suppress the dialog; the app keeps running.
  wine_reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f
  log "Disabled Wine's crash dialog (crashing helpers must not steal the screen)"
  restart_wineserver
  return 0
}

ensure_gdiplus() {
  # Several of the apps build their UI fonts from bundled OpenType files through
  # GDI+; Wine's builtin gdiplus fails that and the app exits right after start.
  if grep -q '^"\*gdiplus"="native' "$WINEPREFIX/user.reg"; then return 0; fi
  log "Installing native gdiplus (winetricks)"
  winetricks -q --unattended gdiplus >>"$STATE_DIR/winetricks.log" 2>&1
}

ensure_d3d12() {
  # The apps gate on a D3D12 device at feature level 12_0. This machine cannot
  # genuinely reach it — both GPUs report sparseBinding=false, and Wine 11.19's own
  # vkd3d hard-stubs tiled resources — but the apps only *ask* for the level at
  # device creation; they do not map tiled resources, so reporting the level is
  # enough.
  #
  # Measured effect: the GPU probe helper goes from `# displays: 0 / # devices: 0`
  # to `# displays: 1 / # devices: 1` with Device version 12.0, and one suite app
  # goes from an `Im_devices.empty()` startup failure to painting its full home
  # screen (0.9686 lit). See the repo docs for exactly what is faked — real
  # tiled-resource mappings will still fail.
  local pin="$ROOT/patches/vkd3d-proton/install.sh"
  if [[ ! -x $pin ]]; then
    warn "the D3D12 12_0 override installer is missing at $pin — the apps will report 0 displays"
    return 0
  fi
  if grep -q '^"\*d3d12"="native' "$WINEPREFIX/user.reg"; then return 0; fi
  log "Installing the patched vkd3d-proton (D3D12 feature level 12_0 override)"
  WINEPREFIX="$WINEPREFIX" "$pin" install >>"$STATE_DIR/fixups.log" 2>&1 ||
    warn "the D3D12 override failed; see $STATE_DIR/fixups.log"
  return 0
}

ensure_dxvk() {
  if grep -q '^"\*d3d11"="native' "$WINEPREFIX/user.reg"; then return 0; fi
  # Look for a GPU Vulkan driver (ICD) rather than run vulkaninfo, which can
  # crash without a display.
  if ! compgen -G "/usr/share/vulkan/icd.d/*.json" >/dev/null &&
     ! compgen -G "/etc/vulkan/icd.d/*.json" >/dev/null; then
    warn "no Vulkan driver found; skipping DXVK"
    return 0
  fi
  # DXVK is PINNED, not taken from winetricks: winetricks always installs the newest
  # release, and the newest DXVK renders *nothing* for any Vulkan swapchain under
  # Wine 11.19 on this machine — measured lit-pixel fraction of the client area is
  # 0.000 with the newest and 1.000 with 2.4.1, same Wine, same Wayland driver, same
  # KWin. wined3d and a GDI StretchDIBits of the same back buffer both give 1.000,
  # so the pixels exist and only the newest DXVK's present is lost.
  local pin="$ROOT/patches/dxvk/pin-2.4.1.sh"
  if [[ -x $pin ]]; then
    log "Installing DXVK 2.4.1 (pinned; winetricks would install the known-broken newest DXVK: 0.000 lit vs 1.000 for 2.4.1)"
    PREFIX="$WINEPREFIX" "$pin" >>"$STATE_DIR/fixups.log" 2>&1 ||
      warn "the pinned DXVK install failed; see $STATE_DIR/fixups.log"
  else
    warn "the pinned DXVK installer is missing at $pin — not falling back to winetricks,"
    warn "which installs the known-broken newest DXVK (0.000 lit vs 1.000 for 2.4.1)"
  fi
  return 0
}

# Copy src to dst, backing dst up to dst.bak once if it already exists.
install_conf() {
  local src=$1 dst=$2
  if [[ ! -f $src ]]; then warn "missing $src — skipped"; return 0; fi
  mkdir -p "$(dirname "$dst")"
  if [[ -f $dst && ! -e $dst.bak ]]; then
    cp -f "$dst" "$dst.bak"
    log "Backed up $dst -> $dst.bak"
  fi
  cp -f "$src" "$dst"
  log "Installed $(basename "$dst") -> $dst"
}

ensure_portal_kwin() {
  local cfg=${XDG_CONFIG_HOME:-$HOME/.config}
  # Silent all-black screenshots: with XDG_CURRENT_DESKTOP unset the desktop portal
  # falls back to the gtk backend, which has no Screenshot implementation, and every
  # capture produces an empty PNG. Force the KDE backend (portals.conf) and give the
  # portal service the right desktop identity plus a longer start timeout (systemd
  # drop-in).
  install_conf "$ROOT/patches/desktop/portals.conf" \
               "$cfg/xdg-desktop-portal/portals.conf"
  install_conf "$ROOT/patches/desktop/10-kde-portal.conf" \
               "$cfg/systemd/user/xdg-desktop-portal.service.d/10-kde-portal.conf"
  if command -v systemctl >/dev/null; then
    systemctl --user daemon-reload >/dev/null 2>&1 || true
    systemctl --user restart xdg-desktop-portal >/dev/null 2>&1 ||
      warn "could not restart xdg-desktop-portal; restart it by hand or log out"
  fi

  # KWin script: parks the apps' stuck captionless HTML panel ("Loading…", ~808x768)
  # offscreen so the window underneath stays usable. TARGET_CLASS inside
  # contents/code/main.js must be set to the target app's exe file name (its KWin
  # resource class, lowercase) when the default does not fit.
  local id=lcs-html-panel-park src="$ROOT/patches/kwin/html-panel-park"
  local dir=${XDG_DATA_HOME:-$HOME/.local/share}/kwin/scripts/$id
  if [[ -d $src ]]; then
    mkdir -p "$dir"
    cp -r "$src/metadata.json" "$src/contents" "$dir/"
    log "Installed KWin script $id -> $dir"
    if command -v kwriteconfig6 >/dev/null; then
      kwriteconfig6 --file kwinrc --group Plugins --key ${id}Enabled true
      log "Enabled KWin script $id in kwinrc (takes effect at the next reconfigure/login)"
    else
      warn "kwriteconfig6 not found; enable the $id script in kwinrc by hand"
    fi
  else
    warn "KWin script source not found at $src — skipped"
  fi
  return 0
}

installer_hint() {
  [[ -n $INSTALLER_DIR && -f $INSTALLER_DIR/Set-up.exe ]] || return 0
  cat <<EOF

Installer media with Set-up.exe found: $INSTALLER_DIR
(apps install under: ${SUITE_DIR:-<not configured>})
This script does not drive it; start it manually in this prefix:
  cd "$INSTALLER_DIR" && WINEPREFIX="$PREFIX" wine Set-up.exe
No vendor-only command-line flags are involved — run it interactively.
EOF
  return 0
}

# ---------------------------------------------------------------------------
ensure_tools
check_wine_version
ensure_prefix
mkdir -p "$STATE_DIR"
ensure_msxml
check_msxml3_loadable
ensure_wine_drivers
ensure_gdiplus
ensure_runtime_fixes
ensure_dxvk
ensure_d3d12
ensure_portal_kwin
installer_hint
log "Environment ready: prefix $WINEPREFIX"
log "See docs/setup.md for the per-app launch commands and further fixes."

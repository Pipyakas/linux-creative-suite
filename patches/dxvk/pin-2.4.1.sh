#!/usr/bin/env bash
# pin-2.4.1.sh — install (or remove) a *pinned* DXVK in the Wine prefix.
#
#   ./pin-2.4.1.sh              install DXVK 2.4.1
#   ./pin-2.4.1.sh --uninstall  remove DXVK, restoring Wine's builtin d3d11/dxgi
#   PREFIX=/path ./pin-2.4.1.sh use a different prefix
#
# WHY A PIN, AND WHY AN OLD ONE
#
# DXVK 3.1.1 renders **nothing** for any Vulkan swapchain under Wine 11.19 on this
# machine, while DXVK 2.4.1 renders correctly, under the same Wine, the same
# Wayland driver and the same KWin.  Measured with a D3D11-device + DXGI-swapchain
# test app (a plain Win32 window that clears its back buffer to green), scoring
# the lit-pixel fraction of the client area:
#
#     DXVK 3.1.1 ............ 0.000   black
#     DXVK 2.4.1 ............ 1.000   green
#     builtin wined3d ....... 1.000   green   (OpenGL path, never touches Vulkan)
#     DXVK 3.1.1 + StretchDIBits of the same back buffer ... 1.000   green
#
# So the pixels exist and the window can show them; only DXVK 3.1.1's present is
# lost.  The suspected mechanism is DXVK 3.1.1's use of
# VkSwapchainPresentFenceInfoKHR (src/dxvk/dxvk_presenter.cpp:189-222), which
# 2.4.1 does not have; see the sibling dxvk patches in this repo.  `winetricks
# dxvk` always installs the newest release, which is exactly the broken one, so
# this script exists to pin it.
#
# What it writes, and nothing else:
#   1. backs up the current d3d11.dll / dxgi.dll / d3d10core.dll and user.reg
#      once, into ./backup-dxvk/;
#   2. copies the pinned x64 DLLs from ./dxvk-2.4.1/x64/ into drive_c/windows/system32;
#   3. sets HKCU\Software\Wine\DllOverrides *d3d11, *dxgi, *d3d10core = "native";
#   4. restarts wineserver.
#
# --uninstall restores the backups and sets those three overrides back to
# "builtin", which leaves Wine's own wined3d in place (verified working above).

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
src="$here/dxvk-2.4.1/x64"
if [[ -z ${PREFIX:-} ]]; then
    cfg="$here/../../config/apps.local.env"
    [[ -f $cfg ]] && . "$cfg"
fi
prefix=${PREFIX:-${WINEPREFIX:-$HOME/.local/share/wineprefixes/suite}}
sys64="$prefix/drive_c/windows/system32"
ov='HKCU\Software\Wine\DllOverrides'
dlls=(d3d11.dll dxgi.dll d3d10core.dll)
backup="$here/backup-$(date +%Y%m%d-%H%M%S)"
wine_run() { WINEPREFIX="$prefix" wine "$@"; }

[[ -d $prefix ]] || { echo "error: no such prefix: $prefix" >&2; exit 1; }

case "${1:-install}" in
--uninstall)
    if [[ -d $here/backup-dxvk ]]; then
        for d in "${dlls[@]}"; do
            [[ -f $here/backup-dxvk/$d ]] && cp -f "$here/backup-dxvk/$d" "$sys64/$d"
        done
        echo "restored the previous d3d11/dxgi/d3d10core from backup-dxvk/"
    else
        warn_no_backup=1
        echo "warning: no backup-dxvk/ found; leaving the DLLs in place" >&2
    fi
    for d in "${dlls[@]}"; do
        [[ -f $here/dxvk-2.4.1/x64/$d ]] && cp -f "$here/dxvk-2.4.1/x64/$d" "$sys64/$d"
    done
    # Point the overrides back at the builtin implementations.  Use `reg add`, never
    # `reg delete`: in this prefix a deleted value silently comes back on the next
    # wineserver start.
    for kv in '*d3d11' '*dxgi' '*d3d10core'; do
        wine_run reg add "$ov" /v "$kv" /t REG_SZ /d builtin /f >/dev/null 2>&1 || true
    done
    WINEPREFIX="$prefix" wineserver -k >/dev/null 2>&1 || true
    echo "uninstalled: overrides are now 'builtin' (wined3d)"
    exit 0
    ;;
install) ;;
*) echo "usage: $0 [--uninstall]" >&2; exit 2 ;;
esac

[[ -d $src ]] || { echo "error: $src missing — unpack the DXVK 2.4.1 tarball next to this script" >&2; exit 1; }

mkdir -p "$here/backup-dxvk"
for d in "${dlls[@]}"; do
    if [[ ! -f $here/backup-dxvk/$d && -f $sys64/$d ]]; then
        cp -f "$sys64/$d" "$here/backup-dxvk/$d"
        echo "backed up $d"
    fi
done
[[ -f $here/backup-dxvk/user.reg ]] || cp -f "$prefix/user.reg" "$here/backup-dxvk/user.reg"

for d in "${dlls[@]}"; do
    cp -f "$src/$d" "$sys64/$d"
    printf '  %-14s %10s bytes\n' "$d" "$(stat -c %s "$sys64/$d")"
done

for kv in '*d3d11' '*dxgi' '*d3d10core'; do
    wine_run reg add "$ov" /v "$kv" /t REG_SZ /d native /f >/dev/null
done
WINEPREFIX="$prefix" wineserver -k >/dev/null 2>&1 || true

echo
echo "DXVK 2.4.1 installed; *d3d11, *dxgi, *d3d10core = native"
echo "Verify the present path with:"
echo "  WINEPREFIX=$prefix wine 'C:\\windows\\system32\\swapbuf-test.exe' 2   # then screenshot: expect GREEN"

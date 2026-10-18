#!/usr/bin/env bash
# install.sh -- install (or remove) the native DXCore shim in the suite Wine
#               prefix.
#
#   ./install.sh                 install into the default prefix
#   ./install.sh --uninstall     remove it again
#   ./install.sh --show          list what it would write
#   PREFIX=/path ./install.sh    use a different prefix
#
# The prefix comes from $PREFIX; when unset it is read from config/apps.local.env
# at the repo root, then falls back to the default from config/apps.example.env.
#
# What it does
#   1. backs up user.reg to ../backup-20261015-dxcore/ (once; never overwrites
#      an existing backup) -- always before anything is written;
#   2. copies build/dxcore.dll          -> drive_c/windows/system32/dxcore.dll
#             build/dxcore-probe.exe    -> drive_c/windows/system32/
#             build/win32/dxcore.dll    -> drive_c/windows/syswow64/dxcore.dll
#             build/win32/dxcore-probe.exe -> drive_c/windows/syswow64/
#   3. sets HKCU\Software\Wine\DllOverrides "*dxcore"="native";
#   4. restarts wineserver.
#
# The ONLY DllOverrides value it writes or deletes is "*dxcore".  Every other
# override in the prefix (*d3d11, *d3d10core, *d3d9, *dxgi, vulkan-1, msxml3,
# ...) is left untouched.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build="$here/build"
if [[ -z ${PREFIX:-} ]]; then
    cfg="$here/../../../config/apps.local.env"
    [[ -f $cfg ]] && . "$cfg"
fi
prefix=${PREFIX:-${WINEPREFIX:-$HOME/.local/share/wineprefixes/suite}}
backup="$here/../backup-20261015-dxcore"

sys64="$prefix/drive_c/windows/system32"
sys32="$prefix/drive_c/windows/syswow64"
ovk='HKCU\Software\Wine\DllOverrides'

wine_run() { WINEPREFIX="$prefix" wine "$@"; }

[[ -d $prefix ]] || { echo "error: no such prefix: $prefix" >&2; exit 1; }

case ${1:-} in
--show)
    echo "prefix:    $prefix"
    echo "backup:    $backup/user.reg  (copied once, before installing)"
    echo "files:"
    echo "  $build/dxcore.dll                -> $sys64/dxcore.dll"
    echo "  $build/dxcore-probe.exe          -> $sys64/dxcore-probe.exe"
    echo "  $build/win32/dxcore.dll          -> $sys32/dxcore.dll"
    echo "  $build/win32/dxcore-probe.exe    -> $sys32/dxcore-probe.exe"
    echo "registry:"
    echo "  $ovk  /v \"*dxcore\" /d \"native\"   (HKCU, via wine reg)"
    echo "  (no other DllOverrides value is touched)"
    exit 0
    ;;
--uninstall)
    # Only remove the override if it is still ours.
    current=$(WINEPREFIX="$prefix" wine reg query "$ovk" /v '*dxcore' 2>/dev/null \
              | awk '/REG_SZ/ { print $3 }' || true)
    if [[ $current == native ]]; then
        wine_run reg delete "$ovk" /v '*dxcore' /f >/dev/null
        echo "removed HKCU\\\\Software\\\\Wine\\\\DllOverrides \"*dxcore\""
    elif [[ -n $current ]]; then
        echo "left HKCU\\\\Software\\\\Wine\\\\DllOverrides \"*dxcore\" = \"$current\" alone (not ours)"
    else
        echo "no \"*dxcore\" override present"
    fi
    rm -f "$sys64/dxcore.dll" "$sys64/dxcore-probe.exe" \
          "$sys32/dxcore.dll" "$sys32/dxcore-probe.exe"
    echo "removed dxcore.dll / dxcore-probe.exe from system32 and syswow64"
    WINEPREFIX="$prefix" wineserver -k 2>/dev/null || true
    exit 0
    ;;
--install|"")
    ;;
*)
    echo "usage: $0 [--install|--uninstall|--show]" >&2
    exit 2
    ;;
esac

for f in "$build/dxcore.dll" "$build/dxcore-probe.exe" \
         "$build/win32/dxcore.dll" "$build/win32/dxcore-probe.exe"; do
    [[ -f $f ]] || { echo "error: $f missing -- run ./build.sh first" >&2; exit 1; }
done

mkdir -p "$backup"
if [[ -f $prefix/user.reg ]]; then
    # -n: the first backup wins, so re-running install never overwrites the
    # pristine pre-shim registry.
    cp -n "$prefix/user.reg" "$backup/user.reg" || true
    ls -l "$backup/user.reg"
fi
# A copy of the file we are about to add, for the record.
install -m 0644 "$build/dxcore.dll" "$backup/dxcore.dll.64"
install -m 0644 "$build/win32/dxcore.dll" "$backup/dxcore.dll.32"

echo "==> installing into $prefix"
install -m 0644 "$build/dxcore.dll"              "$sys64/dxcore.dll"
install -m 0755 "$build/dxcore-probe.exe"        "$sys64/dxcore-probe.exe"
install -m 0644 "$build/win32/dxcore.dll"        "$sys32/dxcore.dll"
install -m 0755 "$build/win32/dxcore-probe.exe"  "$sys32/dxcore-probe.exe"
ls -l "$sys64/dxcore.dll" "$sys64/dxcore-probe.exe" \
      "$sys32/dxcore.dll" "$sys32/dxcore-probe.exe"

echo "==> setting HKCU\\\\Software\\\\Wine\\\\DllOverrides \"*dxcore\"=\"native\""
wine_run reg add "$ovk" /v '*dxcore' /d native /f >/dev/null
wine_run reg query "$ovk" /v '*dxcore'

echo "==> restarting wineserver"
WINEPREFIX="$prefix" wineserver -k 2>/dev/null || true
sleep 1

echo
echo "installed.  verify with:"
echo "  WINEPREFIX=$prefix wine 'C:\\\\windows\\\\system32\\\\dxcore-probe.exe'   # 64-bit"
echo "  WINEPREFIX=$prefix wine 'C:\\\\windows\\\\syswow64\\\\dxcore-probe.exe'   # 32-bit"

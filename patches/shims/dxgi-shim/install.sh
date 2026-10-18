#!/usr/bin/env bash
# install.sh -- install (or remove) the native DXGI shim that reports a GPU with an
# attached display, so the raster image editor and the puppet app stop refusing to
# start.
#
#   ./install.sh              install into the default prefix
#   ./install.sh --uninstall  remove it and put the original DXGI back
#   PREFIX=/path ./install.sh use a different prefix
#
# The prefix comes from $PREFIX; when unset it is read from config/apps.local.env
# at the repo root, then falls back to the default from config/apps.example.env.
# What it writes, and nothing else:
#   1. backs up the existing drive_c/windows/system32/dxgi.dll  (once) into
#      ../backup-20261016-dxgi/, together with user.reg;
#   2. copies the *current* dxgi.dll (DXVK's) to system32/dxgi-dxvk.dll, because the
#      shim forwards everything it does not override to the real implementation and
#      must not load itself;
#   3. copies build/dxgi.dll -> system32/dxgi.dll;
#   4. sets HKCU\Software\Wine\DllOverrides "*dxgi" = "native,builtin";
#   5. restarts wineserver.
#
# --uninstall reverses 3, 2 and 4 and leaves dxgi-dxvk.dll behind (harmless; it is
# only ever loaded by the shim).

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build="$here/build"
if [[ -z ${PREFIX:-} ]]; then
    cfg="$here/../../../config/apps.local.env"
    [[ -f $cfg ]] && . "$cfg"
fi
prefix=${PREFIX:-${WINEPREFIX:-$HOME/.local/share/wineprefixes/suite}}

# The backup is per prefix, and it MUST be: it is the source of the original DXGI that
# becomes dxgi-dxvk.dll, so a single shared backup would make a second prefix forward to the
# first prefix's DXGI (e.g. a throwaway test prefix with DXVK 2.4.1 would silently get the
# main prefix's 3.1.1 as its "real" DXGI).  The main prefix keeps its established path.
default_prefix=$HOME/.local/share/wineprefixes/suite
if [[ $prefix == "$default_prefix" ]]; then
    backup="$here/../backup-20261016-dxgi"
else
    backup="$here/../backup-$(basename "$prefix")-dxgi"
fi
sys64="$prefix/drive_c/windows/system32"
ovk='HKCU\Software\Wine\DllOverrides'
ov='HKCU\Software\Wine\DllOverrides'
wine_run() { WINEPREFIX="$prefix" wine "$@"; }

# Killing wineserver takes down every Wine process in the prefix, including whatever the
# user has open (it killed a running the video editor twice).  It is NOT needed: a new process
# picks the shim up when it starts, and an already-running one keeps the DXGI it loaded.
# So it is opt-in only.
restart_wineserver() {
    if [[ ${RESTART_WINESERVER:-0} == 1 ]]; then
        WINEPREFIX="$prefix" wineserver -k >/dev/null 2>&1 || true
        echo "wineserver restarted (RESTART_WINESERVER=1)"
    else
        echo "note: wineserver left alone; apps already running keep their old DXGI."
        echo "      pass RESTART_WINESERVER=1 to force it (kills every Wine process)."
    fi
}

[[ -d $prefix ]] || { echo "error: no such prefix: $prefix" >&2; exit 1; }

case "${1:-install}" in
--uninstall)
    if [[ -f $backup/dxgi.dll ]]; then
        cp -f "$backup/dxgi.dll" "$sys64/dxgi.dll"
        echo "restored the original dxgi.dll"
    else
        echo "warning: no backup at $backup/dxgi.dll; removing the shim as-is" >&2
        rm -f "$sys64/dxgi.dll"
    fi
    rm -f "$sys64/dxgi-dxvk.dll"
    rm -f "$sys64/dxgi-shim-probe.exe" "$sys64/factory-layout-probe.exe"
    wine_run reg delete "$ov" /v '*dxgi' /f >/dev/null 2>&1 || true
    wine_run reg add "$ovk" /v '*dxgi' /t REG_SZ /d native /f >/dev/null 2>&1 || true
    restart_wineserver
    echo "uninstalled (the *dxgi override is back to plain 'native', i.e. DXVK)"
    exit 0
    ;;
install) ;;
*) echo "usage: $0 [--uninstall]" >&2; exit 2 ;;
esac

[[ -f $build/dxgi.dll ]] || { echo "error: $build/dxgi.dll missing - run build.sh first" >&2; exit 1; }

mkdir -p "$backup"
if [[ ! -f $backup/dxgi.dll && -f $sys64/dxgi.dll ]]; then
    cp -f "$sys64/dxgi.dll" "$backup/dxgi.dll"
    echo "backed up the existing dxgi.dll -> $backup/dxgi.dll"
fi
[[ -f $backup/user.reg ]] || cp -f "$prefix/user.reg" "$backup/user.reg"

# The shim forwards to the real implementation under a private name.
#
# dxgi-dxvk.dll must always come from the *original* DXGI, never from whatever is currently
# installed: on a re-install $sys64/dxgi.dll is already the shim, and copying it here would
# make the shim forward to itself (which recurses until the stack dies and shows up as
# CreateDXGIFactory1 returning DXGI_ERROR_UNSUPPORTED).  The pristine original is the
# backup taken the first time, so take it from there and refuse if neither exists.
if [[ -f $backup/dxgi.dll ]]; then
    cp -f "$backup/dxgi.dll" "$sys64/dxgi-dxvk.dll"
    echo "dxgi-dxvk.dll <- $backup/dxgi.dll (the original)"
elif [[ -f $sys64/dxgi.dll ]] && ! cmp -s "$sys64/dxgi.dll" "$build/dxgi.dll"; then
    cp -f "$sys64/dxgi.dll" "$sys64/dxgi-dxvk.dll"
    echo "dxgi-dxvk.dll <- the dxgi.dll that was installed (no backup exists yet)"
else
    echo "error: cannot determine the original dxgi.dll (no backup, and the installed one" >&2
    echo "       is the shim).  Restore it before installing." >&2
    exit 1
fi

cp -f "$build/dxgi.dll" "$sys64/dxgi.dll"
echo "installed dxgi.dll"

# the probes live in system32 too, so they can be run by name from anywhere; they are
# console programs and change nothing about the prefix.  --uninstall removes them.
for p in dxgi-shim-probe factory-layout-probe; do
    if [[ -f $build/$p.exe ]]; then
        cp -f "$build/$p.exe" "$sys64/$p.exe"
        echo "installed $p.exe"
    fi
done

wine_run reg add "$ovk" /v '*dxgi' /t REG_SZ /d 'native,builtin' /f >/dev/null
echo "set *dxgi = native,builtin"
restart_wineserver

echo
echo "Verify with:"
echo "  WINEPREFIX=$prefix wine 'C:\\windows\\system32\\displays-probe.exe'   # expect outputs(displays)>=1"
echo "  WINEPREFIX=$prefix wine '<path to the GPU probe helper exe from config/apps.local.env>'"

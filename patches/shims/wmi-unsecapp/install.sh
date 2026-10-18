#!/usr/bin/env bash
# install.sh -- install (or remove) the Wine "Unsecured Apartment" WMI shim
#               in the suite's Wine prefix.
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
#   1. copies the 64-bit build/*.{dll,exe} into
#      drive_c/windows/system32/wine-shims/ and the 32-bit build/win32/* ones
#      into drive_c/windows/syswow64/wine-shims/
#   2. writes the COM registration for CLSID {49bd2028-1523-11d1-ad79-00c04fd8fdff}
#      in both registry views: InprocServer32 + ThreadingModel, LocalServer32,
#      AppID + DllSurrogate
#   3. registers the proxy/stub (build/wmi-unsecapp-ps.dll): the proxy/stub CLSID
#      InprocServer32 in both views, and
#      Interface\{iid}\ProxyStubClsid32 for IUnsecuredApartment,
#      IWbemUnsecuredApartment and IWbemObjectSink
#   4. kills the running wineserver so the next app sees the new state
#
# Why both views: the vendor's WMI-using background services are 32-bit
# processes.  A 32-bit client resolves CLSID and
# Interface keys through Wow6432Node and loads the InprocServer32 named there
# in its own bitness, so it needs its own copy of the DLLs.  The CLSIDs are
# the same GUIDs in both views; only the paths differ.  (This is exactly how a
# real proxy/stub DLL is registered on Windows.)
#
# Everything it writes is listed by `./install.sh --show` and removed by
# `./install.sh --uninstall`; nothing outside the two shim directories and the
# CLSID/AppID/Interface subtrees is touched.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build="$here/build"
if [[ -z ${PREFIX:-} ]]; then
    cfg="$here/../../../config/apps.local.env"
    [[ -f $cfg ]] && . "$cfg"
fi
prefix=${PREFIX:-${WINEPREFIX:-$HOME/.local/share/wineprefixes/suite}}

clsid='{49bd2028-1523-11d1-ad79-00c04fd8fdff}'
ps_clsid='{6f9b0f2a-6a6a-4bf1-9c0d-3aa1b4f0c001}'
iids=('{1cfaba8c-1523-11d1-ad79-00c04fd8fdff}'
      '{31739d04-3471-4cf4-9a7c-57a44ae71956}'
      '{7c857801-7381-11cf-884d-00aa004b2e24}')

shimdir64_win='C:\windows\system32\wine-shims'
shimdir64_unix="$prefix/drive_c/windows/system32/wine-shims"
shimdir32_win='C:\windows\syswow64\wine-shims'
shimdir32_unix="$prefix/drive_c/windows/syswow64/wine-shims"

dll_name=wmi-unsecapp.dll
exe_name=wmi-unsecapp-srv.exe
probe_name=wmi-unsecapp-probe.exe
ps_name=wmi-unsecapp-ps.dll

# 64-bit registrations live directly under Software\Classes; 32-bit ones under
# Software\Classes\Wow6432Node.  reg.exe is 64-bit here, so writing the
# Wow6432Node path explicitly lands in the 32-bit view.
cls64='HKLM\Software\Classes\CLSID'
cls32='HKLM\Software\Classes\Wow6432Node\CLSID'
ifc64='HKLM\Software\Classes\Interface'
ifc32='HKLM\Software\Classes\Wow6432Node\Interface'
appid='HKLM\Software\Classes\AppID'

wine_run() { WINEPREFIX="$prefix" wine "$@"; }

[[ -d $prefix ]] || { echo "error: no such prefix: $prefix" >&2; exit 1; }

case ${1:-} in
--show)
    echo "prefix:     $prefix"
    echo "64-bit dir: $shimdir64_unix"
    echo "32-bit dir: $shimdir32_unix"
    echo "registry:"
    echo "  $cls64\\$clsid                        (default) + InprocServer32/ThreadingModel + LocalServer32 + AppID"
    echo "  $cls32\\$clsid                        same, 32-bit paths"
    echo "  $appid\\$clsid                         (default) + DllSurrogate"
    echo "  $cls64\\$ps_clsid\\InprocServer32      proxy/stub (64-bit)"
    echo "  $cls32\\$ps_clsid\\InprocServer32      proxy/stub (32-bit)"
    for i in "${iids[@]}"; do
        echo "  $ifc64\\$i\\ProxyStubClsid32"
        echo "  $ifc32\\$i\\ProxyStubClsid32"
    done
    exit 0
    ;;
--uninstall)
    set +e
    for k in "$cls64\\$clsid" "$cls32\\$clsid" "$appid\\$clsid" \
             "$cls64\\$ps_clsid" "$cls32\\$ps_clsid"; do
        wine_run reg delete "$k" /f
    done
    for i in "${iids[@]}"; do
        wine_run reg delete "$ifc64\\$i" /f
        wine_run reg delete "$ifc32\\$i" /f
    done
    set -e
    rm -f "$shimdir64_unix/$dll_name" "$shimdir64_unix/$exe_name" \
          "$shimdir64_unix/$probe_name" "$shimdir64_unix/$ps_name"
    rm -f "$shimdir32_unix/$dll_name" "$shimdir32_unix/$exe_name" \
          "$shimdir32_unix/$probe_name" "$shimdir32_unix/$ps_name"
    rmdir "$shimdir64_unix" "$shimdir32_unix" 2>/dev/null || true
    WINEPREFIX="$prefix" wineserver -k 2>/dev/null || true
    echo "uninstalled (registry subtrees + both wine-shims directories removed)"
    exit 0
    ;;
--install|"")
    ;;
*)
    echo "usage: $0 [--install|--uninstall|--show]" >&2
    exit 2
    ;;
esac

for f in "$build/$dll_name" "$build/$exe_name" "$build/$ps_name" \
         "$build/win32/$dll_name" "$build/win32/$exe_name" "$build/win32/$ps_name"; do
    [[ -f $f ]] || { echo "error: $f missing -- run ./build.sh and ./build32.sh first" >&2; exit 1; }
done

echo "==> installing into $prefix"
mkdir -p "$shimdir64_unix" "$shimdir32_unix"
install -m 0644 "$build/$dll_name"        "$shimdir64_unix/$dll_name"
install -m 0755 "$build/$exe_name"        "$shimdir64_unix/$exe_name"
install -m 0644 "$build/$ps_name"         "$shimdir64_unix/$ps_name"
[[ -f $build/$probe_name ]] && install -m 0755 "$build/$probe_name" "$shimdir64_unix/$probe_name"
install -m 0644 "$build/win32/$dll_name"  "$shimdir32_unix/$dll_name"
install -m 0755 "$build/win32/$exe_name"  "$shimdir32_unix/$exe_name"
install -m 0644 "$build/win32/$ps_name"   "$shimdir32_unix/$ps_name"
[[ -f $build/win32/$probe_name ]] && install -m 0755 "$build/win32/$probe_name" "$shimdir32_unix/$probe_name"

# `reg add` runs under Wine; the HKLM paths above land in the prefix's
# system.reg, i.e. machine-wide, exactly where COM looks for them, and easy to
# drop again with `reg delete`.
for bit in 64 32; do
    if [[ $bit == 64 ]]; then
        cls=$cls64; dir=$shimdir64_win; dll="$shimdir64_win\\$dll_name"; exe="$shimdir64_win\\$exe_name"
    else
        cls=$cls32; dir=$shimdir32_win; dll="$shimdir32_win\\$dll_name"; exe="$shimdir32_win\\$exe_name"
    fi
    echo "==> registering CLSID $clsid ($bit-bit)"
    wine_run reg add "$cls\\$clsid" /ve /d "WMI Unsecured Apartment (Wine shim)" /f >/dev/null
    wine_run reg add "$cls\\$clsid\\InprocServer32" /ve /d "$dll" /f >/dev/null
    wine_run reg add "$cls\\$clsid\\InprocServer32" /v ThreadingModel /d "Both" /f >/dev/null
    wine_run reg add "$cls\\$clsid\\LocalServer32" /ve /d "$exe" /f >/dev/null
    wine_run reg add "$cls\\$clsid\\AppID" /ve /d "$clsid" /f >/dev/null

    echo "==> registering proxy/stub $ps_clsid ($bit-bit)"
    wine_run reg add "$cls\\$ps_clsid" /ve /d "WMI Unsecured Apartment proxy/stub (Wine shim)" /f >/dev/null
    wine_run reg add "$cls\\$ps_clsid\\InprocServer32" /ve /d "$dir\\$ps_name" /f >/dev/null
    wine_run reg add "$cls\\$ps_clsid\\InprocServer32" /v ThreadingModel /d "Both" /f >/dev/null
done

wine_run reg add "$appid\\$clsid" /ve /d "WMI Unsecured Apartment (Wine shim)" /f >/dev/null
# Empty DllSurrogate == "use the system surrogate (dllhost.exe)".  Belt and
# braces: Wine only consults this if LocalServer32 fails to start.
wine_run reg add "$appid\\$clsid" /v DllSurrogate /d "" /f >/dev/null

# Wine's CoGetPSClsid (dlls/ole32/compobj.c) opens
#   HKCR\Interface\{iid}\ProxyStubClsid32
# as a *subkey* and reads its DEFAULT value, unlike Windows which uses a value
# of that name under HKCR\Interface\{iid}.  Wine's own built-in entries (e.g.
# IRundown {00000134-...}) are stored the same way -- checked with
# `wine reg query`.  Both forms are written so the shim keeps working if Wine
# switches to the documented layout.
echo "==> registering interface proxy/stub mappings"
for i in "${iids[@]}"; do
    wine_run reg add "$ifc64\\$i\\ProxyStubClsid32" /ve /d "$ps_clsid" /f >/dev/null
    wine_run reg add "$ifc64\\$i" /v ProxyStubClsid32 /d "$ps_clsid" /f >/dev/null
    wine_run reg add "$ifc32\\$i\\ProxyStubClsid32" /ve /d "$ps_clsid" /f >/dev/null
    wine_run reg add "$ifc32\\$i" /v ProxyStubClsid32 /d "$ps_clsid" /f >/dev/null
done

echo "==> restarting wineserver"
WINEPREFIX="$prefix" wineserver -k 2>/dev/null || true
sleep 1

echo
echo "installed.  verify with:"
echo "  WINEPREFIX=$prefix wine '$shimdir64_win\\$probe_name'      # 64-bit"
echo "  WINEPREFIX=$prefix wine '$shimdir32_win\\$probe_name'      # 32-bit (if built)"

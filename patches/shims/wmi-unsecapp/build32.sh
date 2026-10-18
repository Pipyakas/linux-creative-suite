#!/usr/bin/env bash
# build32.sh -- 32-bit (WoW64) counterpart of build.sh + build-ps.sh.
#
# The vendor's WMI-using background services are
# 32-bit processes.  A 32-bit client resolves CLSID and Interface keys in the
# Wow6432Node view and loads the InprocServer32 named there *in its own
# bitness*, so the shim needs 32-bit binaries registered separately.  The
# CLSIDs are the same GUIDs in both views; only the DLL/EXE paths differ.
#
#   ./build32.sh          build into ./build/win32
#   ./build32.sh clean    remove ./build/win32
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out="$here/build/win32"
gen="$out/ps"
idl="$here/wmi_unsecapp_ps.idl"
cc=${CC32:-i686-w64-mingw32-gcc}

if [[ ${1:-} == clean ]]; then rm -rf "$out"; echo "cleaned $out"; exit 0; fi

command -v "$cc" >/dev/null || { echo "error: $cc not found (install gcc-mingw-w64-i686)" >&2; exit 1; }
command -v widl >/dev/null || { echo "error: widl not found (install wine-tools)" >&2; exit 1; }

mkdir -p "$gen"

common=(-O2 -s -Wall -Wextra -Wno-unused-parameter -Wno-discarded-qualifiers
        -D_WIN32_WINNT=0x0601 -DWINVER=0x0601)

echo "==> wmi-unsecapp.dll (win32)"
"$cc" "${common[@]}" -shared -o "$out/wmi-unsecapp.dll" "$here/wmi-unsecapp.c" \
    -Wl,--kill-at -lole32 -loleaut32 -luuid -luser32 -ladvapi32

echo "==> wmi-unsecapp-srv.exe (win32)"
"$cc" "${common[@]}" -municode -o "$out/wmi-unsecapp-srv.exe" "$here/wmi-unsecapp-srv.c" \
    -lole32 -loleaut32 -luuid -luser32 -ladvapi32

echo "==> wmi-unsecapp-probe.exe (win32)"
"$cc" "${common[@]}" -o "$out/wmi-unsecapp-probe.exe" "$here/wmi-unsecapp-probe.c" \
    -lole32 -luuid -luser32 -ladvapi32

echo "==> widl (win32)"
widl -m32 -Oif -h -o "$gen/wmi_unsecapp_ps.h"       "$idl"
widl -m32 -Oif -p -o "$gen/wmi_unsecapp_ps_p.c"     "$idl"
widl -m32 -Oif -s -o "$gen/wmi_unsecapp_ps_stub.c"  "$idl"
widl -m32 -Oif -u -o "$gen/wmi_unsecapp_ps_i.c"     "$idl"
widl --dlldata-only -o "$gen/dlldata.c"             "$idl"

echo "==> wmi-unsecapp-ps.dll (win32)"
"$cc" "${common[@]}" -I"$gen" -shared -o "$out/wmi-unsecapp-ps.dll" \
    "$here/wmi-unsecapp-ps.c" \
    "$gen/wmi_unsecapp_ps_p.c" \
    "$gen/wmi_unsecapp_ps_stub.c" \
    -Wl,--kill-at -lrpcrt4 -lole32 -luuid

echo
echo "built (win32):"
ls -l "$out"

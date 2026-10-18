#!/usr/bin/env bash
# build.sh -- build the Wine "Unsecured Apartment" WMI shim.
#
#   ./build.sh            build into ./build
#   ./build.sh clean      remove ./build
#
# Produces (64-bit, x86_64-w64-mingw32):
#   build/wmi-unsecapp.dll          in-proc COM server   (InprocServer32)
#   build/wmi-unsecapp-srv.exe      local COM server     (LocalServer32)
#   build/wmi-unsecapp-probe.exe    test client that mimics the raster app's call
#
# install.sh copies these into the prefix and writes the registry keys.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out="$here/build"
cc=${CC:-x86_64-w64-mingw32-gcc}

if [[ ${1:-} == clean ]]; then
    rm -rf "$out"
    echo "cleaned $out"
    exit 0
fi

command -v "$cc" >/dev/null || {
    echo "error: $cc not found (install mingw-w64-gcc)" >&2
    exit 1
}

mkdir -p "$out"

# -O2 -s                      small, stripped
# -municode                   wWinMain entry point for the EXE
# -Wl,--kill-at               export stdcall entry points undecorated
#                             (DllGetClassObject, not DllGetClassObject@12)
common=(-O2 -s -Wall -Wextra -Wno-unused-parameter
        # mingw declares the interface vtables as const, the COM structs we
        # write are not; harmless, and silencing it keeps the build quiet.
        -Wno-discarded-qualifiers
        -D_WIN32_WINNT=0x0601 -DWINVER=0x0601)

echo "==> wmi-unsecapp.dll"
"$cc" "${common[@]}" -shared -o "$out/wmi-unsecapp.dll" \
    "$here/wmi-unsecapp.c" \
    -Wl,--kill-at \
    -lole32 -loleaut32 -luuid -luser32 -ladvapi32

echo "==> wmi-unsecapp-srv.exe"
"$cc" "${common[@]}" -municode -o "$out/wmi-unsecapp-srv.exe" \
    "$here/wmi-unsecapp-srv.c" \
    -lole32 -loleaut32 -luuid -luser32 -ladvapi32

echo "==> wmi-unsecapp-probe.exe"
"$cc" "${common[@]}" -o "$out/wmi-unsecapp-probe.exe" \
    "$here/wmi-unsecapp-probe.c" \
    -lole32 -luuid -luser32 -ladvapi32

echo
echo "built:"
ls -l "$out"
echo
echo "exports of wmi-unsecapp.dll:"
objdump -p "$out/wmi-unsecapp.dll" | awk '/Export Address Table/,/^$/' | head -20

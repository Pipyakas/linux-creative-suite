#!/usr/bin/env bash
# build.sh -- build the native DXCore shim for Wine (both architectures).
#
#   ./build.sh            build into ./build (64-bit) and ./build/win32 (32-bit)
#   ./build.sh clean      remove ./build
#
# Produces:
#   build/dxcore.dll           64-bit  (x86_64-w64-mingw32-gcc) -> system32
#   build/dxcore-probe.exe     64-bit
#   build/win32/dxcore.dll     32-bit  (i686-w64-mingw32-gcc)   -> syswow64
#   build/win32/dxcore-probe.exe 32-bit
#
# Toolchain flags mirror ../wmi-unsecapp/build.sh (proven on this machine).

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out="$here/build"

if [[ ${1:-} == clean ]]; then
    rm -rf "$out"
    echo "cleaned $out"
    exit 0
fi

# -O2 -s                      small, stripped
# -Wl,--kill-at               export stdcall entry points undecorated
#                             (DXCoreCreateAdapterFactory, not ...Factory@8)
# -Wno-discarded-qualifiers   mingw declares interface vtables const; the COM
#                             structs we fill in are not.  Same as wmi-unsecapp.
# -I/usr/include/wine/windows NOT passed on purpose: the Wine DXCore headers
#                             are included by absolute path, so their
#                             #include <ole2.h> resolves to mingw's headers
#                             instead of dragging in Wine's whole include tree.
common=(-O2 -s -Wall -Wextra -Wno-unused-parameter -Wno-discarded-qualifiers
        -D_WIN32_WINNT=0x0601 -DWINVER=0x0601)

build_one() { # build_one <cc> <outdir>
    local cc=$1 dir=$2
    command -v "$cc" >/dev/null || {
        echo "error: $cc not found (install mingw-w64-gcc)" >&2
        exit 1
    }
    mkdir -p "$dir"

    echo "==> $dir/dxcore.dll  ($cc)"
    "$cc" "${common[@]}" -shared -o "$dir/dxcore.dll" \
        "$here/dxcore-shim.c" \
        -Wl,--kill-at \
        -lole32 -luuid -luser32 -lgdi32 -lkernel32

    echo "==> $dir/dxcore-probe.exe  ($cc)"
    "$cc" "${common[@]}" -o "$dir/dxcore-probe.exe" \
        "$here/dxcore-probe.c" \
        -lole32 -luuid -luser32 -lkernel32
}

build_one "${CC64:-x86_64-w64-mingw32-gcc}" "$out"
build_one "${CC32:-i686-w64-mingw32-gcc}"   "$out/win32"

echo
echo "built:"
ls -l "$out" "$out/win32"
echo
echo "exports of dxcore.dll (64-bit):"
objdump -p "$out/dxcore.dll" | awk '/\[Ordinal\/Name Pointer\]/,/^$/' | head -20
echo "exports of dxcore.dll (32-bit):"
objdump -p "$out/win32/dxcore.dll" | awk '/\[Ordinal\/Name Pointer\]/,/^$/' | head -20

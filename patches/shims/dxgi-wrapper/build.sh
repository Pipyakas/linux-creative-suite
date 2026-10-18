#!/usr/bin/env bash
# build.sh — build the DXGI wrapper for both bitnesses.
# Same toolchain/flags as the WMI and DXCore shims, which compile cleanly here.
set -eu
cd "$(dirname "$(readlink -f "$0")")"
out=build
mkdir -p "$out" "$out/win32"

CFLAGS='-O2 -Wall -Wextra -Wno-unused-parameter -std=gnu11'

x86_64-w64-mingw32-gcc $CFLAGS -shared -o "$out/dxgi-wrapper.dll" dxgi-wrapper.c \
    -Wl,--out-implib,"$out/libdxgi-wrapper.a" -lole32 -luuid
i686-w64-mingw32-gcc   $CFLAGS -shared -o "$out/win32/dxgi-wrapper.dll" dxgi-wrapper.c \
    -Wl,--out-implib,"$out/win32/libdxgi-wrapper.a" -lole32 -luuid

echo "built:"
ls -l "$out/dxgi-wrapper.dll" "$out/win32/dxgi-wrapper.dll"
echo
echo "exports:"
objdump -p "$out/dxgi-wrapper.dll" | awk '/\[Ordinal\/Name Pointer\] Table/,/^$/' | grep -oE 'CreateDXGIFactory[0-9]?|DXGIDeclareAdapterRemovalSupport|DXGIGetDebugInterface1' | sort -u
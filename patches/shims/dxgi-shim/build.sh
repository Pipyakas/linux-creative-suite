#!/usr/bin/env bash
# build.sh -- build the native DXGI shim (64-bit) into ./build
#
# C++ on purpose: the shim implements mingw's IDXGIFactory7 / IDXGIAdapter4 / IDXGIOutput6
# and lets the compiler lay out the vtables from the headers.  That removes the hand-written
# index table that made the previous C version place EnumAdapters at slot 3 instead of 7.
# See RESULTS.md: mingw's order was verified against Wine's IDL and against the installed
# DXVK binary slot by slot, so it is the ABI to compile against.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mkdir -p "$here/build"
CXX=${CXX:-x86_64-w64-mingw32-g++}
# -static and -fno-exceptions/-fno-rtti matter: without them the DLL imports
# libwinpthread-1.dll, and a dxgi.dll that fails to load takes every D3D app with it.
# A system DLL must depend on nothing but kernel32/user32.
"$CXX" -shared -O2 -std=c++17 -Wall -Wextra -Wno-unknown-pragmas \
    -fno-exceptions -fno-rtti \
    -o "$here/build/dxgi.dll" \
    "$here/dxgi-shim.cpp" "$here/dxgi-shim.def" \
    -Wl,--kill-at -static
echo "built $here/build/dxgi.dll ($(stat -c %s "$here/build/dxgi.dll") bytes)"

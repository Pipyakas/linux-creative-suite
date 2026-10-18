# DXVK 3.1.1 swapchain-maintenance1 test

## Verdict

No. Disabling the `m_hasSwapchainMaintenance1` code path (and
subsequently disabling only its `fenceInfo` chaining) did **not**
restore presentation. All self-built 3.1.1 variants stayed black;
only DXVK 2.4.1 turns the same repro green, so the bug is not
accounted for by this branch alone.

## Build recipe

Toolchain used: meson 1.12.1, ninja 1.13.2, mingw-w64 g++ 16.2.0,
Vulkan-Headers/SPIRV-Headers pulled by submodule.

Source tree (unchanged after this work): a DXVK checkout at release
commit `b1a1c99` (`v3.1.1`).

```sh
cd <dxvk-checkout>
git submodule update --init --recursive --depth 1
```

Unmodified build:
```sh
rm -rf ../dxvk-build-311-base
meson setup ../dxvk-build-311-base . --cross-file build-win64.txt --buildtype release
ninja -C ../dxvk-build-311-base -j4
```

Patched build #1 (force `m_hasSwapchainMaintenance1 = false`):
```sh
git apply <repo>/patches/dxvk/0001-force-disable-swapchain-maintenance1.patch
rm -rf ../dxvk-build-311-patched
meson setup ../dxvk-build-311-patched . --cross-file build-win64.txt --buildtype release
ninja -C ../dxvk-build-311-patched -j4
```

Patched build #2 (skip only `fenceInfo` chaining):
```sh
git checkout -- src/dxvk/dxvk_presenter.cpp
git apply <repo>/patches/dxvk/0002-skip-present-fence-chain-only.patch
rm -rf ../dxvk-build-311-fenceonly
meson setup ../dxvk-build-311-fenceonly . --cross-file build-win64.txt --buildtype release
ninja -C ../dxvk-build-311-fenceonly -j4
```

DLLs land at:
- `src/d3d11/d3d11.dll`
- `src/dxgi/dxgi.dll`
- `src/d3d10/d3d10core.dll`

Installed (throwaway prefix, never the real one), after setting
`*d3d10core`, `*d3d11`, `*dxgi = native`:
```sh
cp <builddir>/src/d3d11/d3d11.dll <builddir>/src/dxgi/dxgi.dll <builddir>/src/d3d10/d3d10core.dll \
  "$WINEPREFIX/drive_c/windows/system32/"
```

Tests (30 s render, then screenshot):
```sh
WINEPREFIX=<throwaway-prefix> WINEDEBUG=-all \
  setsid nohup wine 'C:\windows\system32\swapbuf-test.exe' >/tmp/run-<tag>.log 2>&1 &
sleep 25
gui.sh shot <tag>          # scripts/gui.sh in this repo
magick shots/<tag>.png -crop 880x555+243+105 +repage -alpha off \
  -colorspace Gray -threshold 8% -format '%[fx:mean]' info:
```

## Measured lit fractions

| build | DXVK version string | lit fraction |
|---|---|---|
| self-built 3.1.1 unmodified | `v3.1.1` | `0.100162` |
| build #1 force `m_hasSwapchainMaintenance1 = false` | `v3.1.1+` | `0.155147` |
| build #2 `fenceInfo` chaining disabled only | `v3.1.1+` | `0.000000` |
| unpacked 2.4.1 reference | `v2.4.1` | `0.999773` |

(The `+` denotes a dirty/git-derived build.)

A `gui.sh shot` capture without `>8 KiB` was never recorded; every row
above is a real non-blank full-screen capture cropped afterward.

## Conclusion

All three 3.1.1 measurements stay near zero while 2.4.1 is `~1.0`. Since
forcing the whole maintenance-1 path false did not move the value into
the green range, the Wine breakage is not explained by DXVK merely
declaring that feature (or its present-fence chain) on its own. Something
else in 3.1.1 (or the combination with this Wine/Mesa stack) is still
blocking presentation.

## Files

- `0001-force-disable-swapchain-maintenance1.patch`
- `0002-skip-present-fence-chain-only.patch`
- `dxvk-dxcore-adapter.patch` — optional adapter-side patch adding
  `IDXCoreAdapter(1)` and Wine's internal `IWineDXGIAdapter` to DXVK's
  DXGI adapter (see `patches/shims/dxgi-shim/` for the standalone shim
  version of the same idea)
- `pin-2.4.1.sh` — installs the pinned 2.4.1 release DLLs into a prefix

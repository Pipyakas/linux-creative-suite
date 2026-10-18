# vkd3d-proton capability override (D3D12 feature level 12_0)

`override.patch` makes vkd3d-proton report `D3D12_TILED_RESOURCES_TIER_2` even when the
Vulkan ICD exposes no sparse binding. The vendor's consumer apps only *query* feature
level 12_0 at device creation and never map tiled resources, so on hardware that fails the
genuine sparse-binding check this turns "device creation refused" into a working device.
Real tiled-resource operations are **not** backed by hardware residency here and may fail
if a caller tries them — that is the documented, intentional trade-off.

## Build recipe

Applied against a vkd3d-proton checkout (upstream sources are referenced, not vendored):

```sh
cd <vkd3d-proton-checkout>
git apply override.patch          # if starting from a clean checkout
meson setup build-release . --cross-file build-win64.txt --buildtype release --strip
ninja -C build-release -j4
# DLLs land at build-release/libs/d3d12/{d3d12.dll,d3d12core/d3d12core.dll}
```

## Install

Copy the built DLLs into `x64-override/` next to `install.sh`, then:

```sh
PREFIX=<wine-prefix> ./install.sh            # install (backups the current DLLs first)
PREFIX=<wine-prefix> ./install.sh --uninstall
```

`install.sh` reads the prefix from `$PREFIX`, else from `config/apps.local.env` at the repo
root, and registers `d3d12`/`d3d12core` as `native` under
`HKCU\Software\Wine\DllOverrides`.

## Release build, and a negative result

The build originally installed was a **debug** one — `buildtype = debug`, `debug = True`,
`optimization = 0` — which put a **142 MB** `d3d12core.dll` and a 699 KB `d3d12.dll` in the
prefix. An unoptimised 142 MB DLL is slow to load and slow to run, and the raster image
editor is the heaviest app here, so it was a reasonable suspect for a clean exit part-way
through device initialisation.

It is not the cause. Rebuilt with `--buildtype release --strip` and reinstalled:

| | debug (was) | release (now) |
|---|---|---|
| `d3d12.dll` | 699,094 B | **562,910 B** |
| `d3d12core.dll` | 142,216,867 B | **20,527,917 B** |

The GPU probe helper still passes on the release build (`# displays: 1`, `# devices: 1`,
`# attached displays: 1`), and the raster image editor still exits with the same
`SPL ~DefaultMemoryManagerImpl bytesAllocated = 0` and no window. Recorded so the next
person does not spend another build cycle on it.

The release build is kept anyway — it is 7x smaller, optimised, behaviourally identical,
and simply better to have installed.

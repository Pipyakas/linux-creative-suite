# linux-creative-suite

Notes, patches and tooling for running a commercial Windows creative suite (19 desktop apps —
raster, vector, layout, PDF, video, compositing, audio, 3D, photo cataloguing) under Wine on
Linux.

Product names are deliberately never used in this repository. Every app is referred to by a
codename (`raster`, `vector`, `nle`, `compositor`, …); see [docs/status.md](docs/status.md) for the
list with a one-line description of each.

## Status

18 of 19 apps start, render their full workspace and respond to input. One (`raster`) paints its
splash screen and then quits on its own before showing its main window; see
[docs/findings/apps/raster-startup-exit.md](docs/findings/apps/raster-startup-exit.md).

Opening to a workspace is not the same as every feature working. Real-workload benchmarking has
only just started; see [docs/benchmarks.md](docs/benchmarks.md).

Tested on: CachyOS, KDE Plasma 6 (Wayland, Xwayland), Wine 11.19, Mesa 26.2, Intel HD 620 + AMD
Radeon R5 M445, 1366x768.

## What made the difference

| Change | Why |
|---|---|
| Wine **X11** driver instead of the Wayland driver | the apps' embedded-browser panels never composite on the Wayland driver |
| DXVK pinned to **2.4.1** | 3.1.1 presents nothing for any Vulkan swapchain under Wine 11.19 |
| vkd3d-proton reporting **D3D12 feature level 12_0** | the hardware lacks sparse binding, so 11_1 is the real ceiling; two apps refuse to start below 12_0 |
| KDE screenshot portal selected explicitly | otherwise every screenshot is silently black, which invalidated early test results |
| Answering startup dialogs that never appear on screen | several apps "hang" behind modal windows that exist but are not composited |

Details: [docs/setup.md](docs/setup.md), [docs/findings/graphics/](docs/findings/graphics/).

## Layout

```
docs/       status, setup, automation, benchmarks, and per-topic findings
patches/    patches against Wine / DXVK / vkd3d-proton, small compatibility shims,
            a KWin script, desktop portal config
scripts/    install helper, GUI-automation harness, X11 window inspection tools
config/     apps.example.env — copy to apps.local.env and fill in your install paths
tools/      leakcheck.sh — pre-commit guard that rejects product/vendor names
```

## Getting started

1. Read [docs/setup.md](docs/setup.md).
2. `cp config/apps.example.env config/apps.local.env` and fill in the prefix and executable paths
   (the `.local` files are git-ignored).
3. Apply the graphics changes from `patches/` (DXVK pin, vkd3d-proton override), then use
   `scripts/unblock.sh <codename>` to launch an app and get it past its startup dialogs.

## Contributing

Never commit vendor binaries, installers, screenshots, logs, or anything copied out of the
installation directory. Install the guard before committing:

```sh
printf '#!/bin/sh\nexec tools/leakcheck.sh $(git diff --cached --name-only --diff-filter=ACMR)\n' > .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

`tools/leakcheck.sh` reads its term list from `config/forbidden-terms.local.txt`, which is
intentionally not part of the repository; create your own.

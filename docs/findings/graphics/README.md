# Graphics findings

## Index

- [DXVK present regression](dxvk-present-regression.md) — why the pinned release renders swapchains and the newer release does not.
- [Wayland versus X11 driver](wayland-vs-x11-driver.md) — embedded Chromium surfaces, Wine presentation experiments, and the working driver selection.
- [D3D12 feature level](d3d12-feature-level.md) — the hardware limit, the capability override, and its consequences.
- [DXGI and DXCore shims](dxgi-dxcore-shims.md) — adapter enumeration, the false output count, and shim status.
- [Input and pointer path](input-pointer-path.md) — why synthetic clicks must use the X11 pointer for Wine X11 windows.

## Executive summary

1. The measured host is CachyOS with KDE Plasma 6, Mesa 26.2.4, Wine 11.19, and a 1366×768 panel.
2. Run Wine through its X11 driver for embedded Chromium panels; Wine's Wayland driver leaves those panels blank.
3. Keep DXVK pinned to 2.4.1: the D3D11 swapchain repro reaches a lit fraction of 1.000.
4. DXVK 3.1.1 presents nothing in the same controlled repro: lit fraction 0.000.
5. DXGI sees two adapters and D3D11 creates feature-level 11_0 devices; a compute shader also returns the expected values.
6. Wine's bundled vkd3d tops out at feature level 11_1; the feature-level 12_0 override is available through vkd3d-proton.
7. The override lets puppet launch and paint, but reports tiled-resource support that this hardware does not provide.
8. The earlier zero-output result was a probe error; DXVK already reports one attached 1366×768 output with seven modes.
9. Synthetic clicks for Wine X11 windows must use the X11 pointer through xdotool; the uinput pointer is separate.
10. Screenshot capture depends on the portal setup documented in [setup.md](../../setup.md).

## Problem

Several independent graphics problems initially looked like one blank-window or empty-device failure. This folder separates swapchain presentation, embedded Chromium rendering, D3D12 capability reporting, adapter enumeration, and synthetic pointer delivery.

## What was measured

The controlled D3D11 swapchain test, DXGI and D3D12 probes, Wine driver comparison, and pointer checks are summarized in the linked reports. The measurements use a corrected screenshot path; the portal setup is documented in [setup.md](../../setup.md).

## What was tried

The investigations compared DXVK releases, built DXVK and Wine variants, implemented adapter shims, forced a vkd3d-proton capability, switched Wine's display driver, and compared the Wayland and X11 pointer paths. Each report records its own measurements and corrections.

## Outcome

The measured working configuration is Wine's X11 driver, DXVK 2.4.1, and the vkd3d-proton feature-level 12_0 capability override where needed. The override does not supply the missing tiled-resource hardware support.

## Open questions

The internal DXVK 3.1.1 regression mechanism remains unproven; disabling its maintenance1 path and present-fence chain did not fix it. The effect of unsupported tiled-resource operations under the override also remains to be characterized.

# Wayland versus X11 driver

## Problem

Two separate symptoms were initially grouped together: Wine's Wayland driver left embedded Chromium panels blank, and D3D swapchain content appeared black. Switching Wine to its X11 driver fixed the embedded Chromium panels. The DXVK version comparison separately showed that DXVK 3.1.1's swapchain failure persists under either Wine driver.

## What was measured

On KDE Plasma 6 Wayland with Wine 11.19, the embedded Chromium Home surface in nle measured 0.0000 lit pixels under Wine's Wayland driver. With Wine's X11 driver, DISPLAY set to the current Xwayland server, and a valid XAUTHORITY cookie, the same surface rendered fully: 0.9977 lit pixels in one capture and 0.999 in the later full-window check. Its startup compatibility report became a normal, clickable window, and the application resized to its actual 910×492 Home-screen size.

A nearby D3D11 green-clear swapchain repro also rendered under the X11 driver when using DXVK 2.4.1. With DXVK 3.1.1, the repro remained black under both the Wayland and X11 drivers. This separates the working embedded Chromium path from the DXVK version regression documented in [dxvk-present-regression.md](dxvk-present-regression.md).

The earlier Wine Wayland investigation observed Mesa attaching and committing buffers to a client subsurface and receiving presentation feedback while KWin still showed black. Native Vulkan presentation worked on the same session. That made the Wine/Mesa/KWin surface path look responsible before the 2.4.1 comparison changed the diagnosis.

## What was tried

| Attempt | Result |
|---|---|
| Select Wine's X11 driver and launch with the active Xwayland DISPLAY and XAUTHORITY | Embedded Chromium panels render; clicks and the startup report work |
| Keep DXVK 3.1.1 and switch between Wine Wayland and X11 drivers | Swapchain repro stays black on both |
| Set an opaque region on the Wayland client surface or flush after present | No change |
| Retry attaching an already-recorded client surface once its toplevel is visible | Subsurface attachment changed from zero to one; the frame remained black |
| Add an early child-surface commit | Triggered explicit-sync error 4 and tore down the Wine/compositor connection |
| Implement Direct2D device-context PushLayer and PopLayer | Built successfully; the headless readback showed that draws inside Wine's existing stub still reached the target, so this did not explain the black workspace |
| Translate present-side fences in Wine | A candidate patch was written, but it was not established as a fix; disabling the DXVK maintenance1 and fence-chain paths did not restore presentation |

The corresponding Wine changes are recorded at [client-surface attach retry](../../../patches/wine/0002-wayland-client-surface-mapped-gate.patch), [Direct2D layer implementation](../../../patches/wine/0001-d2d1-device-context-PushLayer-PopLayer.patch), and [present-fence translation candidate](../../../patches/wine/0003-win32u-vkQueuePresentKHR-present-fence-translation.patch). The opaque-region experiment is also under [Wine patches](../../../patches/wine/).

## Outcome

Use Wine's X11 driver for embedded Chromium panels, with the current Xwayland DISPLAY and XAUTHORITY values. Use DXVK 2.4.1 for D3D11 swapchain presentation. These address distinct paths: the X11 driver resolves the blank Chromium surface, while the DXVK version pin resolves the measured swapchain regression.

The layer implementation remains a correctness improvement, not a fix for the observed black rendering. The subsurface retry repairs a real attach omission but did not restore the image.

## Open questions

The exact reason Wine's Wayland driver rejects the embedded Chromium cross-process surface is not isolated here; the X11 driver is the demonstrated workaround. The DXVK 3.1.1 regression mechanism also remains open. The fence-translation candidate needs a direct, controlled test before it can be treated as a cause.

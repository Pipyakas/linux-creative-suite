# DXVK present regression

## Problem

Under Wine 11.19, a D3D11 swapchain created for a Win32 window is black with DXVK 3.1.1. This was first blamed on Wine's Wayland present path, surface composition, or the selected GPU. The controlled version comparison instead isolates the regression to DXVK 3.1.1 in this environment.

## What was measured

A minimal D3D11 program cleared its back buffer to green for 1,200 frames in a clean throwaway prefix on the same Wine 11.19, Mesa, KWin session, and display driver.

| Configuration | Lit fraction | Reading |
|---|---:|---|
| DXVK 3.1.1 | 0.000 | Black client area |
| DXVK 2.4.1 | 1.000 | Green client area |
| Wine's builtin wined3d | 1.000 | Green client area |
| DXVK 3.1.1 with the same buffer copied by StretchDIBits | 1.000 | Green client area |

Each capture was checked for a visible test window and non-empty image before measurement. The 2.4.1 result was repeated with and without the DXGI shim in a separate throwaway prefix; all 40 presents completed and the lit fraction remained 1.000.

A second build series used a cropped 880×555 capture and the same green-clear test. It measured 0.100162 for an unmodified self-built 3.1.1 build, 0.155147 with the full swapchain-maintenance1 path disabled, 0.000000 with only the present-fence chain disabled, and 0.999773 for the 2.4.1 reference. Those first three readings remain near-black in that harness; they should not be conflated with the separate full-client-area 0.000 / 1.000 comparison.

## What was tried

| Change | Measurement | Outcome |
|---|---:|---|
| Disable DXVK's swapchain-maintenance1 path | 0.155147 in the cropped test | Did not restore the green frame |
| Disable only the present-fence structure chain | 0.000000 in the cropped test | Did not restore presentation |
| Force the Intel ICD instead of the Radeon | Still black with 3.1.1 | Cross-GPU import was not the cause |
| Use the X11 driver instead of the Wayland driver | Still black with 3.1.1 | Driver switching alone did not repair this version |
| Copy the same back buffer through GDI | 1.000 | Rendering succeeded; the failing step was the DXVK present path |

Patch references: [maintenance1-path experiment](../../../patches/dxvk/0001-force-disable-swapchain-maintenance1.patch) and [present-fence-chain experiment](../../../patches/dxvk/0002-skip-present-fence-chain-only.patch).

DXVK 3.1.1 chains a VkSwapchainPresentFenceInfoKHR structure when maintenance1 is available; DXVK 2.4.1 has no equivalent path. Wine 11.19's present handling does not appear to translate that fence. This is a plausible mechanism, not a proven root cause: disabling the whole feature path and disabling just the fence chain both failed to make the test green.

## Outcome

Pin DXVK to 2.4.1. It is the only measured DXVK version here that presents the same swapchain test fully, while 3.1.1 remains black across display-driver, GPU, and present-option changes. The later Wine-side fence-translation theory remains unconfirmed and is not needed to justify the tested version pin.

## Open questions

The specific code change in DXVK 3.1.1 that breaks this Wine and Mesa combination is not identified. The maintenance1 and fence-chain experiments rule out those toggles as sufficient fixes, but do not rule out another interaction in the surrounding presenter code.

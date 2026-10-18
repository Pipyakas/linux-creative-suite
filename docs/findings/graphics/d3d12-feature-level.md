# D3D12 feature level 12_0

## Problem

The raster GPU probe requests D3D12 feature level 12_0. The available hardware and Mesa drivers do not expose the sparse-binding support that vkd3d-proton requires for genuine tier-2 tiled resources, so an unmodified vkd3d implementation cannot report that level.

## What was measured

The machine has an Intel HD 620 using ANV and an AMD Radeon R5 M445 using RADV. Both Vulkan ICDs reported sparseBinding=false.

Wine 11.19's bundled vkd3d explicitly disables tiled resources and caps the maximum feature level at 11_1. vkd3d-proton has tiled-resource support, but its capability check also returns no tiled-resource tier when sparseBinding and the related sparse features are absent. Its unmodified device probe therefore reports:

| Requested level | Result |
|---|---|
| 12_0 | E_INVALIDARG |
| 11_1 | S_OK |
| 11_0 | S_OK |

The adapter path and feature-level refusal are separate issues. With DXVK's unwrapped adapter, Wine's bundled vkd3d can return E_NOTIMPL before feature checks. With a compatible adapter shim, adapter matching succeeds and the remaining 12_0 request is rejected because the maximum is 11_1.

## What was tried

A capability-only change in vkd3d-proton makes its tiled-resource tier query return tier 2 even when the Vulkan sparse features are absent. That allows the reported maximum to rise to feature level 12_0. The patch is [override.patch](../../../patches/vkd3d-proton/override.patch).

The release build completed successfully. With the override installed, D3D12CreateDevice returned S_OK at 12_0 for HARDWARE, WARP, and REFERENCE requests. The GPU probe then reported one display and one device, with device version 12.0.

| Application codename | Observed result with the override |
|---|---|
| puppet | Launched and painted; measured 0.9870 lit pixels at 1338×738 |
| raster | Still exited without ever mapping its main window |

The override was also rebuilt as a stripped release build after an initial debug build. The release DLL shrank from 142,216,867 bytes to 20,527,917 bytes; the probe result was unchanged, and raster still exited without mapping a window.

## Outcome

The override is a capability report change. It lets callers that gate startup on feature level 12_0 proceed far enough to expose a working UI path, as measured for puppet. It does not enable sparse residency or make tiled-resource operations real. Operations that depend on reserved-resource mappings or tile mappings remain unsupported and can fail.

## Open questions

The apps' use of tiled-resource operations after device creation has not been characterized. The cause of raster's remaining no-window exit is unresolved and is not explained by this feature-level gate.

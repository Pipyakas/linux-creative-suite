# Wine patches

Patches applied to a Wine 11.19 source tree (upstream code is patched, never
vendored). Files:

- `0001-d2d1-device-context-PushLayer-PopLayer.patch` — implement
  `ID2D1DeviceContext::PushLayer`/`PopLayer` (notes below)
- `0002-wayland-client-surface-mapped-gate.patch` — gate the Wayland driver's
  client-surface mapping
- `0003-win32u-vkQueuePresentKHR-present-fence-translation.patch` — translate
  present-fence handles in the win32u `vkQueuePresentKHR` path
- `present-wayland-opaque-region.patch` — mark client swapchain surfaces fully
  opaque so KWin stops treating them as alpha-blended (black windows)
- `wayland-client-surface-attach-retry.patch` — commit the child surface state
  early and retry the subsurface attach once a visible top-level exists

## d2d1 PushLayer/PopLayer patch notes

### Change
Implemented `ID2D1DeviceContext::PushLayer`/`PopLayer` (and the
`ID2D1DeviceContext1` `D2D1_LAYER_PARAMETERS1` variant) in
`dlls/d2d1/device.c` instead of dropping draws silently.

- Added a `layer_stack` to `struct d2d_device_context` (d2d1_private.h) holding
  the saved previous target plus an optional temporary layer bitmap.
- `PushLayer`: for a command-list target, records the layer unchanged. For a
  bitmap target, allocates a temporary `struct d2d_bitmap` via
  `d2d_bitmap_create()` (dlls/d2d1/bitmap.c:452), clears it to transparent, and
  switches it in as `context->target`; saves the previous target on the stack.
- `PopLayer`: restores the previous target and composites the layer back with the
  existing `d2d_device_context_DrawBitmap` path (dlls/d2d1/device.c:1247 ->
  `d2d_device_context_draw_bitmap`, which builds a bitmap brush via
  `d2d_bitmap_brush_create` and `FillRectangle`).
- Release frees any outstanding layer surfaces.

### Build command
cd <wine-build-dir> && make -j2 -C dlls/d2d1

### Build output last lines
  ... d2d1.dll ... (PE link) ...
make[1]: Leaving directory '<wine-build-dir>'
make: Leaving directory '<wine-build-dir>/dlls/d2d1'

### Edge cases
- Degenerate (null/zero-area) clip: no switch; layer dropped, parent target retained.
- PopLayer without PushLayer: warning, no-op.
- Allocation failure: fail layer, keep drawing on parent target.
- Leaked layer on context release: surfaces freed.
- Command-list path: byte-for-byte unchanged.

### Deviations from the original plan
- Intermediate surface is allocated at the current target's full pixel size rather
  than the clip rectangle's exact size. The reason: Wine's drawing path maps
  coordinates directly into the target bitmap and has no per-surface origin
  offset; re-basing the transform would be fragile across Save/RestoreDrawingState
  and nested layers. Correctness (transform preservation) was prioritized over the
  smaller-clip memory optimization. Opacity, geometric mask and opacity brush in
  the layer parameters are accepted but not applied.
- Layer objects passed by callers are ignored (logged).

### Not verified (no display available)
- No GUI/minimal D2D test run, no video-editor render check, no screenshot.
- Blending/compositing visual correctness of nested/opacity layers unverified.

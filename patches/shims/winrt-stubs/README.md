# winrt-stubs

Minimal WinRT COM stubs so an app's plugin runtime can probe pen/ink and accessibility
settings under Wine without crashing on missing WinRT classes:

- `roapi.c` — `RoInitialize`/`RoGetActivationFactory` and friends (activation entry points)
- `wine-windows.ui-main.c`, `wine-windows.ui-classes.idl`, `wine-windows.ui.spec` — the
  `Windows.UI.*` activation class stubs
- `wine-accessibilitysettings.c` — `Windows.UIX.AccessibilitySettings` stub
- `wine-combase-actctx.c`, `wine-combase-string.c` — small combase helpers (activation
  context and HSTRING shims) the stubs need

Status: experimental. Sources kept for reference; the built test DLL is not committed.
The pen input path itself stays unimplemented — the stubs only answer the activation
queries so the caller falls back gracefully.

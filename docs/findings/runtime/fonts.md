# Font lookup and startup failures

## Problem

An early `puppet` startup investigation recorded a fail-fast inside the font engine. Missing Windows fallback families were a plausible cause, but the failure did not establish which font lookup returned an invalid value. The current audit shows `puppet` reaching its workspace with the required graphics configuration, so the font hypothesis is historical and unconfirmed.

## Measured

The inspected Wine prefix had 39 font entries and no Segoe UI, Tahoma, or Selawik. Wine's DirectWrite fallback path names Tahoma as a last-resort family. The captured startup stack placed the fail-fast at an offset inside the font engine and showed an app-side breakpoint exception (`0x80000003`); the exception text and the missing family, if any, were not captured.

Wine bug [59311](https://bugs.winehq.org/show_bug.cgi?id=59311) describes a different reported startup failure in the same app family: a device-list assertion in a media initializer. Its stack did not include the font engine, so it does not establish the cause of the local font-engine exception.

Wine 11.19's Direct2D source registers about 20 of roughly 65 built-in effects. Its DirectWrite source contains font fallback logic, and the upstream history includes changes to fallback ranges and unsupported color-font handling. These are runtime facts, not proof that a missing effect or font caused the observed startup failure.

## Tried

The investigation reviewed the installed font list, Wine's fallback source, and public Wine issue history. It did not add a substitute font, force builtin versus native DirectWrite, or run an A/B font test. Those tests were proposed but not performed. The later successful `puppet` audit did not report a font warning, but it also did not record the prefix's font set as an experimental variable.

The `png-forwarder` used for the separate tracking-plugin failure is not a font fix. No font-specific shim is present among the published runtime patches.

## Outcome

The missing fallback families are measured; their role in the historical crash is not. The current puppet UI works with the graphics capability configuration, and there is no evidence that adding fonts is necessary for the current startup path. See [suite status](../../status.md) and [the compositor and media-app findings](../apps/compositor-audio-encoder.md).

## Open questions

- Which font family or lookup produced the invalid result in the historical stack?
- Does the same failure reproduce with the current prefix and graphics configuration?
- Would adding a licensed Segoe UI-compatible substitute change the startup result?
- Do the newer Wine DirectWrite fallback changes reach the Wine 11.19 build used here?

# Relevant Wine upstream research

Research snapshot: most source and tracker checks were recorded on 2026-10-15. The entries below summarize those records rather than a live status refresh.

## Problem

Several app findings point at Wine gaps in XML parsing, font fallback, COM activation, graphics capability reporting, and media APIs. Some gaps were fixed in later Wine versions or carried only by Wine Staging; others remain tracked or have only local workarounds.

## Measured

| Component / issue | Wine version or tracker state in the source notes | Finding |
|---|---|---|
| Thread-pool timer API | Stub through Wine 11.16; implemented in Wine 11.17 and later, including the tested 11.19. [Bug 57980](https://bugs.winehq.org/show_bug.cgi?id=57980) is fixed. | Commit [`8fc5b439`](https://github.com/wine-mirror/wine/commit/8fc5b439ff6a19de46c794032cf89e9d96662f25) adds the export and forwards it to the ntdll thread-pool implementation. Wine Staging carried a backport from 11.10 through 11.16 and dropped it when upstream gained the implementation. |
| XML serializer and embedded declarations | Wine Staging 11.1 carried MR 10025; the MR is closed and unmerged upstream. | The patch adds XMLSerializer support and handles embedded XML declarations in a way native parsers accept. This is separate from the native 64-bit runtime load fault documented in [msxml3.md](msxml3.md), and no test connected it to the workspace parse report. [MR 10025](https://gitlab.winehq.org/wine/wine/-/merge_requests/10025). |
| Native XML runtime load | Plain Wine 11.19 and Wine Staging 11.19 both failed the isolated load probe. | The local causes are a missing same-architecture resource module and a high-address indexing fault in the native runtime. No upstream fix was found in the source notes. |
| WMI unsecured apartment | [Bug 30439](https://bugs.winehq.org/show_bug.cgi?id=30439) is the upstream reference for the missing COM class. | The local [shim](../../../patches/shims/wmi-unsecapp/) handles activation and marshaling for the tested interfaces; it does not implement WMI query behavior. |
| Empty device list | [Bug 59311](https://bugs.winehq.org/show_bug.cgi?id=59311) was unconfirmed in the source snapshot. | The reported puppet startup assertion is `!m_devices.empty()` in a media initializer. The current app audit reaches the UI with a graphics capability override, so the report does not explain the present result by itself. |
| Popup positioning | [Bug 59547](https://bugs.winehq.org/show_bug.cgi?id=59547) was unconfirmed. | The report concerns an unimplemented popup-window positioning API. It is relevant to menu behavior, not a measured blocker in this slice. |
| WinRT JSON | [Bug 56914](https://bugs.winehq.org/show_bug.cgi?id=56914) was new/open in the source snapshot; an earlier interface change had merged, while additional interfaces remained absent. | The local [WinRT stubs](../../../patches/shims/winrt-stubs/) answer selected activation requests only; pen input and the JSON interfaces are not implemented by that patch. |
| DirectWrite font fallback | Wine 11.19 source in `dlls/dwrite/layout.c` uses Tahoma as a last-resort family. | The font investigation also cites commits [`edd63f5`](https://github.com/wine-mirror/wine/commits/master/dlls/dwrite) and [`8153736`](https://github.com/wine-mirror/wine/commits/master/dlls/dwrite) for fallback-range and second-chance handling, plus [`742dd37`](https://github.com/wine-mirror/wine/commit/742dd37) for rejecting unsupported COLRv1 fonts. The notes do not establish which changes are present in the tested package. |
| Direct2D effects | Wine 11.19 source inspection counted about 20 built-in effects out of roughly 65. | Missing effects remain a possible app-specific gap. Earlier audio startup bugs on `d2d_device_context_Flush` and `d2d_geometry_group_GetBounds` are recorded as fixed; they do not establish the cause of any current failure. |
| Media Foundation | Wine 11.19 export inspection: `mfplat` 120 implemented / 64 stubs, `mf` 35 / 51, `mfreadwrite` 9 implemented exports. | Hardware-device enumeration and several media APIs are absent. The umbrella media-source issue [49692](https://bugs.winehq.org/show_bug.cgi?id=49692) is fixed; [bug 60326](https://bugs.winehq.org/show_bug.cgi?id=60326) tracks an open regression, [MR 7570](https://gitlab.winehq.org/wine/wine/-/merge_requests/7570) covers encoder creation, and MR 8172 covers media-type output. |
| Embedded-browser blank pages | [Bugs 59994](https://bugs.winehq.org/show_bug.cgi?id=59994) and [59995](https://bugs.winehq.org/show_bug.cgi?id=59995) are the historical sign-in-page references in the source notes. | The local measurement supports Wine's X11 driver through Xwayland for embedded-browser rendering; the exact PDF overlay cause remains open. See [graphics findings](../graphics/wayland-vs-x11-driver.md). |

## Tried

The upstream review compared Wine 11.19 export tables and source files, checked the cited WineHQ issue and merge-request records, and compared plain Wine with Wine Staging for the isolated native XML load. It also compared local prefix behavior with the dedicated shims and driver experiments. No Wine source tree was patched as part of these findings.

Graphics adapter and display-path work is documented separately in [graphics findings](../graphics/README.md). The current local experiments are indexed in [known issues](known-issues.md).

## Outcome

The required thread-pool timer API is available in Wine 11.17+, so it is no longer a startup blocker on Wine 11.19. Wine Staging's XML compatibility changes are distinct from the native runtime load failure. The WMI and selected WinRT shims cover only their measured activation paths. Font fallback, Direct2D effect coverage, Media Foundation completeness, and the reported empty-device assertion remain areas where Wine behavior can differ from the Windows implementation.

## Open questions

- Will MR 10025 or equivalent XML serializer behavior be accepted upstream?
- Can the native XML runtime's high-address fault be reduced to a Wine compatibility report without distributing a modified runtime?
- Which DirectWrite fallback changes are present in the tested Wine 11.19 package?
- Are the missing Direct2D effects or Media Foundation exports exercised by any current app workflow?
- Which open tracker items still reproduce on the current machine and should be updated with a minimal test case?

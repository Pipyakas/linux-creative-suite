# Open issues

This table consolidates the unresolved findings from the current app and runtime reports. Tracker statuses reflect the research snapshots in the source notes, mostly from 2026-10-15; they were not refreshed live while writing this summary. Graphics mechanisms are summarized in the [graphics findings index](../graphics/README.md).

| Area | Current evidence | Status / open point |
|---|---|---|
| `raster` startup | Main window exists but stays unmapped; the app exits cleanly. The virtual-desktop splash paints at 0.958, but the app still exits about 110 seconds after launch. | Root gate unknown. The tested GPU preference, input, forced map, OpenCL visibility, and browser switches do not change the result. See [startup report](../apps/raster-startup-exit.md). |
| `pdf` embedded-browser overlay | The overlay remains blank above a working document interface; KWin parks it. The matcher must include `dialog=true`. Wine's X11 driver is the measured fix for embedded-browser rendering generally. | Workaround measured; exact overlay cause and whether the X11 fix paints this specific page remain open. See [panel report](../apps/pdf-html-panel.md) and [script](../../../patches/kwin/html-panel-park/). |
| `compositor` startup dialogs | Three unseen modal dialogs were found beneath a static splash. Raising windows and sending Return/Escape reached a 0.971-lit workspace. A later audit found a plugin warning and dialogs as late as about 90 seconds. | App works after dialogs are answered; cause and timing remain unexplained. |
| Tracking plugin | Dependencies were supplied and a [PNG forwarder](../../../patches/shims/png-forwarder/) was added; the plugin entry point still returned error 317. | Plugin remains unavailable; it is not needed for the main workspace. |
| `audio` recovery flow | A later audit reached a 0.9981-lit interface after answering crash recovery and closing the Learn panel. An earlier error-report dialog was observed in a different run. | Interface works; relationship between the two dialogs is unknown. |
| `encoder` / `puppet` workspace parse report | An early report described a properties-list error and termination. The wording “from memory” refers to an in-memory list; no matching disk config file was found. The latest audit shows both apps reaching their interfaces. | Early report is not reproduced in the current status; no file, parser defect, or causal link to the native XML runtime has been proved. See [app report](../apps/compositor-audio-encoder.md). |
| Font lookup | The prefix had 39 fonts and lacked Segoe UI, Tahoma, and Selawik. A historical `puppet` fail-fast occurred inside the font engine, but no failed family lookup was captured; the current app works with its graphics configuration. | Missing fallback fonts are a hypothesis, not an established cause. See [font report](fonts.md). |
| Native 64-bit XML runtime | The native runtime fails Wine 11.19 initialization for a missing matching resource module and a separate high-address table-index fault. Per-app builtin overrides pass isolated selection checks for `encoder` and `puppet`. | Prefix workaround is in use; no upstream runtime fix was identified. See [runtime report](msxml3.md). |
| WMI unsecured apartment | The [shim](../../../patches/shims/wmi-unsecapp/) fixes activation and cross-process marshaling for two background services. `raster` never requests the class. | Service path is fixed locally; the shim does not implement WMI query semantics. [Wine bug 30439](https://bugs.winehq.org/show_bug.cgi?id=30439) remains the upstream reference. |
| Media Foundation | Wine 11.19 exposes partial media APIs and lacks hardware-device enumeration used by some media paths. | Software decode and incomplete media I/O remain runtime limits; no current UI failure here is directly attributed to them. See [Wine research](wine-upstream-research.md). |
| Direct2D effects | Wine 11.19 registers about 20 of roughly 65 built-in effects. Older audio startup failures on two Direct2D calls are fixed in Wine 11.3, but missing effects remain a possible app-specific limit. | No current audio startup failure has been tied to a missing effect. |
| Device-list assertion | [Wine bug 59311](https://bugs.winehq.org/show_bug.cgi?id=59311) records a startup assertion on an empty device list for the puppet animator. The current audit reaches its UI with a graphics capability override. | Upstream report remains unconfirmed; relationship to the current successful configuration is unknown. |
| Popup positioning | [Wine bug 59547](https://bugs.winehq.org/show_bug.cgi?id=59547) reports an unimplemented popup-position API affecting a vector app. | Open upstream issue, not observed as a current blocker in this slice. |
| Media JSON interfaces | [Wine bug 56914](https://bugs.winehq.org/show_bug.cgi?id=56914) records missing WinRT JSON interfaces for a video editor. | Open tracker item; not measured in the current app audit. Experimental [WinRT stubs](../../../patches/shims/winrt-stubs/) only answer selected activation queries and do not implement the APIs. |

## Local experiment map

- Adapter reporting: [DXGI shim](../../../patches/shims/dxgi-shim/), [DXCore shim](../../../patches/shims/dxcore-shim/), and [DXGI wrapper](../../../patches/shims/dxgi-wrapper/). The wrapper is not usable with DXVK under its current module-name constraint; the other experiments did not resolve the `raster` exit.
- Missing PNG module: [PNG forwarder](../../../patches/shims/png-forwarder/).
- WMI activation and marshaling: [WMI shim](../../../patches/shims/wmi-unsecapp/).
- Selected WinRT activation: [WinRT stubs](../../../patches/shims/winrt-stubs/).

## Open questions

- Which measured app failures still reproduce after the current status pass, and which were transient setup states?
- Which runtime gaps can be reduced to a minimal Wine test case and an upstream report?
- Does any current media workflow fail because of Media Foundation or Direct2D, rather than merely running with reduced capability?

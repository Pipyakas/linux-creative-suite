# PDF editor HTML panel

## Problem

The `pdf` editor starts its main interface, but a separate captionless embedded-browser window remains above it with a “Loading…” overlay. The document controls and page view exist underneath; the overlay is the part that fails to paint.

## Measured

The overlay is approximately 800×738 pixels and is reported by KWin as `dialog=true`. Browser cache activity is written during launch, which shows that the browser runtime starts; the visible page remains blank. The current suite audit records the main interface at a lit fraction of 0.9983 after the overlay is parked. The PDF pages, toolbar, and menus were observed working in the final KWin-script run.

The driver comparison in [Wayland versus X11](../graphics/wayland-vs-x11-driver.md) measured an embedded-browser Home surface at 0.0000 lit pixels with Wine's Wayland driver and 0.9977–0.999 with Wine's X11 driver through Xwayland, using the active `DISPLAY` and `XAUTHORITY`. That comparison is strong evidence for the driver fix, although it used a different app surface.

## Tried

The KWin script at [html-panel-park](../../../patches/kwin/html-panel-park/) moves the stuck overlay off-screen when it maps. Its first matcher required `dialog=false`; that silently excluded the target because KWin reports `dialog=true`. The matcher was corrected to include dialog windows. This matters because the overlay is otherwise absent from the main document surface's usable area.

The Wine X11 driver is the measured rendering fix for embedded-browser surfaces. Keeping `DISPLAY` and the current Xwayland authority cookie available makes Wine select the X11 path; with the Wayland driver, the browser surface stays blank. The compositor parking script is useful with or without restarting KWin.

Other candidate causes from the research notes included the modern viewer setting, DirectComposition swapchain support, browser command-line switches, protected mode, and licensing state. None was established as the cause of this exact overlay. They are not needed for the demonstrated parking workaround.

## Outcome

The document interface is usable after the script parks the overlay, and the X11 driver is the demonstrated way to make embedded-browser content render. The parking script fixes the obstruction; it does not explain why this page fails to composite. See [suite status](../../status.md).

## Open questions

- Which embedded-browser surface owns the overlay: the Home view, sign-in workflow, or another panel?
- Does the X11-driver fix make this exact overlay paint, or does it only keep the underlying document surface usable?
- Is the blank page caused by browser composition, page loading, or the app's licensing workflow? The available browser-cache and helper logs do not isolate that choice.

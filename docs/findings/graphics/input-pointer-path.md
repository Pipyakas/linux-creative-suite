# Input and pointer path

## Problem

Early tests concluded that embedded Chromium panels ignored synthetic clicks and might require a physical mouse. That conclusion was caused by sending pointer motion through uinput on the Wayland side while the target windows were Wine X11-driver windows.

## What was measured

The Wayland-side pointer driven by ydotool and the X11 pointer read by xdotool are separate and scale independently. After ydotool moved to 300,300, the X11 pointer reported 600,600; after a move to 900,200, the X11 pointer reported 1365,400.

A missing or stale XAUTHORITY cookie also made xdotool fail silently in the old harness. Empty location results were treated as zero, so the movement loop sent clicks to unrelated screen coordinates.

## What was tried

| Input method | Result |
|---|---|
| ydotool mouse movement through uinput | Moves the Wayland-side pointer, not the pointer read by Wine X11 windows |
| xdotool click --window | Does not activate the embedded panel; this targets the wrong client |
| xdotool mousemove followed by xdotool click 1 | Works for Wine X11 windows |
| ydotool key | Keyboard input continues to reach Wine |
| kmove and kclick | Reserved for compositor-owned windows |

With DISPLAY and the current XAUTHORITY exported, xdotool mousemove plus click opened nle's New Project dialog, created a project, and reached the full editor workspace. The same method opened puppet's Import file dialog. No physical mouse was needed in either verified case.

## Outcome

For synthetic pointer input to Wine X11-driver windows, use the X11 pointer through xdotool. Use the compositor pointer helpers for compositor-owned windows, and refresh XAUTHORITY after login because Xwayland creates a new cookie.

The earlier “embedded panels ignore synthetic input” conclusion is retracted. The panels were live and accepted clicks once the input reached the correct pointer.

## Open questions

The working path is verified for nle and puppet. Other applications that use embedded Chromium panels should be checked individually, although the earlier failures no longer support a general input restriction.

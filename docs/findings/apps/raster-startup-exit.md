# Raster startup exit

## Problem

`raster` initializes, creates its main window, and then exits cleanly without showing that window. Earlier reports that it created no window were an observation error: the window was present in the X tree but unmapped, so KWin and the screenshot harness did not list it.

## Measured

On the normal X11 embedding path, one launch created a 1181×608 main window at (44, 52) with `map_state=0`. A separate 782×532 splash was mapped for about 0.383 seconds, then destroyed. The main window remained present and unmapped; the process exited with status 0 at about 21 seconds after printing `SPL ~DefaultMemoryManagerImpl bytesAllocated = 0`.

Window geometry varied with startup progress. One later direct-path sample was 1×1 at (0, 0); in the virtual-desktop run the main window was 1189×668 and unmapped. In that desktop, the 782×532 splash painted at a lit fraction of 0.958 and stayed visually identical from 10 through 100 seconds. The app then destroyed its windows and process tree about 110 seconds after launch. A normal X11 capture never showed the splash contents.

The GPU probe helper reports multiple platforms when invoked with the app's arguments:

| Platform | Devices | Displays attached | Result |
|---|---:|---:|---|
| Native graphics | 1 Radeon device, feature level 12.0 | 1 | Passes |
| OpenCL through rusticl | 1 Intel device | 0 | Unattached |

Hiding OpenCL entirely changes that platform to zero devices and zero displays, but the app exits on the same schedule. Selecting the Radeon through the Vulkan loader does not attach the OpenCL device to a display. The helper therefore reports a mixed, multi-platform result; its unattached OpenCL platform is not the exit gate by itself.

## Tried

The following hypotheses were tested and did not change the exit:

- GPU preference disabled, both on the normal path and in the virtual desktop.
- A run after moving the Settings folder aside also exited with the same final allocator message; the preference-folder state is not sufficient to explain the exit.
- Input versus no input: repeated Return/Escape presses and a run with no keys behaved identically.
- Forcing the main window to map: `XMapWindow` succeeded, changing map state from unmapped to mapped, but the app still exited on schedule.
- Waiting for an answerable modal: repeated key presses did not keep the process alive. The compositor app's invisible startup modals were a useful analogy, but the same technique did not help `raster`.
- Chromium switches `--disable-gpu`, `--in-process-gpu`, and a combined disabled-features attempt.
- Hiding the OpenCL platform and selecting different Vulkan devices.
- Fabricating an EDID registry value to provide physical display dimensions. The helper still reported zero physical size; Wine removed the value during monitor re-enumeration at the next server restart.
- Treating the repeated `NtQueryInformationProcess` calls as an anti-debug check. A working app made more of the same calls.
- Treating the IPC broker's medium-integrity warning as fatal. The broker remained running and registered event queues after the warning.
- Clearing licensing and profile caches and creating the suspected Vulcan registry key; neither changed the exit.
- WMI activation: the trace contained zero requests for the unsecured-apartment class. Installing or omitting its shim did not change this app's result.
- XML runtime errors: the relevant launch trace had no such errors and the builtin runtime was already selected.
- The earlier zero-device diagnosis. The helper's native platform now reports an attached display and device; the old conclusion was based on truncated helper output and a broken window observer.

The helper must be run with its app-supplied arguments to see every platform. A no-argument run inspected only the beginning of its output and missed the OpenCL platform. The window-list observer only sees mapped windows; `XGetImage` on the Xwayland root also returned invalid pixel data, so neither is evidence about this app's rendering. A raw X-tree walk is the reliable window-presence measurement.

The project keeps GPU enumeration experiments in [the DXGI shim](../../../patches/shims/dxgi-shim/), [the DXCore shim](../../../patches/shims/dxcore-shim/), and [the DXGI wrapper](../../../patches/shims/dxgi-wrapper/). They address adapter reporting and do not explain the present clean exit. See also [graphics findings](../graphics/README.md) and [suite status](../../status.md).

## Outcome

The corrected finding is that `raster` creates a correctly sized main window but never maps it, paints a short-lived splash on the normal path, and then self-quits. In a Wine virtual desktop the splash paints clearly, but the main window still never maps and the app still exits later. Mapping, GPU preferences, input, the tested OpenCL configuration, and the tested Chromium switches are not the cause. The gate remains unidentified.

## Open questions

- What internal decision causes the clean shutdown between initialization and the first mapped main window?
- Does the medium-integrity warning from the IPC broker affect the app's trust decision even though the broker continues running?
- Do the two unimplemented pointer/input APIs or the transactional registry-open API appear in a call trace before shutdown? Their presence in a module load log alone does not show that they are called.
- Would isolating the plugin directory change the shutdown? This was proposed as a cheap test but was not performed.
- Can a targeted process/API trace capture the app's final shutdown path without relying on the inaccessible internal UI?

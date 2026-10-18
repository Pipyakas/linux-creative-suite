# Where each app stands

Summary of the suite on this machine: **18 of 19 apps reach their full workspace**; one
(codename `raster`) paints only its splash. Ten of the 19 have had *input* verified as well as
rendering. No app has been shown to be un-operable — see "What is still open" at the end.

The last full pass ran on 2026-10-17: each app was launched, its startup modals were answered,
and the **lit fraction of its client area** was measured from a screenshot (alpha off,
grayscale, 8 % threshold, title bar / menu bar / taskbar skipped). Every verdict below has a
screenshot behind it. The harness and the per-app dismissal recipes are in
[automation.md](automation.md); the environment that got here is in [setup.md](setup.md).

How to read the two verdict columns:

* **reaches workspace** — the app's own main UI is painted (not a splash, not a modal), with no
  on-screen startup dialog left.
* **interaction verified** — a synthetic action was *observed* to change the UI (a menu opened,
  a dialog appeared, or the workspace was reached by clicking). "no" does not mean inert: it
  means the fixed-coordinate menu test was not valid for that app (home-screen-only surfaces
  have no menu bar at the tested spot), or the test ran while the app's own startup modals were
  still up. Those rows are unverified, not failed.

| codename | descriptor | reaches workspace | interaction verified | startup dialogs to answer | notes |
|---|---|---|---|---|---|
| `raster` | raster image editor | **no — splash only** | no (no window to drive) | none reachable | See "The one exception" below. |
| `vector` | vector illustration app | yes (0.9990) | yes | none | File menu opens. |
| `layout` | page-layout app | yes (0.9997) | yes | none | File menu opens. |
| `copyedit` | copy-editing companion to `layout` | yes (1.0000) | yes | none | File menu opens. |
| `pdf` | PDF editor | yes (0.9983) | no (menu test inconclusive) | the stuck embedded-browser overlay | Overlay is captionless and reported as a dialog; park it off-screen. |
| `nle` | non-linear video editor | yes (0.9989) | **yes** (plus a full drive to the editor workspace) | System Compatibility Report | Click "continue with known issues" at rel (0.79, 0.90); do **not** send keys afterwards. |
| `nle-lite` | lightweight video editor | yes (0.7016) | yes | none in the final pass | Home screen rendered; File menu opens. |
| `compositor` | motion-graphics compositor | yes (0.8657) | no (menu test ran while its modals were up) | a plugin warning, then late modals | Modals appear up to ~90 s in; keep polling. Project + Tip of the Day visible. |
| `compositor-cli` | compositor command-line renderer | n/a (no UI) | n/a | n/a | Hangs after GPU init under Wine — see [benchmarks.md](benchmarks.md). |
| `audio` | audio editor | yes (0.9981) | yes | crash-recovery prompt → Learn panel | "Later" at rel (0.927, 0.80), then the Learn panel's corner close. |
| `encoder` | batch encoder | yes (0.4421, dark UI) | yes | none | Its UI is simply dark; the number is not a defect. |
| `puppet` | puppet/performance animator | yes (0.9870) | yes | none | File menu opens; "Import puppet…" opens a real file dialog. Needed the D3D12 override. |
| `photo-catalog` | photo cataloguing/RAW app | yes (0.8000, dark UI) | no (menu test was blocked by its own dialogs) | "What's New" panel | Close its X at inset (34, 34) from the top-right. |
| `asset-browser` | asset browser | yes (0.9043) | yes | none | File menu opens. |
| `web-editor` | web authoring app | yes (0.9976) | yes | none | File menu opens. |
| `scene3d` | 3D scene composer | yes (0.9965) | no (menu test hit an in-app error dialog) | occasionally a render-engine error | The error ("unable to start rendering engine … will now close") is a real in-app failure, not a dismissal problem; the final pass reached the Home screen. |
| `mat-designer` | node-based material designer | yes (0.9949) | no (no menu bar on its home surface) | none | Node graph rendered. |
| `tex-painter` | 3D texture painter | yes (0.9977) | no (menu test inconclusive) | "Configuration Issues" | That window is reported as *not* a dialog, so a dialog filter misses it: click its bottom-right action at (0.90, 0.92). |
| `mat-sampler` | material-from-photo sampler | yes (0.9925) | no (no menu bar on its home surface) | none in the final pass | Its GPU-below-minimum prompt can be pre-answered with `ignoreGPUWarning=true` in its user settings file. |
| `stager` | 3D staging/render app | yes (0.9965) | no (menu test hit an in-app error dialog) | none in the final pass | Scene viewport rendered. |

Descriptors match [config/apps.example.env](../config/apps.example.env); the exe paths and
window classes per codename live in the local, git-ignored copy of that file. Reproduce any row
with `scripts/unblock.sh <codename>` (how it works: [automation.md](automation.md)).

## The one exception: `raster`

`raster` starts, initialises fully and then exits with **rc=0, no window and no error log**.

* Outside a virtual desktop, no window is ever mapped: the process creates a complete main
  window (1189x668, correct class and title) and never maps it, so the compositor never manages
  it and every window-listing tool reports nothing. A splash is mapped for ~0.4 s.
* Inside a Wine virtual desktop (`wine explorer /desktop=<name>,1366x738 …`) the app **does**
  paint its splash — measured **0.958 lit**, frozen for 10–100 s — and then quits ~110 s in.
  So the process can paint; it is the main window that never appears.
* Ruled out in desktop mode: input (Return/Escape vs none identical) and the GPU gate
  (`UseGraphicsProcessor 0` identical).
* The vendor's own GPU probe helper passes on this prefix (`# displays: 1`, `# devices: 1`,
  D3D12 device version 12.0), so the earlier "zero GPU devices" explanation no longer holds for
  this app; the remaining failure is not the D3D12 feature level and is **not identified**.

An earlier round of verdicts called this app "no window because the GPU probe reports zero
displays". That was wrong twice over: the probe's zero-display output was itself an artefact,
and the main window does get created — it is simply never mapped.

## What is still open

1. **`raster` never maps its main window.** Reproduce with the virtual-desktop trick in
   [automation.md](automation.md); the app quits on its own ~110 s in.
2. **Interaction is unverified for 8 of the 18 reachable apps** (`pdf`, `compositor`,
   `photo-catalog`, `scene3d`, `mat-designer`, `tex-painter`, `mat-sampler`, `stager`). The
   fixed-coordinate menu test is only valid where a menu bar exists at that coordinate; these
   apps open on home screens or were still covered by their own modals when the test ran. A
   per-app click map (home-screen buttons) would settle them.
3. **`compositor` renders but the output file is unverified**, and its command-line renderer
   hangs — details and next steps in [benchmarks.md](benchmarks.md).
4. **`scene3d` and `stager` each showed an in-app "rendering engine" error in one pass** and
   reached their home screens in another. It is unclear whether that error is transient or
   triggered by a specific action (opening a document); worth one dedicated run.

## What earlier passes got wrong (kept so the numbers are trusted correctly)

* **A window is not a UI.** `nle` opened a real window with a live menu bar and a 100 % black
  client area for hours; every early "it works" was window presence, not pixels.
* **"The vendor's Chromium/panel surfaces ignore synthetic input" was an instrument error.**
  The pointer path was wrong: Wine's X11-driver windows read the X11 pointer, and the tools in
  use drove a different, Wayland-side pointer. See [automation.md](automation.md) — the claim is
  retracted, the surfaces are operable.
* **Modal dialogs were never answered** in the first pass, so several apps were judged by their
  splash or their compatibility prompt.
* **Dark apps look black.** `encoder` measured 0.0000 in one pass and a complete, working UI in
  the next.
* **Every lit pixel measured during the broken screenshot period was invalid.** The screenshot
  portal was returning valid, entirely black PNGs while exiting 0 (see [setup.md](setup.md)),
  and any row measured then has been re-measured.
* **`outputs(displays)=0` was a probe artefact**, not a fact about this machine: stock
  DXGI reports one attached display at 1366x768 before any of this work.
* **`pdf`'s low first scores were the parked overlay**, not the app: the embedded-browser panel
  covers the document window until it is parked, and the parking script originally skipped it
  because it required "not a dialog".

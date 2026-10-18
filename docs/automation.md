# Automation: driving these apps under KDE Wayland + Xwayland

How to drive the suite for testing: launch an app, get past its startup modals, click and type
into it, take trustworthy screenshots, and decide whether it reached its workspace.

The low-level helpers live in **`scripts/gui.sh`** (pointer, keyboard, typing, screenshots,
compositor window list) and the per-app driver in **`scripts/unblock.sh`** (launch, answer
modals, verdict). Everything below is what those scripts do and why — including the lessons that
cost the most time.

## 1. The one rule that matters: use the X11 pointer

The prefix runs on **Wine's X11 driver**, so every app window is an X11 client inside Xwayland
and reads its input from the **X11 pointer**.

* `xdotool` injects through XTEST *inside Xwayland*. That is the right pointer.
* `ydotool` drives a **different** pointer: it goes through uinput on the Wayland/compositor
  side. The two are decoupled — measured: after `ydotool mousemove --absolute 300 300`, Xwayland
  still reported the X11 pointer at 0,0. So **ydotool clicks reach the compositor's own windows
  and silently do nothing to the apps.**
* Keyboard is different: `ydotool`/uinput keys *do* reach Wine. That asymmetry is why dialogs
  were dismissable with Escape for a long time while every mouse click silently failed.
* `xdotool click --window <id>` targets the wrong client and does nothing; plain
  `xdotool mousemove X Y` + `xdotool click 1` is what works. Posting `WM_LBUTTONDOWN`/`UP` from
  inside the prefix did not work either, but it is moot now.

**This is the single most expensive mistake this project made.** It produced the conclusions
"the vendor's Chromium/panel surfaces ignore synthetic input", "this button cannot be activated
by any method" and "this app needs a human with a physical mouse". All three were wrong and are
retracted. With `xdotool mousemove` + `click 1`, a puppet-app "Import puppet…" button opens its
real file dialog on the first try, and the video editor's Home → New Project → Create → Skip
path reaches the full editor workspace.

Two practical notes:

* The first time anything grabs uinput, KDE shows a modal consent dialog — *"xdotool is asking to
  control input devices"*. It steals **all** pointer and keyboard input while it is up, and it is
  chicken-and-egg for a pointer (ydotool cannot click the dialog that would grant ydotool
  permission). Answer it once with the keyboard: Space to tick *always allow*, then Enter.
* `gui.sh` keeps `kmove`/`kclick` (the uinput pointer) only for targets that really are
  compositor windows — the compositor's own menus and that consent dialog.

## 2. Environment the helpers set

`gui.sh` exports, for itself:

```bash
DISPLAY (default :0)          XDG_RUNTIME_DIR
WAYLAND_DISPLAY (wayland-0)   DBUS_SESSION_BUS_ADDRESS
XAUTHORITY                    YDOTOOL_SOCKET
```

* **`XAUTHORITY` is not optional.** Xwayland mints a **new auth cookie at every login**; without
  it `xdotool` cannot connect at all and every pointer operation fails *silently*. The helper
  picks the newest `/run/user/$UID/xauth_*`.
* It starts `ydotoold` if it is not running.
* **The app needs `DISPLAY` too.** A non-interactive shell starts without it, and a helper that
  exports it only for itself leaves the apps it launches with no display at all — they exit
  silently. That produced a full run of **seven spurious "no window" results**. Export
  `DISPLAY`/`XAUTHORITY` *before* launching Wine, not just for the helper.

## 3. `scripts/gui.sh` command reference

| command | what it does |
|---|---|
| `focus` | raise and focus the largest non-dialog Wine window (via a KWin script) |
| `move X Y` | move the X11 pointer (xdotool) |
| `click X Y [right\|double\|slow]` | move + click with the X11 pointer; `slow` holds the button 0.25 s |
| `drag X1 Y1 X2 Y2` | press, move in 8 steps, release |
| `kmove X Y`, `kclick X Y` | the uinput pointer, for compositor windows only |
| `key COMBO...` | key chord(s) through uinput, e.g. `key ctrl+n`, `key escape`, `key alt+f` |
| `type 'text'` | type text through uinput with a 30 ms delay |
| `shot NAME [WxH+X+Y]` | full-screen screenshot through the desktop portal, optional crop |
| `windows` | the compositor's window list: `caption\|class\|dialog=<bool>\|WxH+X+Y` |

Two details inside `windows` and `shot` are worth knowing:

* The window list is implemented by loading a small KWin script that `print()`s one line per
  window with a per-run tag, then reading the lines back out of the user journal with that tag.
  Field 4 is the geometry, formatted `WxH+X+Y`.
* **Parse the window list with `awk -F'|'`.** Without the field separator, `$4` is the whole
  line and every area computation silently yields 0 — a real measurement bug that made dialogs
  look like they were not there.
* `shot` is the only reliable capture path on this desktop: `ffmpeg -f x11grab` and ImageMagick
  `import` return black frames under Wayland. `shot` nudges the pointer first (a DPMS-slept
  display captures black), kills a stale screenshot process (a timeout-killed one keeps the
  portal's grab busy and blanks **every** later capture), allows **45 s** per attempt — the
  first capture after the portal starts is slow — and **exits 3 on a blank capture**.
  A blank frame must never be recorded as "0 lit pixels"; that mistake briefly suggested the
  prefix itself had broken D3D rendering.

Screenshots depend on the desktop portal being healthy. If captures come back blank or short,
check the portal (fix and verification in [setup.md](setup.md)) and do **not** restart the
compositor: that advice was wrong and destroys every running app.

## 4. Launching and killing apps

```bash
WINEPREFIX=$PREFIX WINEDEBUG=-all setsid nohup wine '<windows path>' \
  >"$LOG_DIR/<label>.log" 2>&1 </dev/null &
```

* Launch detached (`setsid nohup`) so the app survives the helper's exit.
* Poll the window list every 15 s, up to 300 s, for the first window of the expected class.
  Nothing there by then means the app never opened a window.
* To stop everything: kill the app processes, then `WINEPREFIX=$PREFIX timeout 40 wineserver -k`
  and wait for `wineserver` to disappear. **Exclude the harness's own process lines** (shell,
  editor, `awk`) from the sweep — the obvious `ps | awk | xargs kill` pattern otherwise kills
  the harness itself.
* When an app never maps its window, **a Wine virtual desktop gives it a visible surface**:

  ```bash
  wine explorer /desktop=<name>,1366x738 '<windows path>'
  ```

  This is how `raster` was shown to paint a splash (0.958 lit, frozen 10–100 s) while its real
  main window is created and never mapped.

## 5. Answering startup modals

The hard cases share three properties:

1. they are often **captionless** — the compositor reports them as dialogs with an empty caption;
2. they often **do not composite** — they are on the window list but not on the screen;
3. they appear **late** — up to ~90 s after the splash.

The dismissal loop that works, repeated over several minutes:

* **Raise every window the app owns, in reverse order, then send Return and then Escape.** The app
  decides which control advances; the harness is not selecting any button, which is also why it
  is safe — no destructive control is ever clicked.
  Raising in *reverse* order matters: the window list returns the main window before the modal,
  so activating in order leaves the main window focused last and the keys go to it. This is
  exactly why one editor's compatibility report survived every pass while a single
  caption-targeted Escape dismissed it.
* **Match captions only when they are non-empty.** `caption.includes("")` matches *every*
  window, so the loop raises the desktop and the keys go to the compositor instead of the modal.
  A "raise this caption and answer it" helper must be a **no-op** unless a window with that exact
  caption exists — the keys it sends are global, and sending them with nothing matching is how a
  working home surface was destroyed (next bullet).
* **Keep polling after the screen looks settled.** The compositor app raises a captionless
  warning ~90 s in, after its splash; a loop that breaks when no dialog is visible leaves the app
  covered forever.
* **Gate the key-spam: stop sending keys once no dismissable dialog is left.** Measured cost of
  ignoring this: after the video editor's compatibility report was gone, further Return/Escape
  reached its captionless full-bleed home surface and **destroyed it** — lit 0.9989 → 0.0274 and
  the window vanished, which read as "the app is black" for a day.
* **Close-clicking** is for dialogs the keyboard does not dismiss:
  * size guard: skip anything larger than **70 % of the screen** in either dimension — the
    editor's captionless home surface is 93 % of its main window, so it can never be a
    close-click candidate;
  * on-screen guard: skip anything parked off-screen (`x ≥ 1500`);
  * a **non-empty caption** is one (not the only) reason to consider a dialog close-clickable: a
    captionless small dialog is legitimate too — one audio app's "Learn" panel is captionless and
    its close button is only reachable by a corner click;
  * **try several plausible corner offsets and check after each click**, because they differ per
    dialog: an inset of (34, 34) is exact for one app's "What's New" panel but 34 px low and
    14 px left for another app's panel, whose close sits in the very corner at (x+w−14, y+9).
* **Never click the top-right of a captionless full-bleed surface** — the same trap as the key
  spam above.

### Per-app dismissal recipes

| app | what to answer |
|---|---|
| `nle` | **System Compatibility Report**: click "continue with known issues" at relative (0.79, 0.90) of the dialog. Escape no longer dismisses it in the current build. **Then stop sending keys.** |
| `audio` | crash-recovery prompt: click "Later" at relative (0.927, 0.80) of the 494x126 prompt (always safe); then the captionless "Learn" panel needs its close button at the very corner (offsets like (14, 9), not (34, 34)). |
| `tex-painter` | **Configuration Issues** — reported as **not** a dialog, so any dialog-only filter misses it. Click its bottom-right action at relative (0.90, 0.92) ("continue anyway"); same offset works for its home-screen overlay's Close. |
| `photo-catalog` | "What's New" panel: close at inset (34, 34) from the window's top-right corner. |
| `pdf` | a captionless embedded-browser window (~808x768) that is stuck on "Loading…" and covers the document window. It is reported as a **dialog**, so the parking rule must not require "not a dialog". Park it at x=2600 instead of closing it; skip it when it is already parked (x > 2000). |
| `compositor` | a plugin warning at startup (a bundled third-party tracking plugin fails to load) plus captionless modals that keep appearing late. Let the Return/Escape loop keep polling. |

## 6. Deciding whether the app actually reached its workspace

Two gates, both needed:

* **Dialogs.** Count the app's *on-screen* dialogs: reported as a dialog, `x < 1500` (so a parked
  overlay does not count) and area below 70 % of the screen (so a full-bleed home surface does
  not count). Counting a parked overlay failed a working app once; counting a full-bleed home
  surface marked a fully working editor "still dialogged" at lit=0.999.
* **Pixels.** Compute the **lit fraction of the client area** from the screenshot: crop the app's
  window rectangle, skip the decoration (this theme: `y+38`, height −76), turn alpha off, convert
  to grayscale, threshold at 8 %, take the mean. Verdict: no on-screen dialogs + lit ≥ 0.5 →
  **workspace**; no dialogs but darker → **dark UI, no dialog** (often a normal dark theme:
  0.44 for the batch encoder, 0.80 for the photo catalogue, 0.30-ish passes for the audio app's
  splash are all fine); otherwise **still dialogged**.

Mean brightness alone is useless as a verdict: a pure black window measures 0.5 once alpha is
averaged in, and a correct dark app can measure 0.09. And a window is not a UI — the editor
opened a real window with a live, hover-highlighting menu bar over a 100 % black client area.
Check every verdict against the screenshot the harness saved at that moment.

### Calibrating "input worked"

A menu click counts as working when **a new window appeared** or **more than ~5000 pixels
changed**. Calibrated in the same run: the editor's menu produced 116749 changed pixels, the
audio app's 307624, while hover noise on a blocked app produced 36. Do not gate the verdict on
dialog counts alone, and run a known-good app first as a control before trusting any new
screenshot/diff code.

## 7. Typing into the apps

`gui.sh type` sends text through uinput with a 30 ms delay and is fine for ordinary fields. One
measured quirk: **typing a Windows path into a Wine file dialog can drop the leading `C:\`** if
the whole string is sent too fast — send it character by character with a delay, or avoid
synthetic typing for paths entirely (navigate the dialog, or set the path through the app's own
scripting interface, which is what [benchmarks.md](benchmarks.md) does).

## 8. Harness pitfalls, as a checklist

Each of these cost real time and is fixed in `scripts/gui.sh` / `scripts/unblock.sh`:

1. **Two decoupled pointers** — synthetic pointing must use the X11 pointer (xdotool). Keyboard
   via uinput works.
2. **Missing `DISPLAY` for the launched app** — the helper's own environment is not enough.
3. **`dialog=true` on captionless full-bleed surfaces** — "dialog" ≠ "modal"; verdict gating and
   close-clicks must both know it, and a stray Return/Escape on such a surface destroys it.
4. **`awk` without `-F'|'`** on the window list matches nothing and yields area 0.
5. **Early `break` in the dismissal loop** leaves late modals (up to ~90 s) undriven.
6. **Blank captures recorded as a measurement** — exit non-zero instead.
7. **Caption matching against an empty string** raises the desktop instead of the dialog.
8. **Raising windows in list order** focuses the main window last, so the modal never gets the
   keys.
9. **A wrong `kwinrc` enable key** makes a compositor-side helper silently never load
   (see [setup.md](setup.md)).

## 9. Quick recipes

```bash
scripts/gui.sh windows                 # who is on screen, with class and geometry
scripts/gui.sh click 104 200           # X11 pointer click
scripts/gui.sh key escape
scripts/gui.sh shot my-shot            # exits 3 on a blank frame
scripts/unblock.sh nle                 # one app: launch -> answer modals -> verdict + PNG
ROUNDS=20 scripts/unblock.sh compositor  # more dismissal rounds for the app whose modals are late
```

`unblock.sh` appends one row per run (`app`, class, verdict, lit, dialogs left, caption) and
writes a PNG per app; the accumulated results are the table in [status.md](status.md).

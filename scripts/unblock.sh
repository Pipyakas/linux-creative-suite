#!/usr/bin/env bash
# unblock.sh — drive a suite app past its invisible startup modals, then report what
# it reached.  The app table comes from config/apps.local.env at the repo root: each
# APP_<codename> entry is '<windows path to the exe>|<exe file name>' (the exe file
# name is what KWin reports as the window's resource class); empty entries are
# skipped.  Labels are codenames.  Usage:
#
#   ./unblock.sh                 run every app configured in apps.local.env
#   ./unblock.sh raster nle      run only these codenames
#
# WHY THIS REPLACES dialogcheck.sh's dismissal logic
#
# dialogcheck.sh tried to be clever: identify each modal by comparing its area against
# the main window, focus it by matching frameGeometry, dismiss it, repeat. That
# approach kept failing for a specific reason, and the simple version below is the one
# that actually works.
#
# The apps that stall create modals that KWin reports as `dialog=true` with an
# **empty caption** and that **do not composite** — they are on the window list but not
# on the screen. So:
#
#   * There is no caption to match. `caption.includes("")` matches every window and
#     raises the desktop, so the keys go to the compositor instead of the modal.
#   * Geometry matching is fragile; frameGeometry drifts by the decoration offset.
#   * The modals appear LATE. The compositor app shows only its splash for ~90 s, so
#     any dismissal pass that runs earlier finds nothing and gives up, leaving the app
#     blocked forever.
#
# What works, and is what took the compositor app from "hung on the splash at
# Initializing media engine, bit-identical pixels for 7.5 minutes" to its fully
# rendered workspace:
#
#   repeat N times over several minutes:  raise EVERY window of the app, then send
#   Return and Escape. Keep going after the screen looks settled, because the next
#   modal appears later.
#
# The app itself decides whether to advance — we are not selecting any button, we are
# just answering whatever modal is in front of it. That is also why it is safe: no
# destructive control is ever clicked.
set -u
here=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
GUI="$here/gui.sh"
cfg="$here/../config/apps.local.env"
[[ -f $cfg ]] && . "$cfg"
PREFIX=${PREFIX:-${WINEPREFIX:-$HOME/.local/share/wineprefixes/suite}}
ROUNDS=${ROUNDS:-14}
TSV="$here/unblock.tsv"

export DISPLAY=${DISPLAY:-:0}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
export XAUTHORITY=${XAUTHORITY:-$(ls -t /run/user/$(id -u)/xauth_* 2>/dev/null | head -1)}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/user/$(id -u)/bus}
mkdir -p "$here/logs" "$here/shots"

kill_wine() {
  ps -eo pid,args --no-headers |
    awk '/zsh -c|claude|awk/ {next} /\.exe|wine-preloader|wineserver/ {print $1}' |
    xargs -r kill -9 2>/dev/null
  sleep 3
  WINEPREFIX=$PREFIX timeout 40 wineserver -k >/dev/null 2>&1
  for _ in 1 2 3 4 5 6 7 8; do pgrep -x wineserver >/dev/null || break; sleep 2; done
}

# Raise every window this app owns, then answer whatever modal is in front.
poke() {
  local class=$1 f
  f=$(mktemp --suffix=.js)
  # Raise in REVERSE order. windowList() returns the main window before the modal, so
  # activating in order leaves the main window focused last and the keys go to it
  # instead of to the modal — which is why the video editor's System Compatibility
  # Report survived every pass and the video editor measured lit=0.0135 BLACK, when a
  # single caption-targeted Escape dismisses it and the Home screen renders at 0.9989.
  printf 'var ws = workspace.windowList().filter(function(w){ return (w.resourceClass || "").toLowerCase().includes(%s); });\nfor (var i = ws.length - 1; i >= 0; i--) { ws[i].active = true; }\n' \
    "\"$(printf '%s' "$class" | tr '[:upper:]' '[:lower:]')\"" >"$f"
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$f" ub$$ >/dev/null 2>&1
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
  sleep 2
  "$GUI" key Return >/dev/null 2>&1; sleep 2
  "$GUI" key Escape >/dev/null 2>&1; sleep 2
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript ub$$ >/dev/null 2>&1
  rm -f "$f"
}

# The PDF editor's embedded Chromium opens a captionless window that stays on
# "Loading…" forever under Wine, covering the whole document window. Park it
# offscreen. Matched conservatively: the PDF app's resource class (from
# APP_pdf), empty caption, and the known stuck size ~808x768. KWin reports it as
# dialog=true, so a check that requires dialog=false would skip it — that is the bug
# that was in the KWin plugin (now fixed there too, for the next login).
park_cef_overlays() {
  local class=${EXE_NAME[pdf]:-} f
  [[ -n $class ]] || return 0
  f=$(mktemp --suffix=.js)
  JS_CLASS=$(printf '%s' "$class" | tr '[:upper:]' '[:lower:]')
  cat >"$f" <<JS
for (const w of workspace.windowList()) {
  if ((w.resourceClass || "").toLowerCase() !== "$JS_CLASS") continue;
  if (String(w.caption).length > 0) continue;
  const g = w.frameGeometry;
  if (!(Math.abs(g.width - 808) <= 40 && Math.abs(g.height - 768) <= 40)) continue;
  if (g.x > 2000) continue;
  w.frameGeometry = { x: 2600, y: 0, width: g.width, height: g.height };
  print("parked CEF overlay " + g.width + "x" + g.height);
}
JS
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$f" pk$$ >/dev/null 2>&1
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
  sleep 1
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript pk$$ >/dev/null 2>&1
  rm -f "$f"
}

# Click the X close button of every ON-SCREEN dialog that Return and Escape did not
# dismiss.
#
# Some overlays are not modal dialogs and ignore the keyboard: the photo cataloguing
# app opens a "What's New" panel over its Library module that only closes on its close
# button. Return and Escape leave it up, so the app is fully functional but permanently
# covered. Clicking a close button is safe — it is not a destructive control, and the
# app's own UI is behind it.
click_dialog_close() {
  local class=$1 line x y w h
  while read -r line; do
    [[ -n $line ]] || continue
    # Only click close on a REAL dialog. Two guards, both earned the hard way:
    #  * non-empty caption — the video editor's Home overlay (a vendor UI toolkit
    #    surface) is captionless, and clicking its top-right destroys the whole Home
    #    surface (measured: lit 0.9989 -> 0.0274 and the window vanishes), which is
    #    what read as "video editor BLACK" in the harness for a day.
    #  * much smaller than the main window — the video editor's CEF surface is
    #    1366x691, i.e. 93% of the main window, so it must never be a close-click
    #    candidate.
    # NOTE: a captionless dialog=true window is NOT auto-skipped: the audio app's
    # Learn panel is captionless (|dialog=true|900x700+233+20) and its X was only
    # reachable by corner-click. The size guard below is what keeps us off the video
    # editor's Home surface. A captioned small dialog and a captionless small dialog
    # are both legitimate close-click candidates.
    # geometry is field 4: WxH+X+Y
    w=$(echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[1]}')
    h=$(echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[2]}')
    x=$(echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[3]}')
    y=$(echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[4]}')
    # size guard: skip if bigger than 70% of the screen in either dimension (a CEF surface)
    (( w * h < 1366 * 738 * 70 / 100 )) || continue
    # only on-screen (the parked PDF-app CEF overlay sits at x=2600 and must be left alone)
    (( x < 1500 )) || continue
    # Offsets vary per dialog and guessing one is not enough — measured misses so far:
    #   (32,18) -> 27 px right of the photo catalogue app's "What's New" button, missed
    #   (34,34) -> exact for the photo catalogue app, but 34 px low and 14 px left for
    #              the audio app's Learn panel, whose X sits in the very corner at
    #              (x+w-14, y+9)
    # So try the plausible corners until the window is actually gone, checking after
    # each click.
    local ox oy
    for oxoy in "34 34" "14 9" "20 16" "44 44" "30 12"; do
      ox=${oxoy% *}; oy=${oxoy#* }
      "$GUI" click $((x + w - ox)) $((y + oy)) >/dev/null 2>&1
      sleep 2
      "$GUI" windows 2>/dev/null | grep -Fq "${w}x${h}+${x}+${y}" || break
    done
  done < <("$GUI" windows 2>/dev/null | grep -Fi "|$class|" | grep -Ei 'dialog=true')
}

# Dialogs that are still visible on screen.
#  * A parked overlay (x >= 1500) does not count: the PDF editor's CEF window keeps
#    its dialog=true flag after being moved offscreen, and counting it marked a
#    working PDF editor STILL-DIALOGGED.
#  * The captionless Home overlay (the video editor's is 1366x691, i.e. 93% of the
#    screen) also does not count: it is the app's UI, not a modal — same rule
#    any_small_dialog uses for poking. With it counted, the video editor's fully
#    working home read STILL-DIALOGGED while lit=0.999.
onscreen_dialogs() {
  "$GUI" windows 2>/dev/null | grep -Ei 'dialog=true' |
    awk -F'|' -v lim=$((1366 * 738 * 70 / 100)) '
      { split($4,a,/[^0-9]+/); if (a[3]+0 < 1500 && a[1]*a[2] < lim) print }' | wc -l
}

# Click a dialog's bottom-right action button.
#
# Two dialogs are neither modal nor keyboard-dismissable and are reported dialog=false,
# so the keyboard path never reaches them:
#   * The texture painting app's "Configuration Issues" (700x298, dialog=FALSE) — it
#     just sits there for the whole run. Answering "Continue anyway" is the documented
#     way past it: the texture painter runs on the Intel HD 620 because that is the GPU
#     the display is attached to.
#   * The texture painting app's "Home screen" overlay, whose Close button likewise
#     needs a click.
# Both put their single action button in the bottom-right. Measured offsets from the
# captured screenshots: "Continue anyway" at (632,273) inside 700x298 => (0.903, 0.916)
# of the dialog; "Close" at (534,365) inside 600x388 => (0.890, 0.941).
click_dialog_button() {
  local caption=$1 line w h x y
  line=$("$GUI" windows 2>/dev/null | grep -Fi "$caption" | head -1)
  [[ -n $line ]] || return 0
  w=$(echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[1]}')
  h=$(echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[2]}')
  x=$(echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[3]}')
  y=$(echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[4]}')
  (( w > 0 && h > 0 )) || return 0
  # Button position is per-dialog. Defaults (0.90, 0.92) fit the bottom-right action
  # button of the texture painter's "Configuration Issues" and "Home screen". The video
  # editor's "Continue with known issues" is measured (0.791, 0.899) of 575x526+207+103
  # — the generic point lands 62 px right, off the red button.
  local fx=90 fy=92
  case $caption in
    "System Compatibility Report") fx=79; fy=90 ;;   # "Continue with known issues"
  esac
  echo "    clicking '$caption' action at $((x + w * fx / 100)),$((y + h * fy / 100))" >&2
  "$GUI" click $((x + w * fx / 100)) $((y + h * fy / 100)) >/dev/null 2>&1
  sleep 4
}

# True (non-empty stdout) when the app still has a real small dialog on screen. Poking
# keys while none exists is exactly how the harness destroyed the video editor's Home
# overlay: once its System Compatibility Report was gone, further Return/Escape went to
# the captionless CEF home surface / main window, and by the shot it was menu-bar-only
# black. The video editor is fully usable (0.9989 by hand) — the harness must stop
# pressing keys once dismissable dialogs are gone.
any_small_dialog() {
  "$GUI" windows 2>/dev/null | grep -Fi "|$1|" | grep -Ei 'dialog=true' |
    awk -F'|' -v ma="$2" 'BEGIN{ if (ma <= 0) ma = 1366*738 }
      { n=split($4,a,/[^0-9]+/); area=a[1]*a[2]; if (area < ma*0.7) { print area; exit } }' |
    head -1
}

# Raise one window by its exact caption and answer it with Escape then Return. NO-OP
# unless a window with exactly that caption exists — the loadScript/start alone is
# harmless, but the Escape/Return below are GLOBAL, and sending them with no matching
# window is what closed the video editor's Home overlay after its compatibility report
# was already dismissed (measured: lit 0.9989 -> 0.0274, CEF window gone).
#
# The "System Compatibility Report" dialog sits over an unpainted client area, so
# leaving it up measures the video editor as lit=0.0135 BLACK even though it is
# perfectly usable — verified by hand the same day (Home -> New Project -> Create ->
# full workspace). Escape is the key that dismisses it; Return is not, because its
# default button is not the dismissing one.
dismiss_by_caption() {
  local caption=$1 f
  "$GUI" windows 2>/dev/null | grep -Fq "$caption" || return 0
  f=$(mktemp --suffix=.js)
  printf 'for (const w of workspace.windowList()) { if (w.caption === "%s") { w.active = true; } }\n' \
    "$caption" >"$f"
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$f" dc$$ >/dev/null 2>&1
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
  sleep 2
  "$GUI" key Escape >/dev/null 2>&1; sleep 3
  "$GUI" key Return  >/dev/null 2>&1; sleep 3
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript dc$$ >/dev/null 2>&1
  rm -f "$f"
}

run() {
  local label=$1 path=$2 class=$3
  local log="$here/logs/unblock-$label.log"
  echo "=== $label" >&2
  kill_wine
  WINEPREFIX=$PREFIX WINEDEBUG=-all setsid nohup wine "$path" >"$log" 2>&1 </dev/null &

  local elapsed=0 seen=''
  while (( elapsed < 300 )); do
    sleep 15; elapsed=$((elapsed + 15))
    seen=$("$GUI" windows 2>/dev/null | grep -Fi "|$class|" | head -1)
    [[ -n $seen ]] && break
  done
  if [[ -z $seen ]]; then
    printf '%s\t%s\tNO-WINDOW\t-\t-\n' "$label" "$class" >>"$TSV"
    kill_wine; return 0
  fi
  echo "    first window at ${elapsed}s: $seen" >&2

  # mainarea = area of the app's biggest window (prefer dialog=false; else biggest
  # overall). The compositor app marks ALL its windows dialog=true, so without such a
  # fallback mainarea is empty and any_small_dialog's default treats every full-screen
  # surface as "small"... here the fallback keeps 766x516 as main so its two real
  # modals count as small.
  maingeo=$("$GUI" windows 2>/dev/null | grep -Fi "|$class|" | grep -vi 'dialog=true' |
            awk -F'|' '{n=split($4,a,/[^0-9]+/); area=a[1]*a[2]; if (area>max) {max=area; line=$0}} END{print line}')
  [[ -n $maingeo ]] || maingeo=$("$GUI" windows 2>/dev/null | grep -Fi "|$class|" |
            awk -F'|' '{n=split($4,a,/[^0-9]+/); area=a[1]*a[2]; if (area>max) {max=area; line=$0}} END{print line}')
  mainarea=$(echo "$maingeo" | awk -F'|' '{n=split($4,a,/[^0-9]+/); print a[1]*a[2]}')

  for i in $(seq 1 "$ROUNDS"); do
    park_cef_overlays
    click_dialog_button "Configuration Issues"
    click_dialog_button "System Compatibility Report"
    # The audio app's crash-recovery prompt: Open/Delete/Later with Later at rel
    # (0.927,0.80) of 494x126+436+320 — measured from the captured screenshot. Open
    # would recover a session but "Later" is always safe.
    if [[ -n ${EXE_NAME[audio]:-} ]]; then
      line=$("$GUI" windows 2>/dev/null | grep -Fi "|${EXE_NAME[audio]}|" | grep 'dialog=true' |
             awk -F'|' 'length($1)>1 {print; exit}')
      if [[ -n $line ]]; then
        # only the crash prompt (494x126): a 3-button prompt whose safe action is Later
        w=$( echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[1]}' )
        h=$( echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[2]}' )
        x=$( echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[3]}' )
        y=$( echo "$line" | awk -F'|' '{split($4,a,/[^0-9]+/); print a[4]}' )
        if [[ $w == 494 && $h == 126 ]]; then
          echo "    clicking the audio app's crash-recovery 'Later' at $((x + 458)),$((y + 101))" >&2
          "$GUI" click $((x + 458)) $((y + 101)) >/dev/null 2>&1
          sleep 4
        fi
      fi
    fi
    click_dialog_button "Home screen"
    # Dismiss captioned modals FIRST, then gate the key-spam: stop poking once the
    # app's small dialogs are all gone. This matters only for the video editor: after
    # its System Compatibility Report is dismissed, its captionless CEF Home surface is
    # the top window, and a further Return/Escape reaches that surface and kills the
    # whole Home UI (measured lit 0.9989 -> 0.0274). With this gate the harness never
    # presses a useless key.
    if [[ -z $(any_small_dialog "$class" "$mainarea") ]]; then
      # still try the close-buttons and captioned dismissals one final time: their
      # click paths handle windows the gate does not know about (e.g. the compositor
      # app's captionless "Warning", which did not composite and therefore never
      # appeared in the early poke rounds).
      click_dialog_close "$class"
      dismiss_by_caption "System Compatibility Report"
      dismiss_by_caption "Warning"
      if [[ -z $(any_small_dialog "$class" "$mainarea") ]]; then
        # DO NOT break: the compositor app raises its captionless "Warning" modal ~90 s
        # in, AFTER the splash (visible in the earlier run that stayed on the splash).
        # Ending the loop there left it covered forever. Each round still re-checks;
        # keys are simply not pressed while the workspace is clean.
        printf '    poke %2d/%s  no dismissable dialog; keys off this round\n' "$i" "$ROUNDS" >&2
        sleep 15
        continue
      fi
    fi
    poke "$class"
    click_dialog_close "$class"
    dismiss_by_caption "System Compatibility Report"
    dismiss_by_caption "Warning"
    printf '    poke %2d/%s  dlg_left=%s\n' "$i" "$ROUNDS" \
      "$(onscreen_dialogs)" >&2
  done

  sleep 30
  local shot="$here/shots/unblock-$label.png" lit=0 sz=0 verdict
  "$GUI" shot "unblock-$label" >/dev/null 2>&1
  sz=$(stat -c %s "$shot" 2>/dev/null || echo 0)
  local winline geo
  winline=$("$GUI" windows 2>/dev/null | grep -Fi "|$class|" |
            awk -F'|' '{n=split($4,a,/[^0-9]+/); area=a[1]*a[2]; if (area>max) {max=area; line=$0}} END{print line}')
  geo=$(echo "$winline" | awk -F'|' '{print $4}')
  if [[ -n ${geo:-} && $sz -gt 8192 ]]; then
    local w h x y rest
    w=${geo%%x*}; rest=${geo#*x}; h=${rest%%+*}; rest=${rest#*+}; x=${rest%%+*}; y=${rest##*+}
    lit=$(magick "$shot" -crop "${w}x$((h - 76))+$x+$((y + 38))" +repage -alpha off \
          -colorspace Gray -threshold 8% -format '%[fx:mean]' info: 2>/dev/null)
  fi
  : "${lit:=0}"
  local dleft; dleft=$(onscreen_dialogs)
  local cap; cap=$(echo "$winline" | awk -F'|' '{print $1}')
  if (( dleft == 0 )) && awk -v m="$lit" 'BEGIN{exit !(m>=0.5)}'; then
    verdict="WORKSPACE"
  elif (( dleft == 0 )); then
    verdict="NO-DIALOG-BUT-DARK"
  else
    verdict="STILL-DIALOGGED"
  fi
  printf '%s\t%s\t%s\tlit=%.4f\tdlg_left=%s\tcaption=%s\n' \
    "$label" "$class" "$verdict" "$lit" "$dleft" "$cap" >>"$TSV"
  echo "    -> $verdict lit=$lit dlg_left=$dleft caption='$cap'" >&2
  kill_wine
}

# Build the app table from config/apps.local.env: codename -> exe path + resource class.
declare -A EXE_PATH EXE_NAME
for var in $(compgen -v APP_ | sort); do
  val=${!var}
  [[ -n $val ]] || continue
  key=${var#APP_}
  EXE_PATH[$key]=${val%%|*}
  EXE_NAME[$key]=${val##*|}
done
((${#EXE_PATH[@]})) || { echo "no APP_* entries in $cfg — fill in config/apps.local.env first" >&2; exit 2; }

# Original run order: worst blockers first.
ORDER=(compositor pdf photo_catalog stager mat_designer mat_sampler tex_painter
       vector layout copyedit asset_browser encoder web_editor nle_lite puppet
       nle audio raster)

[ -s "$TSV" ] || printf 'CODENAME\tCLASS\tVERDICT\tLIT\tDIALOGS\tCAPTION\n' >"$TSV"

want() { ((${#FILTER[@]} == 0)) || [[ " ${FILTER[*]} " == *" $1 "* ]]; }
FILTER=("$@")
for key in "${ORDER[@]}"; do
  [[ -n ${EXE_PATH[$key]:-} ]] || continue
  want "$key" || continue
  run "$key" "${EXE_PATH[$key]}" "${EXE_NAME[$key]}"
done
echo UNBLOCK-DONE

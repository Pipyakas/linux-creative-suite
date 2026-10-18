#!/usr/bin/env bash
# gui.sh: drive any Wine app's GUI under Wine under KDE Plasma (Wayland) + Xwayland for manual-style tests.
#   gui.sh focus                 raise and focus the largest non-dialog Wine window (KWin script)
#   gui.sh move X Y               X11 pointer (reaches Wine windows)
#   gui.sh kmove X Y / kclick X Y   uinput pointer (reaches compositor windows)
#   gui.sh click X Y [right|double|slow]   (X11 pointer — this is what Wine apps need)
#   gui.sh drag X1 Y1 X2 Y2      press at 1, move in steps, release at 2
#   gui.sh key COMBO...          e.g. key ctrl+n  key escape  key alt+f  key enter
#   gui.sh type 'text'
#   gui.sh shot NAME [WxH+X+Y]   full-screen screenshot (optional crop) -> shots/NAME.png
#                                exits 3 (no path printed) if the capture came back blank
#   gui.sh windows               KWin's window list (caption, class, dialog, geometry)
# Pointer: `move`/`click` use XTEST inside Xwayland (xdotool), because the prefix runs on Wine's
# X11 driver and every Wine window is an X11 client that reads the X11 pointer.  `kmove`/`kclick`
# use uinput (ydotool) and are only for compositor windows such as KWin's own menus.  Keyboard
# goes through ydotool, which does reach Wine.
set -u
here=$(dirname "$(readlink -f "$0")")
export DISPLAY=${DISPLAY:-:0} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$UID}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/user/$UID/bus}
# Xwayland mints a NEW auth cookie at every login; without this xdotool cannot connect at all
# and every pointer operation fails silently.
export XAUTHORITY=${XAUTHORITY:-$(ls -t /run/user/$UID/xauth_* 2>/dev/null | head -1)}
export YDOTOOL_SOCKET=${YDOTOOL_SOCKET:-/run/user/$UID/.ydotool_socket}
[[ -n ${XAUTHORITY:-} ]] || for f in $(ls -t /run/user/$UID/xauth_* 2>/dev/null); do export XAUTHORITY=$f; done
pgrep -x ydotoold >/dev/null || { systemctl --user start ydotool; sleep 1; }

kwin() {
  local f n=gui_$$_$RANDOM
  f=$(mktemp --suffix=.js); printf '%s\n' "$1" >"$f"
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$f" "$n" >/dev/null
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null; sleep 0.3
  qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$n" >/dev/null; rm -f "$f"
}
pos() { xdotool getmouselocation 2>/dev/null | sed -E 's/x:([0-9]+) y:([0-9]+).*/\1 \2/'; }
move() {
  # Pointer positioning, via XTEST **inside Xwayland** — i.e. `xdotool`, not ydotool.
  #
  # This is the single most important thing to know about driving these apps.  The prefix
  # runs on Wine's **X11** driver, so every Wine window is an X11 window inside Xwayland and
  # takes its input from the **X11 pointer**.  `ydotool` drives a *different* pointer: it goes
  # through uinput on the Wayland/compositor side.  The two are decoupled — measured, after
  # `ydotool mousemove --absolute 300 300` Xwayland still reported the pointer at 0,0 — so
  # **ydotool clicks reach KWin's own windows and silently do nothing to Wine's.**
  #
  # That produced a completely wrong diagnosis for hours: the vendor UI toolkit / the plugin runtime "surfaces ignore
  # synthetic input", "CEF under Wine drops pointer events", the video editor's New Project
  # "cannot be activated by any method".  All of it was this.  With `xdotool mousemove` +
  # `xdotool click`, the puppet app's "Import puppet..." opens its file dialog on the
  # first try, and the video editor's buttons work too.
  #
  # Keyboard still goes through ydotool (`key`), which does reach Wine — that is why Escape
  # dismissing the apps' dialogs always worked even while every mouse click silently failed.
  local tx=$1 ty=$2
  xdotool mousemove "$tx" "$ty" >/dev/null 2>&1 || {
    echo "gui.sh: move $tx,$ty failed (is DISPLAY/XAUTHORITY right?)" >&2; return 1; }
  sleep 0.15
}

# For the rare target that is a *compositor* window rather than a Wine one — e.g. KWin's
# window menu or the "xdotool is asking to control input devices" consent dialog.  Those are
# not X11 clients and need the uinput pointer.
kmove() {
  local tx=$1 ty=$2
  ydotool mousemove --absolute "$tx" "$ty" >/dev/null 2>&1
  sleep 0.15
}
keycode() { # evdev codes (linux/input-event-codes.h)
  case ${1,,} in
    esc|escape) echo 1;; tab) echo 15;; enter|return) echo 28;; space) echo 57;;
    backspace) echo 14;; delete|del) echo 111;; up) echo 103;; down) echo 108;;
    left) echo 105;; right) echo 106;; home) echo 102;; end) echo 107;;
    pageup) echo 104;; pagedown) echo 109;;
    ctrl) echo 29;; shift) echo 42;; alt) echo 56;; super|meta) echo 125;;
    f[1-9]) echo $((58 + ${1:1}));; f10) echo 68;; f11) echo 87;; f12) echo 88;;
    [a-z]) local c=${1,,} r
           for r in "16 qwertyuiop" "30 asdfghjkl" "44 zxcvbnm"; do
             set -- $r; local s=${2%%"$c"*}; [[ $s != "$2" ]] && { echo $(($1 + ${#s})); return; }
           done;;
    [1-9]) echo $((1 + $1));; 0) echo 11;; minus|-) echo 12;; equal|=|plus) echo 13;;
    comma|,) echo 51;; dot|.) echo 52;; slash|/) echo 53;;
    *) echo "unknown key $1" >&2; return 1;;
  esac
}
key() {
  local combo k codes=() down="" up=""
  for combo in "$@"; do
    IFS=+ read -ra codes <<<"$combo"; down=""; up=""
    for k in "${codes[@]}"; do k=$(keycode "$k") || return 1; down+=" $k:1"; up="$k:0 $up"; done
    ydotool key $down $up >/dev/null 2>&1; sleep 0.25
  done
}
case ${1:-} in
  focus) kwin 'for (const w of workspace.windowList()) if (w.resourceClass == "steam_proton" && !w.dialog && w.frameGeometry.width > 600) { w.minimized = false; workspace.activeWindow = w; }' ;;
  move) move "$2" "$3" ;;
  kmove) kmove "$2" "$3" ;;
  click)
    move "$2" "$3"
    case ${4:-} in
      right) xdotool click 3 >/dev/null 2>&1 ;;
      double) xdotool click --repeat 2 --delay 80 1 >/dev/null 2>&1 ;;
      slow)   xdotool mousedown 1 >/dev/null 2>&1; sleep 0.25; xdotool mouseup 1 >/dev/null 2>&1 ;;
      *) xdotool click 1 >/dev/null 2>&1 ;;
    esac; sleep 0.3 ;;
  kclick)
    kmove "$2" "$3"; ydotool click 0xC0 >/dev/null 2>&1; sleep 0.3 ;;
  drag)
    move "$2" "$3"; xdotool mousedown 1 >/dev/null 2>&1; sleep 0.15
    for i in 1 2 3 4 5 6 7 8; do
      xdotool mousemove $(( $2 + ($4 - $2) * i / 8 )) $(( $3 + ($5 - $3) * i / 8 )) >/dev/null 2>&1; sleep 0.05
    done
    sleep 0.1; xdotool mouseup 1 >/dev/null 2>&1; sleep 0.3 ;;
  key) shift; key "$@" ;;
  type) ydotool type -d 30 "$2" >/dev/null 2>&1 ;;
  shot)
    mkdir -p "$here/shots"
    # Two ways a capture comes back blank (<8 KB) instead of failing, both verified here:
    #   * the display is asleep (DPMS) — nudge the pointer, then it captures;
    #   * the XDG screenshot portal is not serving Screenshot (this was the real cause for
    #     weeks: XDG_CURRENT_DESKTOP was unset, so kde.portal was never selected and every
    #     interface fell back to gtk, which has no Screenshot implementation — spectacle
    #     then exited 0 writing a valid all-black PNG).  See probe/other/SCREENSHOT-PORTAL.md.
    #     Check with: systemctl --user is-active xdg-desktop-portal
    # Wake first, then retry, then warn loudly so a blank frame is never mistaken for an
    # empty window.
    for attempt in 1 2 3 4; do
      ydotool mousemove -x 3 -y 3 >/dev/null 2>&1
      ydotool mousemove -x -6 -y -6 >/dev/null 2>&1
      sleep 1
      # A spectacle killed by `timeout` can keep the XDG-desktop-portal grab busy and
      # make every later capture return 0 bytes, so clear stale ones first.
      pkill -9 -x spectacle >/dev/null 2>&1
      rm -f "$here/shots/$2.png"
      # 45 s, not 20: spectacle's first capture after the portal starts is slow
      # (it has to build its own caches), and being killed by the timeout used
      # to leave the portal's grab busy and blank every later capture too.
      timeout 45 spectacle -b -n -f -o "$here/shots/$2.png" >/dev/null 2>&1
      sz=$(stat -c %s "$here/shots/$2.png" 2>/dev/null || echo 0)
      (( sz > 8192 )) && break
      sleep 2
    done
    if (( sz < 8192 )); then
      # Do NOT suggest restarting plasma-kwin_wayland here: that advice was wrong and
      # destroys every running Wine app.  A blank capture means the XDG portal is down
      # again — see probe/other/SCREENSHOT-PORTAL.md.
      echo "gui.sh: WARNING blank/absent screenshot ($sz bytes) — check the portal:" >&2
      echo "         systemctl --user is-active xdg-desktop-portal" >&2
      echo "         gdbus introspect --session --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop | grep -c Screenshot" >&2
      # Exit non-zero on a blank capture.  It used to exit 0 and print the path anyway,
      # which produced a real false measurement: an empty magick result was reported as
      # "0 lit pixels" and briefly suggested the suite prefix itself broke wined3d.
      # Callers must not have to check the file size themselves.
      exit 3
    fi
    [[ -n ${3:-} ]] && magick "$here/shots/$2.png" -crop "$3" +repage "$here/shots/$2.png"
    echo "$here/shots/$2.png" ;;
  windows)
    tag=W$$; kwin "for (const w of workspace.windowList()) { var g = w.frameGeometry; print('$tag|' + w.caption + '|' + w.resourceClass + '|dialog=' + w.dialog + '|' + Math.round(g.width) + 'x' + Math.round(g.height) + '+' + Math.round(g.x) + '+' + Math.round(g.y)); }"
    journalctl --user --since '-5s' -o cat | grep "^$tag|" | cut -d'|' -f2- ;;
  *) sed -n '2,13p' "$0"; exit 2 ;;
esac

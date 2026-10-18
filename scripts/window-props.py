#!/usr/bin/env python3
"""Catch the raster image editor's 0.38-second window and dump its X properties before it dies.

Established so far (earlier window probes):
  * The app creates a real top-level window: 782x532 at (289,65),
    WM_CLASS = the app's own exe file name (name/class), name = its title.
  * It lives only ~0.38 s, first seen ~7.9 s into the launch.
  * KWin never lists it and never composites it, so every full-screen screenshot is empty.

Why that happens is decided by the window's WM hints — in particular WM_WINDOW_TYPE /
_NET_WM_WINDOW_TYPE, WM_TRANSIENT_FOR and WM_HINTS, because KWin declines to manage windows
it classifies as utility/splash/dock types or as override-redirect. Querying them by hand is
impossible: the window is destroyed long before any shell command runs, and `xprop` reports
BadWindow. So enumerate and query in the same process, in the same millisecond.

Writes each property dump to the output directory.

Usage: window-props.py <out-dir> <seconds> <window-name-substring>
  The window-name substring is REQUIRED and is matched case-insensitively against each
  window's WM_NAME.
"""
import ctypes, os, subprocess, sys, time

if len(sys.argv) < 4:
    sys.stderr.write("usage: %s <out-dir> <seconds> <window-name-substring>\n" % sys.argv[0])
    sys.exit(2)
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/window-props"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 32.0
NEEDLE = sys.argv[3]
os.makedirs(OUT, exist_ok=True)

PROPS = ["WM_CLASS", "WM_NAME", "_NET_WM_NAME", "WM_STATE", "WM_WINDOW_TYPE",
         "_NET_WM_WINDOW_TYPE", "WM_TRANSIENT_FOR", "WM_NORMAL_HINTS", "WM_HINTS",
         "WM_PROTOCOLS", "_MOTIF_WM_HINTS", "override-redirect", "map-state",
         "_KDE_NET_WM_WINDOW_TYPE", "_KDE_WM_WINDOW_TYPE"]

lib = ctypes.CDLL("libX11.so.6")
lib.XOpenDisplay.restype = ctypes.c_void_p
lib.XDefaultRootWindow.restype = ctypes.c_ulong
lib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
lib.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                           ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
                           ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                           ctypes.POINTER(ctypes.c_uint)]
lib.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p]
lib.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_char_p)]
lib.XFree.argtypes = [ctypes.c_void_p]

H = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
KEEP = H(lambda *_a: 0)
lib.XSetErrorHandler.argtypes = [ctypes.c_void_p]
lib.XSetErrorHandler.restype = ctypes.c_void_p
lib.XSetErrorHandler(KEEP)


class WAttr(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int), ("width", ctypes.c_int),
                ("height", ctypes.c_int), ("border_width", ctypes.c_int),
                ("depth", ctypes.c_int), ("visual", ctypes.c_void_p), ("root", ctypes.c_ulong),
                ("class_", ctypes.c_int), ("bit_gravity", ctypes.c_int),
                ("win_gravity", ctypes.c_int), ("backing_store", ctypes.c_int),
                ("backing_planes", ctypes.c_ulong), ("backing_pixel", ctypes.c_ulong),
                ("save_under", ctypes.c_int), ("colormap", ctypes.c_ulong),
                ("map_installed", ctypes.c_int), ("map_state", ctypes.c_int),
                ("all_event_masks", ctypes.c_long), ("your_event_mask", ctypes.c_long),
                ("do_not_propagate_mask", ctypes.c_long), ("override_redirect", ctypes.c_int),
                ("screen", ctypes.c_void_p)]


def name_of(dpy, w):
    p = ctypes.c_char_p()
    if lib.XFetchName(dpy, w, ctypes.byref(p)) and p.value:
        v = p.value.decode("utf-8", "replace")
        lib.XFree(p)
        return v
    return ""


def walk(dpy, root, out):
    r = ctypes.c_ulong(); par = ctypes.c_ulong()
    ch = ctypes.POINTER(ctypes.c_ulong)(); n = ctypes.c_uint()
    if not lib.XQueryTree(dpy, root, ctypes.byref(r), ctypes.byref(par),
                          ctypes.byref(ch), ctypes.byref(n)):
        return
    for i in range(n.value):
        w = ch[i]
        a = WAttr()
        if lib.XGetWindowAttributes(dpy, w, ctypes.byref(a)) and a.map_state == 2 and \
                a.width > 1 and a.height > 1 and not a.override_redirect:
            out.append((w, a.x, a.y, a.width, a.height, name_of(dpy, w)))
        walk(dpy, w, out)
    if ch:
        lib.XFree(ch)


def dump(win, path):
    """Query in-process via xprop, in this very thread, so we win the 0.38 s race."""
    lines = []
    env = dict(os.environ)
    for pn in PROPS:
        try:
            r = subprocess.run(["xprop", "-id", hex(win), pn],
                               capture_output=True, text=True, timeout=2, env=env)
            txt = (r.stdout or r.stderr).strip()
        except Exception as e:                      # noqa: BLE001
            txt = "ERROR %s" % e
        # A dead window makes every property query fail; say so once and stop.
        if "BadWindow" in txt:
            txt = "<window already destroyed when queried>"
        lines.append("%-24s %s" % (pn, txt))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return lines


d = lib.XOpenDisplay(None)
t0 = time.time()
seen = set()
got = 0
while time.time() - t0 < SECONDS:
    cur = []
    walk(d, lib.XDefaultRootWindow(d), cur)
    for w, x, y, wd, ht, nm in cur:
        if NEEDLE.lower() in nm.lower() and w not in seen:
            seen.add(w)
            el = time.time() - t0
            p = os.path.join(OUT, "win-0x%x-%.2fs.txt" % (w, el))
            lines = dump(w, p)
            print("dumped %s (0x%x %dx%d+%d+%d %r)" % (p, w, wd, ht, x, y, nm), flush=True)
            for ln in lines:
                print("   " + ln, flush=True)
            got += 1
    time.sleep(0.004)
print("done: %d window(s) inspected" % got, flush=True)
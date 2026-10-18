#!/usr/bin/env python3
"""How long does the raster image editor's window actually stay alive, at 5 ms resolution?

Context: an earlier capture attempt produced a file of pure garbage, and a control run
showed XGetImage returns NULL on the root window of this Xwayland server. So the garbage
was the capture path failing, NOT app rendering noise. Before picking a capture method,
measure the window's lifetime — if it lives long enough, a plain full-screen screenshot
will catch it; if it is a few milliseconds, it needs a different approach entirely.

Prints one line per window: id, size, position, and the first/last timestamp it was seen.

Usage: window-lifetime.py <seconds> <window-name-substring>
  The window-name substring is REQUIRED and is matched case-insensitively against each
  window's WM_NAME.
"""
import ctypes, sys, time

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


if len(sys.argv) < 3:
    sys.stderr.write("usage: %s <seconds> <window-name-substring>\n" % sys.argv[0])
    sys.exit(2)
SECONDS = float(sys.argv[1])
NEEDLE = sys.argv[2]

d = lib.XOpenDisplay(None)
t0 = time.time()
first, last = {}, {}
polls = 0
while time.time() - t0 < SECONDS:
    cur = []
    walk(d, lib.XDefaultRootWindow(d), cur)
    now = time.time() - t0
    polls += 1
    for w, x, y, wd, ht, nm in cur:
        if NEEDLE.lower() in nm.lower():
            first.setdefault(w, now)
            last[w] = now
    time.sleep(0.005)

print("polls=%d over %.1fs (%.0f Hz)" % (polls, SECONDS, polls / SECONDS))
if not first:
    print("no matching window seen at any point")
for w in first:
    print("  win 0x%x  %s  first=%.3fs  last=%.3fs  lifetime>=%.3fs" %
          (w, "captured earlier", first[w], last[w], last[w] - first[w]))
#!/usr/bin/env python3
"""Force the raster image editor's main window to map, and see whether that unblocks the app.

Measured facts (earlier tree/lifetime/xprop probes):
  * ~7.6 s: the app creates its main window 0x1c00007's parent 0x1c00001, 1181x608 at (44,52),
    WM_CLASS = the app's own exe file name, name = its title, map_state = 0 (Unmapped).
  * It is NEVER mapped. A 782x532 splash dialog (transient for 0x1c00001, input=False) is
    mapped for only 0.38 s at 7.9-8.3 s.
  * ~10 s: main window still unmapped. ~16 s: every app window is gone. ~21 s: process
    exits rc=0 with `SPL ~DefaultMemoryManagerImpl bytesAllocated = 0`.

So the long-standing verdict "the app creates no window" is wrong: it creates a complete,
correctly sized, correctly titled main window and simply never shows it. KWin manages only
mapped windows, which is why the window audit - which reads KWin's window list - has never
seen it, and why every screenshot has been empty.

Hypothesis under test: the app is blocked waiting on its own window becoming visible, and
will finish initialising if the map happens. This script calls XMapWindow on the main window
as soon as it appears unmapped, repeatedly, and reports whether the map sticks and whether
the window survives.

Usage: force-map.py <out-dir> <seconds> <window-name-substring>
  The window-name substring is REQUIRED and is matched case-insensitively against each
  window's WM_NAME.
"""
import ctypes, os, sys, time

if len(sys.argv) < 4:
    sys.stderr.write("usage: %s <out-dir> <seconds> <window-name-substring>\n" % sys.argv[0])
    sys.exit(2)
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/force-map"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 40.0
NEEDLE = sys.argv[3]
os.makedirs(OUT, exist_ok=True)

lib = ctypes.CDLL("libX11.so.6")
lib.XOpenDisplay.restype = ctypes.c_void_p
lib.XDefaultRootWindow.restype = ctypes.c_ulong
lib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
lib.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                           ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
                           ctypes.POINTER(ctypes.c_void_p),
                           ctypes.POINTER(ctypes.c_uint)]
lib.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                           ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
                           ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                           ctypes.POINTER(ctypes.c_uint)]
lib.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p]
lib.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_char_p)]
lib.XMapWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
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
        if lib.XGetWindowAttributes(dpy, w, ctypes.byref(a)) and a.width > 200 and a.height > 200:
            out.append((w, a.width, a.height, a.map_state, name_of(dpy, w)))
        walk(dpy, w, out)
    if ch:
        lib.XFree(ch)


d = lib.XOpenDisplay(None)
t0 = time.time()
forced = set()
log = open(os.path.join(OUT, "force-map.log"), "w")
while time.time() - t0 < SECONDS:
    cur = []
    walk(d, lib.XDefaultRootWindow(d), cur)
    now = time.time() - t0
    for w, wd, ht, ms, nm in cur:
        if NEEDLE.lower() in nm.lower() and wd > 1000 and ms == 0:
            lib.XMapWindow(d, w)
            msg = "t=%.2f  XMapWindow 0x%x (%dx%d, was unmapped)" % (now, w, wd, ht)
            print(msg, flush=True)
            log.write(msg + "\n"); log.flush()
            forced.add(w)
        elif NEEDLE.lower() in nm.lower() and wd > 1000 and w in forced:
            msg = "t=%.2f  0x%x now map_state=%d" % (now, w, ms)
            print(msg, flush=True)
            log.write(msg + "\n"); log.flush()
    time.sleep(0.02)
log.close()
print("done; forced ids: %s" % [hex(w) for w in forced], flush=True)
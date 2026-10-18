#!/usr/bin/env python3
"""Dump the suite's entire X window tree at a chosen moment, no name filtering.

Established so far:
  * ~7.9 s into launch the app creates a 782x532 dialog carrying its main window's
    title, _NET_WM_WINDOW_TYPE_DIALOG, WM_TRANSIENT_FOR 0x1c00001, input=False,
    min==max size so it is a fixed-size modal. It lives only ~0.38 s.
  * KWin lists none of it and composites none of it, so screenshots are empty.

The open question is what surrounds that dialog. `xwin.py`-style filtering by name misses
windows with an empty WM_NAME, and 0x1c00001 (the transient parent) is exactly such a
suspect. So: no filter at all, walk the whole tree, and record geometry, class, name,
map_state and override_redirect for every window at each sample time.
"""
import ctypes, os, subprocess, sys, time

OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/pstree"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
SAMPLES = [float(s) for s in (sys.argv[3].split(",") if len(sys.argv) > 3 else ["8.0"])]
os.makedirs(OUT, exist_ok=True)

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
lib.XGetClassHint.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p]
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


class XClassHint(ctypes.Structure):
    _fields_ = [("res_name", ctypes.c_char_p), ("res_class", ctypes.c_char_p)]


def name_of(dpy, w):
    p = ctypes.c_char_p()
    if lib.XFetchName(dpy, w, ctypes.byref(p)) and p.value:
        v = p.value.decode("utf-8", "replace")
        lib.XFree(p)
        return v
    return ""


def class_of(dpy, w):
    h = XClassHint()
    if lib.XGetClassHint(dpy, w, ctypes.byref(h)):
        return "%s/%s" % ((h.res_name or b"").decode("utf-8", "replace"),
                          (h.res_class or b"").decode("utf-8", "replace"))
    return ""


def walk(dpy, root, out, depth=0):
    r = ctypes.c_ulong(); par = ctypes.c_ulong()
    ch = ctypes.POINTER(ctypes.c_ulong)(); n = ctypes.c_uint()
    if not lib.XQueryTree(dpy, root, ctypes.byref(r), ctypes.byref(par),
                          ctypes.byref(ch), ctypes.byref(n)):
        return
    for i in range(n.value):
        w = ch[i]
        a = WAttr()
        if lib.XGetWindowAttributes(dpy, w, ctypes.byref(a)):
            out.append((depth, w, a.x, a.y, a.width, a.height, a.map_state,
                        a.override_redirect, class_of(dpy, w), name_of(dpy, w)))
        walk(dpy, w, out, depth + 1)
    if ch:
        lib.XFree(ch)


d = lib.XOpenDisplay(None)
t0 = time.time()
pending = sorted(SAMPLES)
while pending and time.time() - t0 < SECONDS:
    now = time.time() - t0
    while pending and pending[0] <= now:
        target = pending.pop(0)
        cur = []
        walk(d, lib.XDefaultRootWindow(d), cur)
        p = os.path.join(OUT, "tree-%.1fs.txt" % target)
        with open(p, "w") as f:
            f.write("sample at t=%.2fs (requested %.1f)\n" % (now, target))
            f.write("%-4s %-12s %-16s %-11s %-5s %-6s %s\n" %
                    ("dep", "id", "geom", "map", "ovr", "class", "name"))
            for depth, w, x, y, wd, ht, ms, ovr, cl, nm in sorted(cur, key=lambda r: -r[1]):
                f.write("%-4d 0x%-10x %-16s %-11d %-5d %-6s %r\n" %
                        (depth, w, "%dx%d+%d+%d" % (wd, ht, x, y), ms, ovr, cl, nm))
        print("wrote %s (%d windows)" % (p, len(cur)), flush=True)
    time.sleep(0.004)
print("done", flush=True)
#!/usr/bin/env python3
"""Minimal X11 window enumerator via ctypes libX11 (no xdotool/xwininfo needed).

Modes:
  list                 -> one line per mapped top-level window: id geom class name
  watch <secs> [out]   -> poll every 0.25s, print NEW mapped windows with elapsed
                          seconds since start; writes same lines to <out> if given
"""
import ctypes, ctypes.util, sys, time

lib = ctypes.CDLL("libX11.so.6")
lib.XOpenDisplay.restype = ctypes.c_void_p
lib.XDefaultRootWindow.restype = ctypes.c_ulong
lib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
lib.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                           ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                           ctypes.POINTER(ctypes.c_uint)]
lib.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p]
lib.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_char_p)]
lib.XGetClassHint.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p]
lib.XGetGeometry.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                            ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint),
                            ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
lib.XFree.argtypes = [ctypes.c_void_p]

# Swallow BadWindow/etc. instead of letting libX11's default handler exit the process.
_ERROR_HANDLER = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)


def _quiet_handler(err_display, err_event):
    return 0


_HANDLER_KEEPALIVE = _ERROR_HANDLER(_quiet_handler)
lib.XSetErrorHandler.argtypes = [ctypes.c_void_p]
lib.XSetErrorHandler.restype = ctypes.c_void_p
lib.XSetErrorHandler(_HANDLER_KEEPALIVE)


class XWindowAttributes(ctypes.Structure):
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
        n = p.value.decode("utf-8", "replace")
        lib.XFree(p)
        return n
    return ""


def class_of(dpy, w):
    h = XClassHint()
    if lib.XGetClassHint(dpy, w, ctypes.byref(h)):
        return (h.res_name or b"").decode("utf-8", "replace") + "/" + (h.res_class or b"").decode("utf-8", "replace")
    return ""


def walk(dpy, root, out):
    r = ctypes.c_ulong(); parent = ctypes.c_ulong(); children = ctypes.POINTER(ctypes.c_ulong)(); n = ctypes.c_uint()
    if not lib.XQueryTree(dpy, root, ctypes.byref(r), ctypes.byref(parent),
                           ctypes.byref(children), ctypes.byref(n)):
        return
    for i in range(n.value):
        w = children[i]
        a = XWindowAttributes()
        if lib.XGetWindowAttributes(dpy, w, ctypes.byref(a)) and a.map_state == 2 and \
                a.width > 1 and a.height > 1 and not a.override_redirect:
            out.append((w, a.x, a.y, a.width, a.height, class_of(dpy, w), name_of(dpy, w)))
        walk(dpy, w, out)
    if children:
        lib.XFree(children)


def snapshot(dpy):
    out = []
    walk(dpy, lib.XDefaultRootWindow(dpy), out)
    return out


def main():
    mode = sys.argv[1]
    dpy = lib.XOpenDisplay(None)
    if not dpy:
        print("NO DISPLAY", file=sys.stderr)
        sys.exit(2)
    if mode == "list":
        for w, x, y, wd, ht, cl, nm in sorted(snapshot(dpy)):
            print(f"0x{w:x} {x},{y} {wd}x{ht} [{cl}] {nm!r}")
    elif mode == "watch":
        secs = float(sys.argv[2])
        outp = open(sys.argv[3], "a", buffering=1) if len(sys.argv) > 3 else None
        t0 = time.time()
        seen = {w for w, *_ in snapshot(dpy)}
        end = t0 + secs
        while time.time() < end:
            for w, x, y, wd, ht, cl, nm in snapshot(dpy):
                if w not in seen:
                    seen.add(w)
                    el = time.time() - t0
                    line = f"[{el:7.2f}s] NEW 0x{w:x} {x},{y} {wd}x{ht} [{cl}] {nm!r}"
                    print(line)
                    if outp:
                        outp.write(line + "\n")
            time.sleep(0.25)
    lib.XCloseDisplay(dpy)


main()

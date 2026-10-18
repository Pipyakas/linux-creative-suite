/* win-click.c — inspect and poke a suite app's window tree from inside Wine.
 *
 * Why this exists: an earlier diagnosis held that the video editor's HTML-panel "Home"
 * surface (the vendor UI toolkit / the plugin runtime) ignores synthetic input under
 * Wine's X11 driver, and that "New Project" could not be activated by any pointer
 * method.  That diagnosis was wrong — it was a pointer-path error, not a CEF input
 * wall (see gui.sh `move`).  With `xdotool mousemove` + `xdotool click 1` the surface
 * accepts real pointer input fine; a normal GDI dialog (the System Compatibility Report)
 * also responds to the same clicks.  This tool posts a click as a real Win32 message from
 * inside the process's own window procedure's address space, as an independent check that
 * does not depend on the X11 pointer.
 *
 * usage:
 *   win-click tree <substring>
 *                                      print the whole window tree of every top-level
 *                                      window whose class or caption contains
 *                                      <substring> (case-insensitive)
 *   win-click click <class-or-caption-substring> <x> <y>
 *                                      post WM_LBUTTONDOWN/WM_LBUTTONUP at client (x,y)
 *                                      to the first matching window
 *   win-click keys <class-or-substring> <vk> [<vk> ...]
 *                                      post WM_KEYDOWN/WM_KEYUP for the given virtual keys
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -o win-click.exe win-click.c -luser32
 */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>

static const wchar_t *g_want_class = NULL;
static const wchar_t *g_want_title = NULL;

/* Case-insensitive wide-char substring test: does `hay` contain `needle`? */
static int needle_in(const wchar_t *hay, const wchar_t *needle)
{
    size_t nlen, hlen, i, j;
    if (!needle || !*needle) return 0;
    nlen = wcslen(needle);
    hlen = wcslen(hay);
    if (nlen > hlen) return 0;
    for (i = 0; i + nlen <= hlen; i++)
    {
        for (j = 0; j < nlen; j++)
            if (towlower(hay[i + j]) != towlower(needle[j])) break;
        if (j == nlen) return 1;
    }
    return 0;
}

static void print_tree(HWND h, int depth)
{
    wchar_t cls[256], cap[512];
    cls[0] = cap[0] = 0;
    GetClassNameW(h, cls, 256);
    GetWindowTextW(h, cap, 512);
    RECT r; GetWindowRect(h, &r);
    wchar_t vis[8] = L"?";
    if (IsWindowVisible(h)) wcscpy(vis, L"vis");
    for (int i = 0; i < depth; i++) wprintf(L"  ");
    wprintf(L"hwnd=%p cls=\"%ls\" cap=\"%ls\" %ls rect=%ld,%ld-%ld,%ld style=%08lx ex=%08lx\n",
            (void *)h, cls, cap, vis,
            (long)(r.right - r.left), (long)(r.bottom - r.top), (long)r.left, (long)r.top,
            (unsigned long)GetWindowLongW(h, GWL_STYLE),
            (unsigned long)GetWindowLongW(h, GWL_EXSTYLE));
    HWND child = GetWindow(h, GW_CHILD);
    while (child) { print_tree(child, depth + 1); child = GetWindow(child, GW_HWNDNEXT); }
}

static BOOL CALLBACK find_cb(HWND h, LPARAM lp)
{
    wchar_t cls[256], cap[512];
    cls[0] = cap[0] = 0;
    GetClassNameW(h, cls, 256);
    GetWindowTextW(h, cap, 512);
    if (g_want_class && wcsstr(cls, g_want_class) == NULL) return TRUE;
    if (g_want_title && wcsstr(cap, g_want_title) == NULL) return TRUE;
    SetPropW(h, L"found", (HANDLE)1);
    *(HWND *)lp = h;
    return FALSE;
}

int wmain(int argc, wchar_t **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* a crash must not eat the output */

    /* every suite app runs its UI in one process; find the top-level windows belonging
       to the process we were told about, or matching the required needle. */
    if (argc < 2)
    {
        wprintf(L"usage: win-click tree <substring>\n"
                L"       win-click click <substring> <x> <y>\n"
                L"       win-click keys <substring> <vk> [<vk> ...]\n");
        return 2;
    }

    if (wcscmp(argv[1], L"tree") == 0)
    {
        if (argc < 3)
        {
            wprintf(L"usage: win-click tree <class-or-caption-substring>\n");
            return 2;
        }
        const wchar_t *needle = argv[2];
        /* walk every top-level window of this desktop */
        HWND h = GetDesktopWindow();
        HWND w = GetWindow(h, GW_CHILD);
        while (w)
        {
            wchar_t cls[256], cap[512], proc[300];
            cls[0] = cap[0] = proc[0] = 0;
            GetClassNameW(w, cls, 256);
            GetWindowTextW(w, cap, 512);
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            if (pid == GetCurrentProcessId() || needle_in(cls, needle) || needle_in(cap, needle))
            {
                swprintf(proc, 300, L"pid=%lu", (unsigned long)pid);
                wprintf(L"TOP hwnd=%p cls=\"%ls\" cap=\"%ls\" %s\n", (void *)w, cls, cap, proc);
                print_tree(w, 1);
            }
            w = GetWindow(w, GW_HWNDNEXT);
        }
        return 0;
    }

    if (argc < 5) { wprintf(L"usage: win-click click <substring> <x> <y>\n"); return 2; }

    /* "click" takes SCREEN coordinates.  WindowFromPoint resolves the real window —
       including a child like the video editor's the view container — and the point is then
       translated into that window's client area.  Matching by name is unreliable here
       because the vendor UI toolkit's Home screen has an empty caption. */
    int sx = _wtoi(argv[argc - 2]);
    int sy = _wtoi(argv[argc - 1]);

    POINT sp; sp.x = sx; sp.y = sy;
    HWND target = WindowFromPoint(sp);
    if (!target) { wprintf(L"WindowFromPoint(%d,%d) found nothing\n", sx, sy); return 1; }

    /* Walk up to the nearest ancestor that can take focus; the leaf may be NOACTIVATE. */
    HWND focusable = target;
    while (focusable && (GetWindowLongW(focusable, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
        focusable = GetParent(focusable);

    wchar_t cls[256], cap[512];
    GetClassNameW(target, cls, 256); GetWindowTextW(target, cap, 512);
    wprintf(L"screen (%d,%d) -> hwnd=%p cls=\"%ls\" cap=\"%ls\"\n", sx, sy, (void *)target, cls, cap);
    if (focusable && focusable != target)
        wprintf(L"focusable ancestor hwnd=%p\n", (void *)focusable);
    if (focusable) { SetForegroundWindow(focusable); SetFocus(focusable); SetActiveWindow(focusable); }
    Sleep(300);

    POINT p; p.x = sx; p.y = sy;
    ScreenToClient(target, &p);
    wprintf(L"client coords in target: (%ld,%ld)\n", (long)p.x, (long)p.y);

    LPARAM lp = MAKELPARAM((SHORT)p.x, (SHORT)p.y);
    SendMessageW(target, WM_MOUSEMOVE, 0, lp);
    Sleep(150);
    SendMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    Sleep(150);
    SendMessageW(target, WM_LBUTTONUP, 0, lp);
    wprintf(L"posted WM_LBUTTONDOWN/UP to hwnd=%p at client (%ld,%ld)\n",
            (void *)target, (long)p.x, (long)p.y);
    return 0;
}

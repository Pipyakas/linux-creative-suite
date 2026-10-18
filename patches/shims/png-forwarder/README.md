# png-forwarder

`png.def` — a module-definition file that builds `png.dll`, forwarding every libpng
1.6 export by name to the system's `libpng16` (each line is
`png_foo=libpng16.png_foo`). The suite ships its own `png.dll` beside some app
binaries; Wine's builtin PNG support or a missing module name can make the loader
look for it, and this forwarder satisfies that lookup without bundling any third-party
binary — it re-exports whatever libpng the distro provides.

Build (any mingw will do):

    x86_64-w64-mingw32-gcc -shared -o png.dll png.def -L/usr/x86_64-w64-mingw32/lib -lpng16

Status: built and verified loading in the prefix (source-only here; the built DLL is
not committed). Install by copying `png.dll` next to the app binary that asks for it.

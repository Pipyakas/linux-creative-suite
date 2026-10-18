#!/usr/bin/env bash
# install.sh [--show|--uninstall] — put the DXGI wrapper in front of DXVK's dxgi.dll.
#
#   (no argument)  install: back up, rename DXVK's dxgi.dll -> dxgi-dxvk.dll, install ours
#   --show         report what is currently installed
#   --uninstall    restore DXVK's dxgi.dll byte-for-byte in both bitnesses
#
# PREFIX=/path/to/prefix ./install.sh
set -u
HERE=$(dirname "$(readlink -f "$0")")
if [[ -z ${PREFIX:-} ]]; then
  cfg="$HERE/../../../config/apps.local.env"
  [[ -f $cfg ]] && . "$cfg"
fi
PREFIX=${PREFIX:-${WINEPREFIX:-$HOME/.local/share/wineprefixes/suite}}
SYS=$PREFIX/drive_c/windows/system32
WOW=$PREFIX/drive_c/windows/syswow64
BACKUP=$HERE/backup
say() { printf '%s\n' "$*"; }

show() {
  for d in "$SYS" "$WOW"; do
    for f in dxgi.dll dxgi-dxvk.dll dxgi-wrapper.dll; do
      [[ -e $d/$f ]] && say "  $(basename "$d")/$f  $(stat -c %s "$d/$f") bytes"
    done
  done
  grep -o '"\*dxgi"="[a-z]*"' "$PREFIX/user.reg" 2>/dev/null | sed 's/^/  override: /'
}

case "${1:-install}" in
  --show)
    say "DXGI wrapper state in $PREFIX:"
    show
    say
    say "current dxgi.dll is:"
    for d in "$SYS" "$WOW"; do
      [[ -e $d/dxgi.dll ]] && say "  $d/dxgi.dll  $(sha256sum "$d/dxgi.dll" | cut -c1-16)  (wrapper=$(cmp -s "$d/dxgi.dll" "$HERE/build/dxgi-wrapper.dll" && echo yes || echo no))"
    done
    exit 0
    ;;
  --uninstall)
    for d in "$SYS" "$WOW"; do
      if [[ -e $d/dxgi-dxvk.dll ]]; then
        rm -f "$d/dxgi.dll"
        mv -f "$d/dxgi-dxvk.dll" "$d/dxgi.dll"
        say "restored DXVK's dxgi.dll in $d"
      fi
      rm -f "$d/dxgi-wrapper.dll"
    done
    WINEPREFIX=$PREFIX timeout 30 wineserver -k >/dev/null 2>&1
    say "uninstalled"
    exit 0
    ;;
  install) ;;
  *) say "usage: $0 [--show|--uninstall]" >&2; exit 2 ;;
esac

# Already installed?
if [[ -e $SYS/dxgi-dxvk.dll ]]; then
  say "already installed (DXVK's dxgi.dll is present as dxgi-dxvk.dll)"
  exit 0
fi

mkdir -p "$BACKUP"
[[ -e $BACKUP/user.reg ]] || cp "$PREFIX/user.reg" "$BACKUP/user.reg"
for d in "$SYS" "$WOW"; do
  [[ -e $d/dxgi.dll ]] && cp -n "$d/dxgi.dll" "$BACKUP/dxgi.dll.$(basename "$d")"
done
say "backed up to $BACKUP"

for d in "$SYS" "$WOW"; do
  src=$HERE/build/dxgi-wrapper.dll
  [[ $d == "$WOW" ]] && src=$HERE/build/win32/dxgi-wrapper.dll
  [[ -e $d/dxgi.dll ]] || { say "  !! $d/dxgi.dll missing — is DXVK installed?"; exit 1; }
  mv -f "$d/dxgi.dll" "$d/dxgi-dxvk.dll"
  cp "$src" "$d/dxgi.dll"
  say "installed wrapper in $d ($(stat -c %s "$d/dxgi.dll") bytes), DXVK's kept as dxgi-dxvk.dll"
done

WINEPREFIX=$PREFIX timeout 30 wineserver -k >/dev/null 2>&1
say "done. State now:"
show
#!/usr/bin/env bash
# Install/uninstall the D3D12 12_0 capability override into the suite's Wine prefix.
# Usage: ./install.sh [install|uninstall|--uninstall]
#
# The prefix comes from $PREFIX; when unset it is read from config/apps.local.env
# (git-ignored) at the repo root, then falls back to the default from
# config/apps.example.env.
set -euo pipefail

ACTION="${1:-install}"
case "$ACTION" in
  --uninstall|-u|uninstall) ACTION="uninstall" ;;
  install) ;;
  *) echo "Usage: $0 [install|uninstall/--uninstall]" >&2; exit 2 ;;
esac

HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
if [[ -z ${PREFIX:-} ]]; then
  cfg="$HERE/../../config/apps.local.env"
  [[ -f $cfg ]] && . "$cfg"
fi
PREFIX="${PREFIX:-${WINEPREFIX:-$HOME/.local/share/wineprefixes/suite}}"
SYS32="$PREFIX/drive_c/windows/system32"

if [ ! -d "$SYS32" ]; then
  echo "system32 not found: $SYS32" >&2
  exit 1
fi

for dll in d3d12 d3d12core; do
  if [ "$ACTION" = "install" ]; then
    if [ ! -f "$SYS32/$dll.dll.old" ] && [ ! -f "$SYS32/$dll.dll.old_none" ]; then
      if [ -f "$SYS32/$dll.dll" ]; then
        cp -v "$SYS32/$dll.dll" "$SYS32/$dll.dll.old"
      else
        touch "$SYS32/$dll.dll.old_none"
      fi
    fi
    cp -vf "$HERE/x64-override/$dll.dll" "$SYS32/$dll.dll"
    if command -v wine >/dev/null 2>&1; then
      WINEPREFIX="$PREFIX" wine reg add 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v "$dll" /d native /f >/dev/null 2>&1 || true
    fi
  else
    if [ -f "$SYS32/$dll.dll.old" ]; then
      rm -f "$SYS32/$dll.dll"
      mv -v "$SYS32/$dll.dll.old" "$SYS32/$dll.dll"
    elif [ -f "$SYS32/$dll.dll.old_none" ]; then
      rm -f "$SYS32/$dll.dll" "$SYS32/$dll.dll.old_none"
    fi
    if command -v wine >/dev/null 2>&1; then
      WINEPREFIX="$PREFIX" wine reg delete 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v "$dll" /f >/dev/null 2>&1 || true
    fi
  fi
done
echo "Done: $ACTION"

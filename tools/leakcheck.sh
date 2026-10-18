#!/usr/bin/env bash
# leakcheck.sh — refuse to publish product or vendor names.
#
# Term list: config/forbidden-terms.local.txt (local only, never committed). One Perl regex per
# line, case-insensitive; a line starting with "(?c)" is matched case-sensitively (for short
# abbreviations that would otherwise collide with ordinary words).
#
# Usage: tools/leakcheck.sh [paths...]   default: every staged/tracked file plus untracked
#        files that are not ignored.  Exit 1 and list file:line:match on any hit; checks both
#        file contents and file paths.
set -u
root=$(git rev-parse --show-toplevel 2>/dev/null) || { echo "not in a git checkout" >&2; exit 2; }
terms="$root/config/forbidden-terms.local.txt"
[[ -f $terms ]] || { echo "leakcheck: $terms missing — cannot verify, refusing" >&2; exit 2; }
cd "$root"
if (($#)); then mapfile -t files < <(printf '%s\n' "$@")
else mapfile -t files < <( { git ls-files; git ls-files --others --exclude-standard; } | sort -u); fi
hits=0
while IFS= read -r line; do
  [[ -z $line || $line == \#* ]] && continue
  flags=-iP; re=$line
  if [[ $line == '(?c)'* ]]; then flags=-P; re=${line#'(?c)'}; fi
  for f in "${files[@]}"; do
    [[ -f $f ]] || continue
    if printf '%s\n' "$f" | grep -q $flags -- "$re"; then echo "PATH  $f  ~ $re"; hits=$((hits+1)); fi
    out=$(grep -nI $flags -o -- "$re" "$f" 2>/dev/null | head -5)
    [[ -n $out ]] && { while IFS= read -r o; do echo "$f:$o"; done <<<"$out"; hits=$((hits+1)); }
  done
done <"$terms"
if ((hits)); then echo "leakcheck: $hits hit group(s) — fix before committing" >&2; exit 1; fi
echo "leakcheck: clean (${#files[@]} files)"

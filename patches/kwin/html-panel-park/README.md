# lcs-html-panel-park (KWin script)

Parks the PDF editor's stuck HTML panel ("Loading…", ~808x768, captionless) offscreen
so the document window underneath is usable. `TARGET_CLASS` in
`contents/code/main.js` must be set to the target app's lowercase exe name — it is the
window's KWin resource class. The matcher deliberately does **not** skip dialog
windows: KWin reports the stuck overlay as `dialog=true`.

## Install

```sh
dir=${XDG_DATA_HOME:-$HOME/.local/share}/kwin/scripts/lcs-html-panel-park
mkdir -p "$dir"
cp -r metadata.json contents "$dir/"
kwriteconfig6 --file kwinrc --group Plugins --key lcs-html-panel-parkEnabled true
qdbus6 org.kde.KWin /KWin reconfigure   # or log out and back in
```

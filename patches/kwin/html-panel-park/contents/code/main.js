// lcs-html-panel-park — park the PDF editor's stuck HTML panel overlay instead of
// letting it cover the document window.
//
// The PDF editor's embedded Chromium (CEF 116) view opens a captionless window that
// stays on "Loading…" forever under Wine: its browser and renderer do start (the CEF
// cache is written), but the view never presents. Everything else works — moving this
// one window aside shows a fully functional editor that renders documents, prints,
// annotates and so on.
//
// Matched conservatively: only windows whose resource class is TARGET_CLASS, whose
// caption is empty, and which are the size of the known stuck window (808x768).
// Nothing else is touched.

const TARGET_CLASS = "pdf.exe"; // set to the PDF app's lowercase exe name

const parked = [];

function isStuckOverlay(w) {
    if ((w.resourceClass || "").toLowerCase() !== TARGET_CLASS) return false;
    if (String(w.caption).length > 0) return false;
    const g = w.frameGeometry;
    // NOTE: do NOT require w.dialog to be false. KWin reports this stuck overlay as
    // dialog=true (measured: `|pdf.exe|dialog=true|800x738+136+0`), so a
    // `if (w.dialog) return false` check makes the plugin skip the one window it
    // exists to park, and the editor stays covered by "Loading…" forever. The class +
    // empty-caption + size match below is already specific enough to be safe on its own.
    return Math.abs(g.width - 808) <= 40 && Math.abs(g.height - 768) <= 40;
}

function park(w) {
    if (!isStuckOverlay(w)) return;
    if (parked.indexOf(w.windowId) !== -1) return;
    parked.push(w.windowId);
    w.frameGeometry = { x: 3000, y: 0, width: w.frameGeometry.width, height: w.frameGeometry.height };
    print("lcs-html-panel-park: parked overlay " + w.frameGeometry.width + "x" + w.frameGeometry.height);
}

print("lcs-html-panel-park: loaded, scanning " + workspace.windowList().length + " windows");
workspace.windowAdded.connect(park);
for (const w of workspace.windowList()) park(w);

# Benchmarks: real workloads under Wine

What was attempted for real-workload numbers on this machine, what came out of it, what is
blocked, and what the next run should do. Nothing here is a published-benchmark score: no run
produced a verified artifact yet, and that is stated per attempt below.

## Summary

| attempt | result | blocker |
|---|---|---|
| A third-party creator benchmark, run from its bundled projects | **not usable here** | Its projects are an old project-format version; the editor's "convert project" step hung for **more than seven hours** under Wine. Its measurement pipeline also cannot be reproduced locally (see below). |
| A self-built video-editor workload from the media already installed | **planned, not executed** | Its scripting entry point needs an installed panel runtime (no command-line script launcher convention); the UI route works but is manual. Plan below. |
| A self-built `compositor` workload, built and rendered by the app's own script engine | **built, saved and queued; render unverified** | `render()` returned after **26 387 ms** but no output file was found afterwards. Details and next steps below. |
| The `compositor-cli` command-line renderer | **blocked** | Hangs after GPU init under Wine: no frames in more than 10 minutes. |

Two rules kept the results honest: every timing unit is stated with what it includes, and no
artifact is called a success until a file exists and can be read back.

## 1. The third-party creator benchmark cannot run here

A well-known third-party creator benchmark for this class of apps exists, and its project files
are installed on this machine. Two independent reasons it is not usable:

1. **The bundled projects are an old project-format version.** Opening one in the current editor
   starts a conversion, and that step **hung for more than seven hours** under Wine without
   completing. The conversion path is therefore unusable here.
2. **Its measurement pipeline is not reproducible locally.** It drives the app through its own
   preset library, discards the encode to time processing only, and depends on its own
   automation and reporting. Even with a project that opened, our runs would not produce
   comparable numbers.

What is usable from it: **the media set** (4K DNxHR LB and SQ, ProRes 422, a high-bitrate H.264
file, a proxy and a 480p source, plus a still image used as a noise overlay) and the rough
workload shape. Those are used as sources for the plans below; the benchmark's own scores are
out of reach and this repo never claims them.

## 2. `compositor`: a scripted workload, built and rendered

The compositor is the only app here with a working command-line **and** script entry point, so
it was the first real workload.

### The scripting wall (and its fix)

An app script cannot write files until the scripting preference **"allow scripts to write files
and access network"** is enabled. Until then the engine runs the script happily and every file
write fails, and — because the app itself is not allowed to write — no log file can record that.
The preference is exposed to scripts as
`app.preferences.getPrefAsBool("Main Pref Section", "Pref_SCRIPTING_FILE_NETWORK_SECURITY")`.
Enable it in the scripting preferences section, or flip the stored value **from 0 to 1** in the
app's preferences text file while the app is closed, then restart the app. The script below
checks it and aborts with an explicit message when it is off.

Second wall, smaller: **paths in this engine use double backslashes** (`"C:\\bench"`), as in the
script below.

### Result

With the preference on and double-backslash paths, the script below:

* created a 1280x720, 10 s, 30 fps composition with **40 full-frame solid layers**, each carrying
  a **radial blur** whose amount steps up per layer;
* saved the project file, and verified the file exists and is non-empty;
* queued the composition in the render queue and set its output file;
* called the render-queue renderer, which returned after **26 387 ms**.

**The render result is UNVERIFIED: no output file was found afterwards.** Possible causes, in
the order worth checking: the queue item's output path/module was not what the script believed
it set; the renderer wrote to a different path (the queue keeps its own output path per item);
the render was refused (for example a full disk) and the scripting engine returned without an
error; or the output module needs to be run from the UI to finalize a file. The log file is the
only witness for all of these, which is why the script below reads the queue item's output path
back after rendering and verifies the artifact file before declaring success.

### The build/render script

Generic, no product names; the scripting API identifiers are the app's own. Save as
`bench-build.jsx` and run it through the compositor's script entry point (see the command after
the script).

```jsx
/*
 * bench-build.jsx — one fixed workload: build, save, queue, render, verify.
 *
 * Run through the compositor's app-scripting entry point:
 *     <app exe> -r 'C:\\path\\to\\bench-build.jsx'
 *
 * Workload: one 1280x720, 10 s, 30 fps composition with 40 full-frame solid layers,
 * each carrying a radial blur whose amount increases per layer. Cheap to build,
 * expensive enough to render to be a repeatable timing unit.
 *
 * Before the first run:
 *   1. The scripting preference "allow scripts to write files and access network"
 *      must be ON, otherwise every write below fails silently. Enable it in the
 *      scripting preferences, or flip its stored value from 0 to 1 in the app's
 *      preferences text file while the app is closed, then restart. Step 2 below
 *      aborts with an explicit message when it is off.
 *   2. Paths in this engine use DOUBLE BACKSLASHES, as everywhere below.
 *   3. The output directory must be writable by the app.
 *
 * The log is append-only across runs and carries a START marker per run; every
 * step logs BEGIN/OK/FAIL so a failure names its exact step.
 */
(function () {
    var OUT_DIR      = "C:\\bench";
    var PROJECT_PATH = OUT_DIR + "\\bench-project";
    var OUT_PATH     = OUT_DIR + "\\bench-render.mov";
    var LOG_PATH     = OUT_DIR + "\\bench-build.log";

    var COMP_NAME = "BENCH";
    var WIDTH = 1280, HEIGHT = 720, FPS = 30, SECONDS = 10;
    var SOLIDS = 40;

    function errorText(e) {
        var s = "<unprintable error>";
        try { s = String(e); } catch (ignore) {}
        try { if (e && e.line) s += " line=" + e.line; } catch (ignore) {}
        try { if (e && e.fileName) s += " file=" + e.fileName; } catch (ignore) {}
        return s;
    }

    /* Append one line. On failure the exact File.error goes to standard output as a
       second channel, so a silent preference/permission problem is still visible. */
    function log(message) {
        var line = (new Date()).toUTCString() + " | " + message;
        var f = new File(LOG_PATH);
        try {
            f.encoding = "UTF-8";
            if (f.open("a") !== true) { $.writeln(line + " | LOG OPEN FAILED | File.error=" + f.error); return false; }
            var wrote = f.write(line + "\r\n");
            var err = f.error;
            f.close();
            if (wrote !== true) { $.writeln(line + " | LOG WRITE FAILED | File.error=" + err); return false; }
            return true;
        } catch (e) {
            var fileError = "";
            try { fileError = f.error; } catch (ignore) {}
            $.writeln(line + " | LOG EXCEPTION | " + errorText(e) + " | File.error=" + fileError);
            try { if (f.opened) f.close(); } catch (ignore) {}
            return false;
        }
    }

    function step(name, fn) {
        log("BEGIN | " + name);
        try {
            var value = fn();
            log("OK | " + name + (value === undefined ? "" : " | " + String(value)));
            return value;
        } catch (e) {
            log("FAIL | " + name + " | CAUGHT | " + errorText(e));
            throw new Error(name + " | " + errorText(e));
        }
    }

    function ensureFolder(folder) {
        if (folder.exists) return "exists " + folder.fsName;
        var parent = folder.parent;
        if (parent && parent.fsName !== folder.fsName && !parent.exists) ensureFolder(parent);
        var created = folder.create();
        if (!folder.exists) {
            throw new Error("Folder.create returned " + created + " but " + folder.fsName +
                " is still missing; Folder.error=" + folder.error);
        }
        return "created=" + created + " " + folder.fsName;
    }

    /* The artifact check that the first run was missing. */
    function artifact(label, file) {
        if (!file.exists) { log("ARTIFACT MISSING | " + label + " | " + file.fsName +
                                " | File.error=" + file.error); return false; }
        if (file.length <= 0) { log("ARTIFACT EMPTY | " + label + " | " + file.fsName +
                                    " | bytes=0"); return false; }
        log("ARTIFACT OK | " + label + " | " + file.fsName + " | bytes=" + file.length);
        return true;
    }

    function two(n) { return n < 10 ? "0" + n : String(n); }

    function main() {
        log("========== START bench-build ==========");
        log("engine fs=" + File.fs);

        step("scripting preference: allow scripts to write files and access network", function () {
            var enabled = app.preferences.getPrefAsBool(
                "Main Pref Section", "Pref_SCRIPTING_FILE_NETWORK_SECURITY");
            if (enabled !== true) {
                throw new Error("Preference is off. Enable 'allow scripts to write files and " +
                    "access network' in the scripting preferences (or set the stored value to 1 " +
                    "in the app's preferences text file with the app closed), restart, re-run.");
            }
            return "Pref_SCRIPTING_FILE_NETWORK_SECURITY=true";
        });

        step("ensure output directory", function () { return ensureFolder(new Folder(OUT_DIR)); });

        var project = step("new project", function () {
            if (app.newProject() === false) throw new Error("app.newProject returned false");
            if (!app.project) throw new Error("app.project is null after newProject");
            return app.project;
        });

        var comp = step("create composition " + WIDTH + "x" + HEIGHT + " " + SECONDS + "s " + FPS + "fps",
            function () { return project.items.addComp(COMP_NAME, WIDTH, HEIGHT, 1, SECONDS, FPS); });

        var i;
        for (i = 0; i < SOLIDS; i++) {
            (function (index) {
                step("solid + radial blur " + two(index + 1) + "/" + SOLIDS, function () {
                    var color = [0.5 + 0.5 * index / SOLIDS, 0.3, 1.0 - 0.5 * index / SOLIDS];
                    var layer = comp.layers.addSolid(color, "S" + index, WIDTH, HEIGHT, 1, SECONDS);
                    layer.startTime = 0;
                    layer.inPoint = 0;
                    layer.outPoint = SECONDS;
                    var effects = layer.property("Effects");
                    if (!effects) throw new Error("layer has no Effects property group");
                    var blur = effects.addProperty("Radial Blur");
                    if (!blur) throw new Error("could not add Radial Blur");
                    var amount = blur.property("Amount");
                    if (!amount) throw new Error("Radial Blur has no Amount property");
                    amount.setValue(15 + index);
                    return "layer=" + layer.name + " amount=" + (15 + index);
                });
            })(i);
        }

        step("open composition in viewer", function () { comp.openInViewer(); return COMP_NAME; });

        step("save project", function () {
            var target = new File(PROJECT_PATH);
            if (!target.parent.exists) throw new Error("parent folder missing: " + target.parent.fsName);
            project.save(target);
            if (!target.exists || target.length <= 0) {
                throw new Error("project file missing or empty after save: " + target.fsName +
                    " | File.error=" + target.error);
            }
            return "saved=" + target.fsName + " bytes=" + target.length;
        });

        step("queue in render queue and set output file", function () {
            var rq = project.renderQueue;
            if (rq.items.length !== 0) throw new Error("render queue is not empty: " + rq.items.length);
            var item = rq.items.add(comp);
            var om = item.outputModule(1);
            om.file = new File(OUT_PATH);
            var back = om.file;
            if (String(back.fsName) !== OUT_PATH) throw new Error("output path did not stick: " + back.fsName);
            return "queued=" + item.name + " output=" + back.fsName;
        });

        step("render", function () {
            var t0 = (new Date()).getTime();
            app.project.renderQueue.render();
            var ms = (new Date()).getTime() - t0;
            log("RENDER RETURNED | elapsed_ms=" + ms);
            return "elapsed_ms=" + ms;
        });

        /* Read the queue's own idea of the output path back, then verify the file.
           The first run on this machine stopped at the timing and found nothing. */
        step("verify render artifact", function () {
            /* Logging only: a read-back that throws must not hide the artifact
               check below, which is the verdict. */
            try {
                var rq = app.project.renderQueue;
                var n;
                for (n = 1; n <= rq.items.length; n++) {
                    var item = rq.items[n];
                    var status = "";
                    try { status = String(item.status); } catch (ignore) {}
                    log("QUEUE ITEM | n=" + n + " name=" + item.name + " status=" + status);
                    var om = item.outputModule(1);
                    if (om && om.file) log("QUEUE OUTPUT | n=" + n + " file=" + om.file.fsName);
                }
            } catch (readBackError) {
                log("WARN | queue read-back failed | " + errorText(readBackError));
            }
            if (!artifact("render", new File(OUT_PATH))) {
                throw new Error("no readable render artifact at " + OUT_PATH +
                    " - compare it with the QUEUE OUTPUT line above");
            }
            return "verified " + OUT_PATH;
        });

        log("========== BUILD COMPLETE ==========");
    }

    try {
        main();
    } catch (fatal) {
        log("========== BUILD ABORTED | CAUGHT | " + errorText(fatal) + " ==========");
        $.writeln("[bench-build] ABORTED | " + errorText(fatal));
    }
})();
```

Run it:

```bash
wine "$PREFIX/drive_c/<path to the compositor exe>" -r 'C:\bench\bench-build.jsx'
```

`C:\bench` is the script's own output directory; change `OUT_DIR` at the top of the script as
needed (double backslashes in the JS source). Timing source: the `RENDER RETURNED | elapsed_ms=`
line in `bench-build.log` — it measures the render call as the app sees it, not the process's
wall clock.

### Next steps for this workload (in order)

1. Re-run with the artifact check above; if the file is still missing, the `QUEUE OUTPUT` line
   shows which path the queue actually holds, and the fix follows from that.
2. Prove the pipeline with a 1-frame, 1-layer composition first, so a failure cannot be an
   effect problem, then scale up layer count.
3. Compare `render()`'s `elapsed_ms` with the shell's wall clock (`/usr/bin/time -f '%e'`) plus
   the render-time total the command-line renderer prints, so the reported number is not just
   the app's internal view.
4. Only after a verified artifact exists: use this as the repeatable local baseline
   (layer-count or effect-amount sweeps), and record the machine and driver versions with it.

## 3. `compositor-cli`: blocked

The command-line renderer of the same suite **hangs after GPU init** under Wine: no frames in
more than 10 minutes. It is not a slow render, it is a stall. Next things to try: force the
software-only render path for the project, run it with a headless/`-s`-style flag if the build
has one, cap every invocation with `timeout`, and compare against a 1-frame project to separate
"GPU init" from "frame rendering".

The intended invocation shape (parameters, not a verified run):

```bash
timeout 900 /usr/bin/time -f 'wall=%e' \
  wine "$PREFIX/drive_c/<path to the command-line renderer>" \
    -project 'C:\bench\bench-project' \
    -comp 'BENCH' \
    -output 'C:\bench\cli-out.mov' \
    -v ERRORS_AND_FRAMES \
    -log 'C:\bench\cli-render.log'
```

`-v ERRORS_AND_FRAMES` asks for per-frame timing records in the log (progress lines shaped like
`PROGRESS: 0:00:00:00 (1): 2 Seconds`); the renderer's own total is the `PROGRESS: Total Time
Elapsed: N Seconds` line when the build prints it. If it does not, the `/usr/bin/time` wall
clock is the fallback. The render-settings template may not select an output codec — if the
default output module does not match the chosen extension, select an installed output-module
template explicitly.

## 4. Video-editor workload (planned)

A real-workload run for the non-linear editor, using the media already installed (the benchmark's
media set from section 1) rather than any bundled project:

1. Create a **new** project **inside the current version of the app** (never open an old-format
   project — that is the >7 h hang). Save it as a project file first.
2. Import the sources: 4K DNxHR LB, 4K DNxHR SQ, 4K ProRes 422, a high-bitrate 4K H.264 file, a
   ProRes proxy and a 480p DNxHR, plus the still grain image. Do not mix codecs in one sequence;
   one source clip per sequence keeps decoder cost interpretable.
3. Build one sequence per source clip ("new sequence from clip"), and one separate UHD sequence:
   the 480p clip scaled up with the still grain overlay above it — an encode-bound case rather
   than a decode-bound one.
4. Export each sequence with the same range (e.g. the first 10 seconds), H.264 / "match source -
   high bitrate", through the app's **own Export** action — not by queueing to the batch encoder,
   which adds a second process and a second app to the timing path. Record the exact preset label
   the UI shows.
5. Time from clicking Export until the app reports completion and the output file stops growing.
   Keep the project file, every exported file, their byte sizes and the selected renderer.
6. Verify each export from the host: `ffprobe -v error -count_frames -show_entries
   format=duration,size:stream=codec_name,width,height,avg_frame_rate,nb_read_frames -of json
   <file>` plus a full decode (`ffmpeg -i <file> -f null -`). Throughput = frames / export
   seconds.

Two honest caveats for this plan. First, the scripting route needs an installed panel runtime;
the editor has no `-r`-style script launcher convention like the compositor, so either that
runtime works under Wine (unmeasured) or the build is done through the UI
([automation.md](automation.md) covers driving it). Second, no GPU decode or GPU-effects path has
been established under Wine on this machine: expect software decode and software rendering, and
label the results as such. A run is only comparable with another run that used the same
renderer, preset, range and machine.

## What is still missing

* No **verified** render artifact from any app yet (sections 2-3).
* No **playback** measurements (dropped frames, scrub latency) — the apps' UI responsiveness
  under synthetic input is measurable with the harness in [automation.md](automation.md), but no
  such run has been scoped to a fixed workload.
* No **cross-machine comparison**; the numbers are from this machine only, and the machine
  context (Wine 11.19, Mesa 26.2, panel-driven iGPU) belongs with every result.

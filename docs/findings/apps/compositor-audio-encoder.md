# Compositor, audio editor, encoder, and puppet animator

## Problem

Several startup dialogs are difficult to see or answer because they never composite above the splash, appear late, or are followed by secondary panels. Separate findings concern a bundled tracking plugin that fails to load and an imprecisely reported workspace-preferences parse error.

## Measured

The compositor was sampled with an unchanged splash at 90, 180, 270, 360, and 450 seconds and a status indicating that the media engine was initializing. A raw window-tree inspection found three modal dialogs that were not visible in the captured image. Raising the windows and sending Return/Escape repeatedly dismissed them; the compositor reached its workspace, which measured 0.971 lit in that run. A later audit measured 0.8657 with the project and Tip of the Day visible, and recorded a plugin warning followed by additional dialogs as late as about 90 seconds after launch. Keep polling rather than treating the first splash as a hang.

After repeated forced stops, the compositor also showed a crash-recovery prompt. Choosing Continue opened its normal project workspace. The bundled third-party tracking plugin produces a load warning. Its supporting Qt, compression, crypto, and PNG dependencies were made available through existing app paths plus the [PNG forwarder](../../../patches/shims/png-forwarder/), but the plugin's own entry point still returned error 317 (`ERROR_MOD_NOT_FOUND`). The plugin remains unavailable; it is not required to reach the main workspace.

The audio editor showed an error-report dialog in an early research run. A later interactive audit instead recorded a crash-recovery prompt followed by a Learn panel; after selecting “Later” and closing that panel, the main interface measured 0.9981 lit. These are observations from different runs, not proof that the earlier dialog and later prompt share a cause.

The encoder's interface measured 0.4421 lit and was identified as intentionally dark, not as a rendering failure. The puppet animator measured 0.9870 and its file-import interaction was verified. It needs the D3D12 capability configuration described in [graphics findings](../graphics/d3d12-feature-level.md).

An early startup report for the encoder and puppet animator described a properties-list parse error followed by termination. The exact dialog is reported as “Error parsing properties list from memory.” Searches and prefix inspection found no corresponding disk file; “from memory” refers to an in-memory property list. Related on-disk workspace state is XML, and the encoder stores preferences and saved workspaces under the user's Documents directory. The current audit shows both apps reaching their workspaces, so the early termination is not the current measured outcome.

## Tried

For the compositor, the useful distinction was between a frozen splash image and unanswered modal dialogs. Window raising plus Return/Escape cleared the invisible dialogs; later startup checks continued long enough to catch late prompts. A separate crash-recovery prompt was answered with Continue.

For the tracking plugin, its dependency locations were supplied and a generic PNG export forwarder was added. The remaining entry-point failure shows that dependency resolution alone was insufficient.

The workspace-error investigation compared the wording with published workspace-reset reports and checked for a literal config file. It did not identify a file path, a malformed XML document, or a Wine parser failure for the current prefix. The XML-serializer compatibility work in Wine Staging is related background, not evidence that this was the cause; see [the native XML runtime finding](../runtime/msxml3.md).

## Outcome

The startup dialogs are answerable, and the compositor, audio editor, encoder, and puppet animator all reach a usable interface in the latest audit. The compositor's late prompts and the tracking plugin warning remain operational nuisances. The plugin itself still fails to load. The workspace parse error remains an earlier, unreproduced symptom rather than a confirmed current blocker. See [suite status](../../status.md).

## Open questions

- What causes the compositor's three unseen startup modals, and why do some additional prompts appear much later?
- What does the bundled tracking plugin's entry point fail to find after its visible dependencies resolve?
- Did the current encoder and puppet animator reach their workspaces because their workspace state changed, because of the XML runtime selection, or for another reason?
- Can the exact workspace dialog and any relevant state-file path be captured on a current run?
- Are the early audio error-report dialog and the later recovery prompt related? No evidence currently connects them.

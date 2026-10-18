# Native XML runtime loading under Wine

## Problem

The native 64-bit XML runtime installed by the prefix fails during `DllMain` on Wine 11.19. This is a loader/runtime problem, not a registry setting. The separate “properties list from memory” report may involve XML state, but no causal link to this load failure was demonstrated.

## Measured

The prescribed load probe failed 10/10 times in a fresh prefix on plain Wine 11.19. The same failure reproduced under Wine Staging 11.19. Two independent causes were traced:

1. The 64-bit runtime looks for a matching 64-bit resource DLL beside itself. The installed set contains only the 32-bit resource DLL; using a mismatched resource DLL does not satisfy the check.
2. After that resource check is satisfied in an isolated experiment, the runtime's own bookkeeping indexes a static table with the high bits of a Wine heap pointer. Wine returned addresses around 140 TB, producing an out-of-bounds table access and an access violation at a fixed offset inside the runtime.

Registry bisects did not affect the load result. A matching resource DLL alone and a test-only neutralization of the high-bit indexing alone each remained insufficient; both were needed in the isolated proof. No modified vendor binary is published.

## Tried

The applied prefix workaround selects Wine's builtin XML runtime for `encoder` and `puppet` through per-app DLL overrides. In a clean test prefix, the equivalent app-default override passed 3/3 runs after the no-override control failed 4/4. The app-named helper probe in the shared prefix was affected by concurrent prefix use, so it is not treated as definitive app validation. The current audit records both apps reaching their interfaces, but does not isolate the override as the only cause.

The broader properties-list investigation found that “from memory” describes an in-memory workspace/layout property list, while related disk state is XML. It found no file matching the reported `.cfg` premise. Wine Staging MR 10025 adds XML serializer behavior and compatibility for embedded XML declarations; it is closed and unmerged upstream and was reported in Wine Staging 11.1. No test tied that patch to the current properties-list dialog.

## Outcome

The prefix uses Wine's builtin XML runtime for the two affected apps. Native 64-bit loading is not repaired by registry changes and remains incompatible with the tested Wine builds. The workspace parse report is a separate, unresolved question; no published shim addresses this native XML runtime load failure.

## Open questions

- Can Wine's heap-address interaction with the native runtime be fixed upstream without modifying the vendor binary?
- Which supported Windows package supplies the exact matching 64-bit resource DLL for this runtime version?
- Does the builtin runtime parse the current workspace state successfully, or did the early dialog have another source?
- Will the XML serializer changes from Wine Staging 11.1 be upstreamed, and do they affect this suite's workspace data?

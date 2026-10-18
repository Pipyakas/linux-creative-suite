# DXGI and DXCore shims

## Problem

Early graphics notes reported zero outputs and zero devices. The output count was wrong: a probe used hand-written interface slot numbers that did not match the actual DXGI vtable. Separately, Wine's built-in DXCore factory returned an empty adapter list, and DXVK adapters did not expose the DXCore interfaces expected by some callers.

## What was measured

A corrected DXGI probe enumerated two adapters. D3D11 device creation succeeded at feature level 11_0 on both, and a compute shader dispatched and returned the expected values. DXVK itself already reported one attached output: 1366×768, seven display modes, and 10 bits per color channel.

The zero-output report came from the old probe's incorrect vtable offsets. The corrected probe used the declared interfaces and was cross-checked against the running DXVK implementation. The result was the same with DXVK 2.4.1.

The built-in Wine DXCore probe returned E_INVALIDARG and zero adapters. A native DXCore shim returned S_OK and two adapters in both 64-bit and 32-bit probes. It answered the adapter properties and memory-budget queries needed by those probes.

## What was tried

| Experiment | Result |
|---|---|
| Correct DXGI output enumeration | One real attached output found; the old zero-output result was a probe artifact |
| DXCore shim implementing a DXGI-backed adapter list | Probe changed from zero to two adapters in both architectures |
| DXGI wrapper adding DXCore and Wine adapter interfaces | Its acceptance probe passed and D3D11 stayed functional, but DXVK hangs when its DLL is renamed or loaded through the wrapper path |
| Synthetic DXGI output for adapters with no output | Did not change the app's counters; tracing showed the app never calls EnumOutputs |
| DXVK in-place adapter-interface patch | Removed the failed QueryInterface checks and allowed adapter matching, but did not by itself produce an accepted app device |
| Adapter identity spoofing | Did not change the zero-device result in those tests |
| vkd3d-proton feature-level override | Changed the GPU probe to one device; see [D3D12 feature level](d3d12-feature-level.md) |

The probe and wrapper work is represented under [shim patches](../../../patches/shims/). The DXVK maintenance and adapter experiments are under [DXVK patches](../../../patches/dxvk/).

## Outcome

Do not treat the app's “zero displays” counter as a DXGI output count. DXVK exposes the attached panel through DXGI; the counter reflects a later device-acceptance path. The DXCore shim remains installed in the recorded prefix because it replaces Wine's empty-list stub and reports both adapters to direct DXCore probes. It did not, by itself, make raster or puppet accept a GPU.

The DXGI wrapper was not kept in the live prefix. DXVK needs its module to be named dxgi.dll; moving it behind a wrapper makes factory creation hang. The in-place DXVK adapter patch was reverted. The separate feature-level override changes the GPU-probe result, but raster still has no window.

## Open questions

The final application-side GPU acceptance logic is not fully understood. The evidence includes a failed adapter query, a later feature-level refusal, and a successful GPU-probe result under the vkd3d-proton override, while raster still exits. More tracing is needed to determine which check remains for that app.

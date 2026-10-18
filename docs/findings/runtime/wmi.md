# WMI unsecured-apartment shim

## Problem

Wine did not register the COM class used by two background services for an unsecured WMI apartment. When those services were enabled, class activation failed and one service then dereferenced a null pointer. The missing class is separate from the `raster` startup exit.

## Measured

The local implementation supplies an in-process COM server, a local-server process, and a proxy/stub for the relevant interfaces. It registers both 64-bit and 32-bit views. With the services temporarily enabled, the shim removed the class-registration errors and page fault: the comparison was 0 errors and 0 page faults with the shim, versus two activation errors and one page fault without it.

The dedicated probe succeeded for in-process and out-of-process activation in both bitnesses, including marshaling the custom interfaces across process boundaries. The working `layout` app remained running with the shim installed. A 14,421-line `raster` OLE trace contained zero requests for this class.

The implementation intentionally provides activation and marshaling only. It does not implement WMI queries or maintain WMI state.

## Tried

The shim at [wmi-unsecapp](../../../patches/shims/wmi-unsecapp/) was built for both bitnesses, registered in both registry views, and tested with the dedicated probe. The background services are disabled in the normal prefix, so the null-dereference path does not run during ordinary startup.

For `raster`, launches with and without the shim had the same clean exit. Its trace requested other WMI classes but never the unsecured-apartment class, so this shim cannot explain or fix its exit.

## Outcome

The shim fixes the missing COM activation and marshaling path required by the background services. It does not provide WMI semantics, and the successful probe does not establish that arbitrary WMI clients will work. `raster` does not use the class on the measured startup path.

## Open questions

- Does any caller need real WMI data or callbacks beyond activation and marshaling?
- Is there a Wine upstream implementation for the missing COM class, tracked under [Wine bug 30439](https://bugs.winehq.org/show_bug.cgi?id=30439)?
- Can the services remain disabled without affecting any app-specific behavior beyond the observed error path?

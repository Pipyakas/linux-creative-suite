# Licensing-client observations

## Problem

Startup logs show failed profile and access-token requests, while different apps continue, display a sign-in surface, or stop. The observed log messages do not by themselves explain the `raster` exit or establish the entitlement state of every app.

## Measured

The prefix writes a licensing-client log during app launches. Logs for `raster` include status 218 (“Data Not Found: Principal”) and a failed access-token request. The same status family appeared in logs from apps that reached their workspaces. The `pdf` log recorded an HTTP 200 response from one licensing endpoint, followed by a denied profile status and a sign-in workflow.

The inspected profile store contained empty per-user scaffolding and a small public device-identity record. The only entitlement registry data observed belonged to `pdf` and reported `NOVALUE` with subscription flag 0. A sign-in surface rendered with email and federated-provider choices, but completing sign-in was not tested.

The prefix had no dedicated licensing Windows service; the observed work was performed by helper processes and an in-process client. A separate update service was present.

## Tried

For `raster`, profile and token caches were cleared and the app was relaunched. Its clean status-0 exit and final allocator message were unchanged. The same licensing status family in working apps also means the status 218 line alone does not distinguish a failing startup from a successful one.

No account sign-in was performed, and no conclusion about service entitlement was inferred from the rendered sign-in surface.

## Outcome

The available evidence shows failed or denied profile requests and an incomplete sign-in state in this prefix. It does not show that these messages cause `raster` to exit; the same status family appears in working apps, and clearing local caches did not change the exit. Sign-in completion and resulting entitlement state remain unmeasured.

## Open questions

- Does a completed, authorized sign-in change the profile responses or any app's startup behavior?
- What licensing result does each app receive after the common profile request?
- Is the denied profile response for `pdf` associated with its visible embedded-browser overlay, or are those independent observations?

# Preserve in-flight security on split reconnect

Live diagnostics after Cube power cycling showed both halves' advertisements
and repeated attempts, with historical failure at phase 2 (security). The latest
generic error was 520; this field mixes connect/encryption/disconnect results and
does not prove the original encryption failure. NVS persistence is already
enabled in the dual-dongle board variant; do not erase bonds to investigate.

The connection handler previously terminated on every nonzero return from
`ble_gap_security_initiate`. Treat `BLE_HS_EALREADY` as an existing security
procedure and wait for its encrypted result under the existing 15-second timeout.
Other start errors, failed encryption and mismatching identities remain fatal.
No pairing or key validation is bypassed and no bonds are deleted.

`DONGLE SPLIT DIAG` now separately exposes `security_start` (last security start
return code) and `encryption_status` (last ENC_CHANGE status), initially -1.
These historical fields survive later disconnect codes, but are reset on boot.
They must be interpreted alongside current phase and increasing attempt counts.

Tests execute the production start-result branch with ASan/UBSan for success,
already-in-progress and fatal errors on both halves, and verify the encrypted
identity guard and timeout remain. ESP-IDF build and power-cycle hardware tests
are still required. This corrects a code-level race; the live failure's sole root
cause is not yet proven. No Actions build is triggered by the source commit.

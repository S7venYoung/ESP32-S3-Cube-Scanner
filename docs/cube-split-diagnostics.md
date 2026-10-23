# Read-only split reconnect diagnosis

Use `DONGLE SPLIT DIAG` on the normal runtime CDC port (115200). No reboot,
settings_reset or pairing erasure is needed to query it. Requires this firmware
update; earlier `DONGLE DIAG` only reports CPU/reset/heap information.

`L[...] R[...]` expose atomic diagnostic fields, no device addresses or keys:

- `phase`: 0 idle, 1 connecting, 2 security, 3 identity, 4 service,
  5 characteristic, 6 CCC, 7 initial read, 8 ready, 9 closing.
- `adv`: reports matching the stored identity, including those received during
  retry cooldown; `adv_age_ms=-1` means no matching report since boot.
- `attempts`: number of actual connection attempts since boot.
- `error`: latest connection-start/result, encryption status or disconnect
  reason. Interpret with phase and repeated snapshots; the code spaces differ.
- `failed_phase`: last phase passed through `Fail`, -1 if none. Historical value,
  not proof the current connection is failing. Querying never clears it.

Compare snapshots while the right half remains powered: no matching reports
points toward advertising/address/discovery; reports plus failed attempts toward
link establishment; failure at phase 2 toward security/identity restoration;
later failure toward GATT subscription/initial read. Counters do not establish
the underlying cause alone. No binding or authentication behavior is changed.

Host tests cover snapshot formatting, 32-bit uptime wrap and production wiring.
Full ESP-IDF compilation and reproduction on hardware are still required. The
source commit deliberately does not trigger Actions.

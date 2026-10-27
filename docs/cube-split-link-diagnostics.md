# Split connection/drop diagnostics (link_diag=2)

Read `DONGLE SPLIT DIAG` over the existing USB CDC channel. The response keeps
`CUBE-SPLIT-DIAG/1 L[...] R[...]` and all original fields, appending per-side data.
This is read-only: no reset, pairing deletion, parameter changes or new BLE task.

- `attempts`: initiating attempts (not drops); `connect_failures`: API rejection or
  unsuccessful CONNECT completion. These can increase without a link ever existing.
  `connect_fail_error/phase/age_ms` retain that failure after later success;
  `connect_fail_kind` is 1=initiation API rejection, 2=CONNECT completion error.
- `connections`: successful CONNECT events; `disconnects`: matching GAP DISCONNECT
  events only; `ready_drops`: those links had reached usable Ready before dropping.
- `last_drop_ms`, `drop_age_ms`, `drop_reason`, `drop_phase`: last real disconnect
  uptime, age, raw NimBLE reason and callback phase. Example 520=HCI timeout,
  534=local-host termination. They do not prove keyboard power loss versus radio loss.
- `link_age_ms`: current successful connection age; `ready_latency_ms`: latest
  CONNECT-to-Ready delay; `reconnect_ms`: first real disconnect through retries to
  next Ready, not just the last connection attempt. Unknown values print -1.
- `positions`, `position_age_ms`: accepted position-snapshot count/age, including
  the initial read. An idle keyboard need not send position data; an old age alone
  is not proof of a broken link. Key positions/contents are never recorded here.
- `local_closes`, `local_kind`, `local_phase`, `local_age_ms`: explicit local close
  requests (1=Fail/validation/security, 2=handshake timeout, 3=explicit forget).
  Not necessarily completed disconnects. A new connection clears current local_kind.
- `missing_disconnects`: terminate reported already disconnected, without counting
  it as an observed GAP DISCONNECT. This clears the diagnostic active-link flags.
- `host_resets`, `host_reset_reason`, `host_reset_age_ms`: NimBLE host resets,
  distinct from Cube hardware reboots and physical-link disconnects. Pair this
  with `DONGLE DIAG` for Cube reset reason and uptime.
- `drops={number:uptime_ms:reason:phase:local_kind;...}`: last eight physical-link
  disconnect events, oldest first. `busy` means a concurrently overwritten entry;
  read again later. Reads never clear counters/history.

Phase 0=Idle, 1=Connecting, 2=Security, 3=Identity, 4=Service, 5=Characteristic,
6=CCC, 7=Reading, 8=Ready, 9=Closing. A deliberate close often reports phase 9;
the local origin phase/cause distinguishes it from a spontaneous timeout.

All counters/history are volatile and reset on Cube reboot. Atomics avoid data
races across host and CDC tasks, but the entire diagnostic string is not one
transactional snapshot; adjacent fields can span neighboring events. Ages use
unsigned 32-bit millisecond subtraction and are meaningful within one wrap
(about 49.7 days). Historical error fields may remain after successful recovery.

Capture `DONGLE SPLIT DIAG`, `DONGLE HOST`, `DONGLE DIAG` soon after a drop. Do not
reset either half or erase bonds first. Hardware power, RSSI/interference and
keyboard-side reset causes cannot be inferred from Cube timeout logs alone.

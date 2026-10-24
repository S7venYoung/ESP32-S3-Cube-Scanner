# Computer BLE pairing and HID diagnostics

Scope: USB+BLE dongle branch only. The working split connection/bond recovery
implementation, keyboard firmware, GPIO assignments and release binary remain
unchanged. No automatic Actions or pairing erasure.

## Correct handling of an in-progress SMP exchange

The original computer CONNECT callback called `ble_gap_security_initiate()` and
terminated for **every** nonzero return. In the pinned IDF 6.1 NimBLE host,
`BLE_HS_EALREADY` means a security procedure is already active, not that pairing
has failed. The split path already handles this case separately.

Computer security is now initiated from the existing host timer no earlier than
100 ms after CONNECT. The current connection descriptor is checked again. An
already encrypted connection proceeds to the existing persistence/authorization
checks. Success/EALREADY waits for the encryption event; EBUSY/EAGAIN retries at
100 ms intervals within the existing 30 s deadline; other errors retain their
code and terminate. Disconnect and host reset cancel pending starts. All
identity, SC/16-byte key, profile ownership and encrypted HID gates remain.

This removes a concrete premature-disconnect path. It is not proof that it is
the only cause of the observed Mac pairing failure or resource error 6.

## Read-only status

`DONGLE HOST` keeps its original prefix/fields and appends `HOST-DIAG=2` with:

- LINK, ENCRYPTED, BONDED, AUTHORIZED: physical link versus usable trusted HID.
- SECURITY-PENDING: starting/waiting on authorization; KBD-SUB and BOOT-SUB:
  keyboard report notifications subscribed by the computer.
- ADV, PAIR-WINDOW, SLOT-BOUND: advertising and selected-slot state.
- REPORT-PROTOCOL and SUSPENDED explain an authorized but inactive HID path.
- SPLIT-LINKS counts ready halves only; MAX-LINKS is the configured host limit.
  These are **not** a measurement of free controller/L2CAP/GATT pool objects.
- ADV-STAGE: 1 advertising data, 2 scan response, 3 advertising start.
  ADV-RC distinguishes which call returned resource error 6.
- CONNECT-RC, SECURITY-RC, ENC-RC and DISCONNECT: separate latest event results.
  -1 means not observed in the current connection attempt (disconnect remains
  historical); counters CONNECTS/DISCONNECTS/REPEATS preserve attempt history.
- KEYS: valid stored local key bit 0 and peer key bit 1. No key bytes or
  Bluetooth identities are exported.
- AUTH-REASON: 0 authorized/idle; 1 waiting encryption; 2 identity/bond not ready;
  3 stored keys not ready; 4 computer already belongs to another slot; 5 selected
  slot belongs to another computer; 6 pairing window expired; 7 save failure;
  8 connection descriptor unavailable; 9 keyboard identity rejected as computer.

Diagnostic flags are one atomic snapshot from the BLE host queue. Counters and
last-event results are atomics and may span neighboring host events; do not
interpret them as an exact synchronized controller trace. Status construction
does not issue GAP commands or access keys/NVS from the serial task.

## Hardware acceptance still required

Use one selected slot and one Mac while capturing DONGLE HOST before/during/after
the Mac connection attempt. Do not erase bonds to conceal errors. Validate HID
typing, disconnect/reconnect, slot switching and Cube power cycling first with
one half, then both halves. Error 6 previously disappeared after changing to
slot 2 and powering off the right half; two variables changed, so connection
capacity is not yet proven to be the cause. New resource-stage diagnostics are
required before increasing pool sizes or altering existing split behavior.

## Actual resource-pool admission check

On the new build the live failure is `ADV-STAGE=3 ADV-RC=6`, with two ready
halves, no computer CONNECT event, and no active advertisement. The pinned
`ble_gap_adv_validate()` returns ENOMEM when `ble_hs_conn_can_alloc()` rejects
connectable advertising. That function checks three separate fixed pools:
connection >= 1, L2CAP channels >= 3, and GATT client configuration >= 1.
Free general heap and configured maximum connection count do not establish that
all three have room. No specific pool has yet been measured on the device.

The SDK's public `os_mempool_info_get_next()` now samples only those three named
pools on the BLE host queue after an advertising attempt, limited to once per
second and a maximum of 64 registry entries. CDC only reads atomic snapshots.
`POOL-CONN`, `POOL-L2CAP`, `POOL-GATT` report free/total blocks, not byte counts.
`POOL-BLOCKED` bits 1/2/4 identify exhausted connection/L2CAP/GATT pools against
the SDK's thresholds. `POOL-MISSING` bits report unobserved pools, never assumed
free or blocked; `POOL-SCAN-COMPLETE=0` indicates a truncated registry walk.
`NIMBLE-LIMIT` reports `MYNEWT_VAL(BLE_MAX_CONNECTIONS)` rather than the app's
CONFIG value so configuration/header mismatches are visible.

These values describe the latest sampled advertising attempt, not every later
controller event. No SDK/vendor file, pool size, scan schedule, identity,
encryption requirement, bond data or accepted split reconnect path is changed.
The next device capture must identify the failing pool before selecting the
configuration or lifetime repair. There is deliberately no guessed capacity
increase that could mask a leak or affect audio/internal RAM.

Primary source evidence (pinned SDK):
- [admission check](https://github.com/espressif/esp-nimble/blob/139cada0ae932957fa06ba37d17e3c9c2c95c773/nimble/host/src/ble_hs_conn.c)
- [public pool diagnostic API](https://github.com/espressif/esp-idf/blob/v6.1/components/bt/porting/include/os/os_mempool.h)

## Delayed CONNECT callback invalidated by an unsuccessful advertising retry

The device capture on slot 4 showed all pools exhausted (0/3 connection, 0/9
L2CAP, 0/4 GATT including the GATT cache), two ready halves, but LINK=0 and
CONNECTS=0 in the app. This is evidence of an allocated connection the computer
state machine has not adopted, not justification to add more connection slots.
Pool snapshots and ADV-RC are from neighboring host events and may differ.

The pinned `ble_gap_rx_conn_complete()` allocates/inserts a new peripheral
connection, clears advertising state, and starts remote version/feature exchange.
It only delivers the application's CONNECT from the later
`ble_gap_event_connect_call()`. During that window the host timer sees no active
advertisement and the app still has no computer handle, so it can retry
advertising. The original expression passed `++host_epoch` directly to
`ble_gap_adv_start()`, even if that call failed with ENOMEM. This invalidates the
callback cookie held by the SDK's already allocated connection. The delayed
CONNECT and later DISCONNECT are silently rejected by the app's epoch guard.

`HostEpoch` now keeps an active cookie plus a provisional advertising cookie.
The active cookie is replaced only when advertising actually starts. Failed
attempts cannot invalidate it. While the API is running, either active or
provisional callbacks are accepted (callbacks need not be assumed asynchronous).
Recursive starts are blocked. A host reset invalidates both without reusing
sequence values. Later successful sessions still reject stale cookies.

`DONGLE HOST` now includes `HOST-EPOCH=2`. Regression tests reproduce the old
comparison failure and cover 100 failed retries, delayed CONNECT acceptance,
callbacks during an advertising call, success invalidating the old session,
reentrant starts and reset cancellation. No pool sizes, bonding, security gates,
split connection logic or existing Release are changed. Hardware acceptance is
still needed with both halves and the Mac: pairing, HID typing, host cycling,
Cube power cycling, and confirming no untracked third connection remains.

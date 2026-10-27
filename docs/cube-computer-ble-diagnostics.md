# Computer BLE pairing and HID diagnostics

## Restored HID subscriptions and offline output switching (HID-RESTORE=1)

The slot-2 capture had LINK/ENCRYPTED/BONDED/AUTHORIZED=1 and both halves ready,
but KBD-SUB/BOOT-SUB/BLE-READY=0. A concrete lost-event path exists: IDF 6.1 delays
the application CONNECT until remote feature exchange, while encrypted reconnects
can restore CCCDs earlier. Previously those SUBSCRIBE events were ignored because
the application had no matching handle, and CONNECT unconditionally cleared flags.

The host queue now stages real WRITE/RESTORE notification/indication state in three
bounded per-handle slots. Events must belong to a live peripheral-role connection,
not a split identity, and match one of the registered HID/Studio value handles.
CONNECT consumes only its own staged state; unsubscribe, disconnect and host reset
prevent reuse. No notification is enabled without an actual SDK subscribe event.
Authorization, encryption, key persistence, profile ownership and neutral-input
barriers still gate all reports. This repairs a demonstrated code path; actual
slot-switch typing still needs hardware verification.

SUB-EARLY and SUB-REPLAY count staged events and adopted nonempty snapshots.
INPUT-NEUTRAL, USB-HOST and USB-NEUTRAL explain output-switch waits. USB-HOST means
enumerated and not suspended, not a guarantee the HID endpoint is idle right now.

Output selection is a local command and does not require a paired computer. BLE
with no connection already satisfies OldReleased. USB previously checked only
enumeration: a suspended host could never complete a neutral report but was still
waited on indefinitely. Release gates now use CubeUsbConnected (mounted and awake),
not CubeUsbAttached. Live hosts still require neutral delivery; all physical keys,
including a held layer key, must still be released before committing a transition.
Thus no unsafe timeout bypasses neutral delivery to a working host.

Tests execute production early-event/CONNECT initialization, per-handle cache
isolation/cleanup, offline/suspended/live-host release decisions, and existing
security/slot-switch paths. No keyboard firmware or bond deletion is required.

SDK reference: [CCCD restoration](https://github.com/espressif/esp-nimble/blob/139cada0ae932957fa06ba37d17e3c9c2c95c773/nimble/host/src/ble_gatts.c).

## Explicit USB is exclusive (PAIR-POLICY=3)

This supersedes the policy below only for explicit USB selection. OUT_USB waits
for the existing neutral-input/release barrier, terminates the computer connection,
and commits USB routing after DISCONNECT. The pending command is preserved until
disconnect completes. USB does not restart computer advertising; a delayed CONNECT
is terminated rather than reopening that endpoint. The split central remains up:
neither keyboard connection nor any bonding record is cleared.

AUTO retains USB preference with BLE availability; BLE/slot selection restores
computer advertising. An explicit PAIR on an empty slot selects BLE. Slot bindings,
no-timeout empty-profile pairing and existing security checks remain unchanged.
Tests cover USB's disconnect classification, neutral/disconnect ordering, delayed
CONNECT rejection, explicit USB advertising suppression and return to BLE.
Real typing and smartphone/Mac disconnect/reconnect still require hardware tests.

## ZMK-style open-profile advertising (PAIR-POLICY=2)

An empty selected computer slot now remains open for pairing: there is no 120s
timeout. USB/BLE output preference does not determine whether the slot advertises.
An occupied slot advertises when disconnected and accepts only its existing
computer; an adopted computer connection stops surplus advertising. Switching
slots or explicitly clearing computer slots refreshes the same state machine.
The existing PAIR command remains compatible (empty slot succeeds; occupied slot
is not silently cleared). Status PAIR-WINDOW now means the selected slot is open,
not a countdown; PAIR-POLICY=2 identifies this behavior. Historical sections below
describe the earlier timeout policy and are superseded by this section.

An empty slot is intentionally discoverable for longer, matching the reference
ZMK behavior. Encryption, SC/16-byte keys, persistent identity, split-peer rejection,
other-slot ownership, the per-connection security deadline and explicit one-peer
bond-repair authorization are unchanged. No automatic key overwrite/deletion,
controller-limit increase, split-bond reset, or computer-name/USB identity change.
In particular, open advertising does not itself remove orphaned computer keys.

Tests execute production advertising under controller stubs across long idle,
connect/disconnect and USB-independent conditions; authorization tests verify
empty-slot pairing after a day's idle while rejecting invalid or foreign peers.
Hardware acceptance still requires fresh Mac pairing, reconnect, USB/BLE routing,
slot switching, and both halves remaining connected. No build is auto-triggered.

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
The original fix replaced the active cookie only on a successful advertising
start. That covered exhausted pools but missed the same delayed callback when
there was capacity for another successful advertising start.

The corrected cookie belongs to the entire NimBLE host lifetime. Once the first
advertisement starts, both successful and unsuccessful retries reuse that cookie.
While the initial API call is running its provisional callback is accepted.
Recursive starts remain blocked. Host reset invalidates both cookies without
reusing sequence values; a reset inside advertising start cannot resurrect one.
A second successful computer CONNECT is terminated instead of overwriting the
first link's authorization and HID state. Disconnect/security/notification events
still have to match the adopted connection handle; security and ownership gates
are unchanged.

`DONGLE HOST` now includes `HOST-EPOCH=3`. Regression tests reproduce the old
comparison failure and cover 100 failed and 100 successful retries, delayed
CONNECT acceptance, callbacks during an advertising call, host-reset invalidation,
reentrant starts and reset cancellation. No pool sizes, bonding, security gates,
split connection logic or existing Release are changed. Hardware acceptance is
still needed with both halves and the Mac: pairing, HID typing, host cycling,
Cube power cycling, and confirming no untracked third connection remains.

## Stop surplus advertising after adopting a computer

The HOST-EPOCH=3 device capture showed an encrypted, bonded, subscribed computer
on slot 2 with `LINK=1 BLE-READY=1 ADV=1`. Both half advertisements were arriving,
but `ble_gap_connect()` immediately returned 525 (HCI 0x0d, connection rejected
due to limited resources); neither half reached security. The free-pool values
were historical advertisement samples and do not prove current controller room.

A successful retry can start a replacement advertisement before the delayed
computer CONNECT is delivered. The old `Advertise()` guard stopped future starts
but did not stop that already-running advertisement. After adopting the first
computer connection, the firmware now immediately stops any remaining computer
advertisement. The host timer also retries a failed stop at most every 250 ms.
This does not disconnect the computer, modify split scanning, or erase bonds.
When no computer is connected the helper does nothing, preserving the existing
pairing and computer-reconnection behavior. `ADV-STOP-RC` reports the latest stop
result (-1 means not attempted); `ADV` remains the current active-state snapshot.

Tests execute the production helper with controller stubs, including successful
stop, bounded retry, disconnected pairing, and callback/timer integration. Actual
resolution of error 525 still requires hardware acceptance: computer `ADV=0`,
both halves ready, HID typing, half power cycling and Cube power cycling.

## Explicit, one-computer bond repair (USB command)

An existing unowned key can trigger REPEAT_PAIRING while the selected slot is
unbound. The old CLEAR command skips unbound slots and cannot remove this orphan.
Do not use CLEAR ALL or reset keyboard bonds as a workaround.

On a fresh REPEAT_PAIRING from the current computer connection, the firmware
remembers the resolved public/static identity in RAM for 120 seconds. It never
deletes a key automatically or accepts repeated pairing silently. The read-only
host status includes `REPAIR-READY=1` only if the target still belongs to the
selected slot (or is unowned), is not a split identity/RPA, and is not assigned
to any other computer slot. Host reset and any other host command invalidate it.

After the user explicitly authorizes this computer's repair, issue
`DONGLE BT REPAIR` over USB. The normal neutral-input/disconnect transition runs
first. The target is revalidated before deletion. A bound selected slot uses its
existing exact-peer ClearSlot; an unbound slot deletes only the observed orphan
identity. Other slots and all split/Wi-Fi/XiaoZhi state remain untouched. An
unknown/expired/ambiguous target returns result -6 and deletes nothing. A target
already bound to another slot must instead be repaired on that slot explicitly.
Success consumes the candidate, switches to BLE and opens a 120-second pairing
window. Try pairing the same Mac again, then verify encrypted/authorized HID.

Only one computer was authorized for this investigation; the new command is not
permission to clear any other pairing. No real device key has yet been deleted
by the code changes or tests. Flash the new build, reproduce the Mac's pairing
request, read REPAIR-READY, then perform the authorized command once.

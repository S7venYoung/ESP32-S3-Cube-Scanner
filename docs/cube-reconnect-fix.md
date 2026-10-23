# Split reconnect after Cube power loss

Keep existing `cube_split` bindings and NimBLE bonds. Do not flash settings_reset
as part of this update. No changes to computer Bluetooth profiles or HID routing.

The old passive scan suppressed duplicate advertisements for each five-second
scan. A known peer's first report could arrive during its five-second failure
cooldown and be ignored, with subsequent reports suppressed even after the peer
became eligible. This is a code-level retry starvation risk, not proof of the
physical right-hand failure's sole cause.

Disable duplicate suppression so eligibility can be re-evaluated. Rescan after
250 ms instead of two seconds, and retry failures after one second instead of
five. Use a 60 ms interval / 45 ms window during the first 60 seconds of uptime;
then restore the original 160 ms / 30 ms low-duty scan. Scanning remains passive,
bounded to five seconds per scan, and stops when neither half needs discovery.
Never bypass identity checks or encryption and never delete bonds automatically.

Log side-specific connect start/result, encryption/identity result, disconnect
reason and ready uptime to distinguish radio discovery from handshake failure.

Validation: `python3 -m unittest discover -s scripts/tests -p test_cube_reconnect.py -v`
executes production scan and known-peer matching code with a BLE shim and ASan /
UBSan. Covers cooldown expiry within the same scan, both sides, directed adverts,
unknown devices, steady-state duty and no-work guards. Full ESP-IDF build and
physical power-cycle reconnect verification remain required. No build is
automatically triggered by this source-only commit.

## Computer BLE host authorization

On a bound profile, allow only an unresolved random resolvable-private address
to proceed to SMP before the final identity comparison. All other mismatching
identities remain rejected at connect. A private address is never stored as a
profile. Final authorization still requires the selected identity (and no other
slot owning that identity), encryption, bonding and both persisted SC / 16-byte
LTKs. Missing identity/key persistence waits on the existing host timer instead
of immediately terminating; the security deadline is 30 seconds. Invalid or
missing security material never grants access and times out. No auto-erasure or
unpaired-BLE USB fallback is introduced.

This follows the distinction between OTA addresses, identity events and key
storage in [NimBLE's security manager](https://github.com/apache/mynewt-nimble/blob/master/nimble/host/src/ble_sm.c).
The host shim executes the production authorization function for delayed keys,
RPA-to-identity transition, reconnect, wrong slots, short keys, expired pairing
window, save failure and split-peer rejection. These are regression fixes for
observed code-level risks; actual Mac pairing failure still needs hardware logs
and a firmware test to confirm the cause.

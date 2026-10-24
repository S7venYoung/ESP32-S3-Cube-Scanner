# Existing-key persistence repair for IDF 6.1

The current IDF v6.1 manifest pins esp-nimble
`139cada0ae932957fa06ba37d17e3c9c2c95c773`. Inspection found this sequence:

- `ble_store_config_write_peer_sec` / `write_our_sec` replace an existing RAM
  record, keeping the number of records unchanged.
- `ble_store_config_persist_peer_secs` / `persist_our_secs` in
  [ble_store_nvs.c](https://github.com/espressif/esp-nimble/blob/139cada0ae932957fa06ba37d17e3c9c2c95c773/nimble/host/store/config/src/ble_store_nvs.c)
  write on increased count and delete on reduced count, but do not persist the
  equal-count update. The [config store](https://github.com/espressif/esp-nimble/blob/139cada0ae932957fa06ba37d17e3c9c2c95c773/nimble/host/store/config/src/ble_store_config.c)
  therefore can return success while Flash retains an earlier key.

This is a concrete SDK-level persistence risk consistent with a session working
until reboot, not proof it explains every observed HCI 514/520 timeout. First-time
pairing that only adds records is not affected by this equal-count case.

The project uses the public config-store callbacks, without editing SDK/vendor
files. New security records use the normal SDK path. Updates to existing own/peer
security records first commit a single-record redo journal in `cube_bond_fix`,
then replace that exact record through public delete/write APIs. Only after the
replacement succeeds is the journal removed. Any failure is returned to NimBLE;
the journal is kept for replay before the host starts or a later key update.
Explicit security deletion first resolves a pending journal, so a later reboot
cannot resurrect a user-deleted record. There is no bulk bond/NVS erase.

The journal contains security material, like the existing NimBLE NVS store. It is
not exposed through CDC, logs or tests against real devices. Its schema/version,
object kind, struct size and address type are validated; incompatible data stops
BLE startup rather than being guessed or discarded. It is scoped to the pinned
IDF 6.1 security-record ABI. Existing NVS encryption settings apply unchanged.

Already stale Flash keys cannot be reconstructed from this fix. After installing,
a successful fresh pairing may be required once; only do a targeted bond clear
with explicit user approval, preserving Wi-Fi/XiaoZhi and unrelated host profiles.
Normal future application updates must not require settings_reset every time.

Host tests execute the production callback functions with a count-based SDK/NVS
shim. They reproduce stale equal-count updates, verify both own and peer keys,
unrelated records, commit failure, deletion failure, power loss between replace
steps, replay after successful write, repeated replay, explicit deletion and
schema rejection. ASan/UBSan passes. Actual ESP-IDF build, NVS-device tests and
repeated power-cycle reconnection remain required; no automatic Actions or flash.

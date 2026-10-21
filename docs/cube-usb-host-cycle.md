# USB output position in the host cycle — 2026-10-21

Next-target commands now cycle USB -> BLE 1 -> BLE 2 -> BLE 3 -> BLE 4 -> BLE 5
-> USB. This applies to the existing Volume Up long press, next-Bluetooth voice
command and DONGLE BT NEXT because all use HostCommand::Next.

USB selects Output::Usb; it does not allocate a sixth BLE bond or overwrite the
five stored computer identities. Direct BT_SEL 0..4 still select BLE 1..5. The
existing release/physical-neutral transition barrier applies before each switch.
AUTO currently using USB advances to BLE 1; AUTO using BLE advances from its
selected profile. Explicit USB advances to BLE 1 regardless of the remembered
BLE profile. USB without a connected host does not silently select BLE.

The user cancelled automatic USB fallback when selecting unpaired BLE. That
policy is unchanged. USB must be selected explicitly (or use existing AUTO).

The host-cycle C++ test passed with ASAN/UBSAN, covering all profiles, repeated
wraparound and invalid-index recovery. The dual build script includes this test.
No firmware build or physical button/voice validation has been performed yet.

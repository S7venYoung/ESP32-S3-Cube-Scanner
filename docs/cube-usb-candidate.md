# Xingzhi Cube USB dongle candidate — not final

This opt-in variant adds actual USB keyboard/media output and CDC companion sync to the encrypted split input adapter. It uses the 61-position, three-layer keymap from `zmk-elashvelvet:zmk-esp32-cube`. USB replaces native Serial/JTAG on GPIO19/20, without altering display/audio/battery/button pins. The ordinary Codex build is unchanged. TinyUSB is pinned to Espressif component 2.4.0.

Keyboard ID1 is an eight-byte, six-key HID report with modifiers; seven held ordinary keys emit ErrorRollOver. ID2 has independent Volume Up/Down/Mute buttons. Press bindings survive layer changes. An ordered 64-frame FIFO preserves brief taps; overflow releases input and blocks new output until all input is neutral. USB connect/disconnect/suspend/resume/protocol changes also require neutral input before rearming. No automatic sample/demo typing exists. Current BIOS boot protocol omits media reports.

`CAPS`, `PAIR`, `CODEX2` and Wi-Fi sync remain available. The existing companion should identify the new CDC port by protocol, but must be physically verified. `DONGLE` identifies this build as `CUBE-USB-CANDIDATE/1 <status> HID-ON`, not the input-only bench. `DONGLE BIND` still explicitly saves peer identities and requires manual reboot; obtain both real fixed public/static identities first. `DONGLE DISABLE` releases and blocks output; `DONGLE ENABLE` requires neutral input before rearming. These enable/disable commands are not persistent. Disconnect the companion before opening the port manually.

## Not implemented / not verified

- Computer Bluetooth HID output and BT_SEL/BT_CLR/BT_CLR_ALL actions are **not** implemented in this USB-only candidate. These actions are recognized separately and logged; they are not silently mapped to keyboard usages.
- The original cross-hand bootloader combo, runtime keymap/macro/combos, mouse, host LED propagation and NKRO are not implemented. This is not a complete ZMK behavior-engine port.
- Dongle position data is not yet fed into the existing theme's layer/WPM/battery telemetry. Existing scanner advertising is replaced by the connected client in this variant.
- No physical typing, latency, USB suspend/recovery, CDC dual synchronization, left/right disconnect, Wi-Fi/voice coexistence or battery-only behavior has been verified.
- The board's VBUS sensing circuitry has not been verified. The candidate uses the default bus-powered USB PHY/configuration, not a claimed self-powered compliance implementation. Do not infer USB electrical compliance while running on the battery alone.

Therefore this file/package must remain marked **candidate**, not final usable firmware. Do not install it over the working setup without a recovery plan. App-only flashing has a distinct filename and is intentionally not advertised as a normal Codex update. USB enumeration changes from native Serial/JTAG to composite CDC/HID; esptool flashing may require manually entering ROM download mode using the board's BOOT/reset method.

## Required final acceptance

Use the actual Cube and both halves to test normal typing and short taps; all three layers; simultaneous modifiers, Space and media buttons; held keys across layer changes; six/seven-key rollover; removal/reconnection of either half; USB unplug/suspend/resume; companion CDC + Wi-Fi sync; Xiaozhi wake/conversation while typing; and battery-only power. Implement and test computer Bluetooth output/profile management and missing required behaviors before calling the requested dual-output dongle final.

Build: `bash scripts/build-cube-usb-candidate.sh` with ESP-IDF v6.1.

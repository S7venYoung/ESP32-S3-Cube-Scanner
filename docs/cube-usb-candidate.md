# Xingzhi Cube USB dongle candidate — not final

This opt-in variant adds actual USB keyboard/media output and CDC companion sync to the encrypted split input adapter. It uses the 61-position, three-layer keymap from `zmk-elashvelvet:zmk-esp32-cube`. USB replaces native Serial/JTAG on GPIO19/20, without altering display/audio/battery/button pins. The ordinary Codex build is unchanged. TinyUSB is pinned to Espressif component 2.4.0.

Keyboard ID1 is an eight-byte, six-key HID report with modifiers; seven held ordinary keys emit ErrorRollOver. ID2 has independent Volume Up/Down/Mute buttons. Press bindings survive layer changes. An ordered 64-frame FIFO preserves brief taps; overflow releases input and blocks new output until all input is neutral. USB connect/disconnect/suspend/resume/protocol changes also require neutral input before rearming. No automatic sample/demo typing exists. Current BIOS boot protocol omits media reports.

`CAPS`, `PAIR`, `CODEX2` and Wi-Fi sync remain available. The existing companion should identify the new CDC port by protocol, but must be physically verified. `DONGLE` identifies this build as `CUBE-USB-CANDIDATE/1 <status> HID-ON`, not the input-only bench. `DONGLE BIND` still explicitly saves peer identities and requires manual reboot; obtain both real fixed public/static identities first. `DONGLE DISABLE` releases and blocks output; `DONGLE ENABLE` requires neutral input before rearming. These enable/disable commands are not persistent. Disconnect the companion before opening the port manually.

The diagnostic client also supports this candidate. If explicit binding arguments are supplied, it disables HID before writing identities; read-only status does not change output. Binding acknowledgements say `HID-MODE-UNCHANGED` rather than falsely claiming an HID-off build. A disabled candidate reports `HID-OFF` in its status response. The host-test suite does not constitute hardware validation.

## Fight theme

The existing Fight theme remains the default for a fresh configuration; a saved theme is preserved. Long-press Volume Down cycles themes. Dojo, fighter sprites, idle animation, CODEX/VS/TODAY artwork, mirrored bars and macOS modifier symbols retain the existing design. No visible WPM module is added.

Encrypted split position notifications now feed independent left/right typing activity. Only new ordinary key presses count; duplicate snapshots, modifiers, layer keys and keys already held at reconnect do not. A bounded five-second window drives each fighter's attack intensity. After 750 ms without a new press its activity becomes zero; VS/TODAY return after the existing 1.5-second idle hold. Modifier highlights are polled by the LVGL timer, not drawn from the BLE callback.

The lower energy bars use each connected half's standard BLE Battery Level characteristic (0x2A19), read every 30 seconds. Missing, disconnected or stale readings remain unknown/empty rather than inventing 0% or 100%. A valid 0% is retained. Layers and modifier state also feed the existing keyboard status getter. This is actual connected-split telemetry, not the scanner advertisement protocol; both halves need the Cube peripheral firmware and explicit identity binding. No additional keyboard telemetry change is required.

## Not implemented / not verified

- Computer Bluetooth HID output and BT_SEL/BT_CLR/BT_CLR_ALL actions are **not** implemented in this USB-only candidate. These actions are recognized separately and logged; they are not silently mapped to keyboard usages.
- The original cross-hand bootloader combo, runtime keymap/macro/combos, mouse, host LED propagation and NKRO are not implemented. This is not a complete ZMK behavior-engine port.
- Fight theme integration is implemented but has not been visually or physically verified on the Cube. Existing scanner advertising is replaced by the connected client in this variant.
- No physical typing, latency, USB suspend/recovery, CDC dual synchronization, left/right disconnect, Wi-Fi/voice coexistence or battery-only behavior has been verified.
- The board's VBUS sensing circuitry has not been verified. The candidate uses the default bus-powered USB PHY/configuration, not a claimed self-powered compliance implementation. Do not infer USB electrical compliance while running on the battery alone.

Therefore this file/package must remain marked **candidate**, not final usable firmware. Do not install it over the working setup without a recovery plan. App-only flashing has a distinct filename and is intentionally not advertised as a normal Codex update. USB enumeration changes from native Serial/JTAG to composite CDC/HID; esptool flashing may require manually entering ROM download mode using the board's BOOT/reset method.

## Required final acceptance

Use the actual Cube and both halves to test normal typing and short taps; all three layers; simultaneous modifiers, Space and media buttons; held keys across layer changes; six/seven-key rollover; removal/reconnection of either half; USB unplug/suspend/resume; companion CDC + Wi-Fi sync; Xiaozhi wake/conversation while typing; and battery-only power. Implement and test computer Bluetooth output/profile management and missing required behaviors before calling the requested dual-output dongle final.

Build: `bash scripts/build-cube-usb-candidate.sh` with ESP-IDF v6.1.

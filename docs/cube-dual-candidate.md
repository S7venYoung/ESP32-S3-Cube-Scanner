# Xingzhi Cube USB + computer BLE HID candidate

This variant shares one NimBLE host between two encrypted ZMK split-client connections and one computer HID peripheral connection. Computer output supports an eight-byte six-key keyboard report, separate three-bit Volume Up/Down/Mute report, HID report/boot protocol, protocol changes and HID suspend/resume. HID information, report map, Report Reference descriptors, output LED writes and Device Information PnP ID are registered. The BLE report map is the same map used by USB. The input-only bench and USB-only candidates remain separate variants.

The existing Fight theme, dojo/fighters/idle animation, left/right typing activity, battery bars, modifier highlights, XiaoZhi voice, CDC and Wi-Fi Codex synchronization remain present. UI getters report the selected computer profile and active output. No second NimBLE initialization or advertisement scanner is introduced.

## Routing and profiles

Fresh configuration uses AUTO: USB HID when mounted and not suspended, otherwise computer BLE HID. It sends keys to one output, never both computers. An explicit output preference is persisted independently of the five computer profiles. Each profile stores the resolved public/static computer identity; NimBLE persists its security keys/IRKs. The same computer identity cannot be claimed by two slots, and a split-keyboard identity cannot be used as a computer slot.

Selection first queues release-all on the old endpoint, waits for transport transmit completion (not an OS/application acknowledgement) or disconnection, and waits for all physical positions to be released, including layer keys. Held keys are never replayed into the newly selected host. Both transports have bounded ordered 64-frame queues; overflow, protocol changes, encryption/subscription changes and reconnect require neutral input before accepting further keys. A stalled BLE notification is disconnected after three seconds. A computer that does not complete encrypted/bonded security is disconnected after fifteen seconds.

Keyboard SYS-layer BT_SEL 0..4 selects computer slot 1..5 and explicitly prefers BLE output. BT_CLR clears the selected **computer** slot; BT_CLR_ALL clears only these five computer slots. These commands do not clear the left/right split bonds, `cube_split` bindings, Wi-Fi or Codex pairing token. The bond table permits seven peers (five computers plus two halves); a full table fails rather than silently evicting an existing keyboard or computer.

Existing bound profiles may reconnect. An empty slot does not advertise after boot until explicitly selected or opened for pairing. Selecting an empty profile or clearing a slot opens a two-minute Just Works pairing window. This is encrypted bonding, **not** passkey/MITM authentication; only open the window in a trusted environment. A wrong resolved identity is rejected. Host-based privacy is enabled for bonded computer RPA resolution; interoperability remains a hardware test.

## USB CDC commands (115200, newline terminated)

Exit the companion before opening its CDC port manually. Do not reset the board or flash as part of these commands.

- `DONGLE`: `CUBE-DUAL-CANDIDATE/1 <split-status> HID-ON|OFF`.
- `DONGLE HOST`: current one-based PROFILE, active OUTPUT, BLE-READY, SWITCHING and RESULT.
- `DONGLE BT 1` through `DONGLE BT 5`: select/persist a slot and prefer BLE. Empty slots open pairing.
- `DONGLE BT PAIR`: reopen the selected empty slot for two minutes. A bound slot requires explicit CLEAR first.
- `DONGLE BT CLEAR`: clear only the selected computer's bond/profile and open pairing.
- `DONGLE BT CLEAR ALL`: clear only all computer slots, not the split keyboard bonds.
- `DONGLE OUTPUT AUTO`, `DONGLE OUTPUT USB`, `DONGLE OUTPUT BLE`: persist output preference.
- `DONGLE ENABLE`, `DONGLE DISABLE`: enable/disable both outputs for this boot. Disable queues release-all immediately; transition completion waits for physical neutral.
- Existing `DONGLE BIND L|R <identity> 0|1`, `CAPS`, `PAIR`, `CODEX2` and companion Wi-Fi commands remain unchanged.

Mutation commands return `CUBE-DUAL-CANDIDATE QUEUED <ticket>`, not a false success acknowledgement. Release physical keys and poll `DONGLE HOST` until DONE matches that ticket and COMMAND-RESULT=0; check the expected PROFILE/OUTPUT too. SWITCHING reports pending work; an immediate old idle status is not completion. Tickets wrap after 65535, so finish one operation before issuing another. Another command may be held in the single bounded mailbox; `ERR HOST-BUSY` means it was not accepted. RESULT -1 is a storage failure, -2 a busy keyboard action, -3 attempting PAIR on a bound slot, -4 report overflow; positive values are NimBLE errors. COMMAND-RESULT is the last command result, separate from later transport errors. Errors do not grant HID authorization. Disable and verify completion before changing split bindings. The older binding diagnostic script deliberately refuses automated dual-output mutation because its synchronous acknowledgement assumptions no longer apply.

The computer appears as `Cube ZMK BLE 1` ... `Cube ZMK BLE 5`, according to the selected slot. Selecting another bound slot disconnects the previous computer after release and advertises for the selected identity. Selecting USB/AUTO does not discard computer bonds.

## Build and remaining validation

Build with ESP-IDF v6.1: `bash scripts/build-cube-dual-candidate.sh`. Application: `xingzhi_cube_dual_candidate.bin`; full package: `merged-binary.bin` plus bootloader, partitions, assets, OTA initialization, SDK config and checksums. This changes the native USB device to HID + CDC; use the board's manual ROM BOOT/reset recovery method if required. It is not the ordinary Codex update image.

This is a **code-complete candidate for the requested computer HID/profile features, not hardware-validated final firmware**. Compile and host-state tests cannot demonstrate Mac/Windows/Linux pairing, RPA reconnect after power cycle, five-host switching, actual key release, short taps, media/boot/suspend behavior, USB/BLE automatic fallback, BLE + Wi-Fi/audio coexistence, display timing or electrical VBUS compliance. Those need the Cube, both halves and real computers. No firmware has been flashed as part of development.

Runtime keymap editing, arbitrary macros/combos, mouse/NKRO, forwarding host LEDs to the halves and the inherited cross-hand bootloader combo are still outside this static-keymap adapter. The original ZMK branches remain untouched.

The GATT/GAP integration follows the [Espressif NimBLE peripheral example](https://github.com/espressif/esp-idf/tree/v6.1/examples/bluetooth/nimble/blehr), while retaining the existing allowlisted split client.

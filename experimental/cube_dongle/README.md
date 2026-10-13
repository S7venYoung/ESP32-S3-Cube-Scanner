# Xingzhi Cube dongle — experimental input bench (HID off)

The separate `xingzhi-cube-1.54tft-wifi-usb-dongle-candidate` variant now includes actual USB HID + CDC output and the static Velvet keymap. See `docs/cube-usb-candidate.md` for its limitations. This input-only bench remains HID-off; neither variant is the requested final USB/BLE dongle.

Target: preserve XiaoZhi/Codex UI, receive two ZMK split peripherals, output keyboard input to a computer over USB **or** BLE. The host-tested core now has an opt-in NimBLE input-only adapter at `main/scanner/cube_split_bench.cc`. Normal Codex builds remain unchanged. The bench variant compiles the client but does not implement either computer HID output; it is not a usable keyboard dongle yet.

## Stage 2 bench

`xingzhi-cube-1.54tft-wifi-dongle-bench` enables a single NimBLE central and replaces advertisement-only scanning. It keeps XiaoZhi, themes, existing USB Serial/JTAG companion sync and Wi-Fi sync. This stage has no computer BLE peripheral, no TinyUSB and no HID injection. It uses fixed public/static peer identity addresses from `CONFIG_CUBE_SPLIT_LEFT_ADDRESS`/`RIGHT_ADDRESS` and their address types. Defaults are empty: the published compile-check artifact intentionally makes no connections until addresses are configured. RPA/rotating addresses are not supported in this bench. Do not auto-bind the first advertised device to a side.

The client discovers the split service/position characteristic and its CCC by UUID, after verifying peer identity and link encryption. It enables notifications and reads an initial snapshot. New notifications supersede an older in-flight initial read. A side can set only positions belonging to its verified Velvet transform (61 total, 32 left and 29 right despite the inherited Velvet46 name). Invalid frames quarantine/terminate the connection and clear that side's state. Disconnect/reset clears state. Connection generations reject stale GATT completions. A 15-second handshake deadline and five-second peer retry cooldown bound failed pairing/discovery. Existing bonds are never automatically deleted; reset/re-pair must be explicit.

Snapshots are handled on the NimBLE host event queue without an extra unbounded worker queue. The bench logs side and total pressed count, not typed content; it never sends a keyboard report. High-rate per-frame logging is for bring-up only and must be removed/rate-limited before latency testing. GATT output, UI telemetry and the real keymap are not yet wired to this adapter.

The existing USB companion serial endpoint accepts the read-only `DONGLE\n` diagnostic command and returns `CUBE-SPLIT-BENCH/1 <decimal-status> HID-OFF`. Status bits 0/1 mean configured left/right, bits 2/3 mean ready left/right, and bits 8..15 contain the combined pressed-position count. This is atomic and safe to read from the USB task; it is not a keyboard input stream. Exit the companion app before opening the serial endpoint with a diagnostic client to avoid contention.

Pairing uses ZMK-compatible L2/Just Works without MITM authentication. A fixed-address allowlist is not cryptographic proof of the physical keyboard during first pairing: perform initial bonding only in a controlled environment. Automatic bond deletion is disabled. A production UI needs explicit pairing windows and identity/bond management; this bench does not claim secure unattended first pairing.

Build: `bash scripts/build-cube-dongle-bench.sh` in ESP-IDF v6.1. Obtain the two identity addresses from trusted device/USB diagnostics rather than assuming advertised names prove identity. Never flash keyboard peripherals before a recovery/working-central plan is ready.

The bench supports explicit USB-only provisioning without recompilation: send `DONGLE BIND L C1:23:45:67:89:AB 1\n` and the analogous `R` command with the real right address. The example is a placeholder, not your keyboard. Type 0 is public, 1 is static random (high two address bits set); malformed, zero/all-ones and duplicate persisted identities are rejected. One address+type string is committed atomically in NVS namespace `cube_split`, key `left` or `right`. `SAVED REBOOT-REQUIRED` confirms successful commit. This does not clear bonds, reboot or connect automatically; manually reboot after binding both peers. Saved identities override compile-time defaults on the next boot. The bench starts independently of Wi-Fi connectivity. Firmware defaults remain inert until identities are provisioned.

Keyboard baseline: `S7venYoung/zmk-elashvelvet`, `zmk-esp32-cube`, derived from `codex/zmk04-dya-Prospector-dongle` at `61d1e5d63e6f176058f4869a1c3ab2a8608ee746`. Its `cormoran/zmk main+dya` transport is not assumed to be identical to stock ZMK. Position UUID `00000001-0096-7107-c967-c5cfb1c2482a` carries exactly 16 bytes, LSB-first global key positions. Snapshot reads and notifications require an encrypted split connection. DYA relay UUID `00000007-...` is separate and is not implemented here.

`split_state.h` validates snapshot size, trust and half ownership atomically; disconnect releases only that half. Static, bounded key state supports keyboard-page keys, modifiers, transparent bindings and momentary layers. Press-time bindings survive layer release, duplicate usages are deduplicated and more than six keys reports HID ErrorRollOver. It is **not** the ZMK behavior engine: combos, hold-tap, runtime macros, consumer/media reports, mouse, BT profile actions and Studio editing are not implemented. It has no actual Velvet keymap yet; tests use synthetic mappings.

USB/BLE routing sends release-all to the old endpoint before switching. Only one computer endpoint receives each report, avoiding duplicate typing. Drivers must queue/deliver release reports reliably; the core alone does not guarantee delivery. It has no mutex: all GATT, GAP and output callbacks must be serialized onto one bounded worker queue. Overflow recovery must resynchronize/release input, never silently drop a key release. Silence is not a disconnect: held keys must not time out just because ZMK sends no new notifications.

## Still required before flashing

1. Dedicated left/right Cube peripheral configurations are built with a pinned ZMK commit. Physically validate their position mapping, security/bonds and reset/re-pair UX against this bench.
2. Extend the bench's single NimBLE owner to include scanner telemetry + computer HOGP server; the current variant reserves three connections but only connects two halves. Register server services before host start; do not initialize a second BLE host. Add private-address resolution before claiming general ZMK pairing support.
3. Import the real physical-layout mapping and supported bindings; explicitly reject unsupported behavior types rather than silently substituting keys. Preserve cross-hand semantics before claiming ZMK compatibility.
4. Replace native USB Serial/JTAG sync with TinyUSB composite HID + CDC. The same GPIO19/20 cannot expose both controllers as concurrent separate devices. Rework Cube serial sync to CDC while preserving `CUBE-CODEX/2`, PAIR and Wi-Fi APIs. Keep a documented ROM-download recovery path.
5. Implement BLE computer pairing, HID report map, CCC subscription/security, indicators, suspend/resume and explicit USB/BLE selection. Losing a host link must reconcile held state and releases before reconnect.
6. Measure input latency, Wi-Fi/audio coexistence, internal RAM and USB reconnect on physical Xingzhi hardware. GPIO/power/audio/display wiring stays unchanged.

## Host test

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  experimental/cube_dongle/state_test.cc -o /tmp/cube-dongle-state-test
/tmp/cube-dongle-state-test
```

Wire format source: https://github.com/cormoran/zmk/blob/main%2Bdya/app/src/split/bluetooth/service.c

Status: state merge/key processing/router and Velvet ownership tested on host; opt-in GATT input adapter awaiting physical BLE verification; no USB/HOGP output, complete keyboard behavior or hardware validation. Existing production firmware is unchanged.

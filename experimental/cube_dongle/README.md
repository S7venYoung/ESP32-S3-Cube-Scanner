# Xingzhi Cube dongle — stage 1 (not runnable firmware)

Target: preserve XiaoZhi/Codex UI, receive two ZMK split peripherals, output keyboard input to a computer over USB **or** BLE. This folder is an isolated, host-tested core; it is deliberately not wired into production CMake or boot. No dongle `.bin` is available yet.

Keyboard baseline: `S7venYoung/zmk-elashvelvet`, `zmk-esp32-cube`, derived from `codex/zmk04-dya-Prospector-dongle` at `61d1e5d63e6f176058f4869a1c3ab2a8608ee746`. Its `cormoran/zmk main+dya` transport is not assumed to be identical to stock ZMK. Position UUID `00000001-0096-7107-c967-c5cfb1c2482a` carries exactly 16 bytes, LSB-first global key positions. Snapshot reads and notifications require an encrypted split connection. DYA relay UUID `00000007-...` is separate and is not implemented here.

`split_state.h` validates snapshot size, trust and half ownership atomically; disconnect releases only that half. Static, bounded key state supports keyboard-page keys, modifiers, transparent bindings and momentary layers. Press-time bindings survive layer release, duplicate usages are deduplicated and more than six keys reports HID ErrorRollOver. It is **not** the ZMK behavior engine: combos, hold-tap, runtime macros, consumer/media reports, mouse, BT profile actions and Studio editing are not implemented. It has no actual Velvet keymap yet; tests use synthetic mappings.

USB/BLE routing sends release-all to the old endpoint before switching. Only one computer endpoint receives each report, avoiding duplicate typing. Drivers must queue/deliver release reports reliably; the core alone does not guarantee delivery. It has no mutex: all GATT, GAP and output callbacks must be serialized onto one bounded worker queue. Overflow recovery must resynchronize/release input, never silently drop a key release. Silence is not a disconnect: held keys must not time out just because ZMK sends no new notifications.

## Still required before flashing

1. Pin and build dedicated left/right Cube peripheral configurations; bind identities to sides, never pair automatically with arbitrary advertisements. Review ZMK security/bonding compatibility and reset/re-pair UX.
2. Add a single NimBLE owner for scanner + split GATT client + computer HOGP server. Configure at least three concurrent connections. Register services before host start; do not initialize a second BLE host. Read an initial position snapshot after encrypted subscription, handle reconnect and service discovery by UUID (not fixed handles).
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

Status: state merge/key processing/router tested on host; no BLE link, USB driver, complete keyboard behavior or hardware verification. Existing production firmware is unchanged.

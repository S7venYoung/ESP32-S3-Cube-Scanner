# Dongle keyboard status advertisements

The USB+BLE dongle sends unpaired, scannable, non-connectable legacy BLE
advertisements when USB is selected or the computer BLE link is connected.
It uses the existing NimBLE host queue and advertising instance; it does not
open another connection or change split bonds, HID security, or radio configuration.

The two manufacturer payloads alternate every 100 ms, with a 50 ms advertising
interval: Prospector ABCD v1 (26 bytes) and split-WPM ABCE v1 (17 bytes).
Each payload is refreshed approximately five times per second. Passive scanners
can decode the payload; active scanning obtains the scan response name.

Data comes from the same live keyboard snapshots used by the Cube themes:
active layer number and first four characters of its name, aggregate and per-half
WPM, modifiers, left/right battery, and actual computer USB/BLE output readiness.
The stable keyboard identifier is derived from the Cube Bluetooth MAC address.
Unavailable batteries remain unavailable. The original ABCD protocol cannot
represent a right battery of exactly zero (ABCE can); negative WPM encodes as zero.
Profile is the selected saved BLE slot, including while USB output is selected;
ABCD has no separate USB slot field. No key content or computer usage data is sent.

The advertised name remains **Cube Velvet Pro**, preserving computer discovery.
Receivers with a Velvet46-only name filter must select Cube Velvet Pro instead.
Original Prospector receivers decode ABCD; receivers supporting ABCE also obtain
left/right WPM. Both halves disconnected stops telemetry; receivers apply their
normal timeout rather than retaining an indefinitely live keyboard.

## Radio priority

Computer pairing/reconnection advertisements have priority. While a BLE slot is
waiting for a computer, status broadcasting pauses. It resumes after CONNECT;
USB selection also enables it. Changing output stops the telemetry advertisement
before the existing host transition runs. Controller errors retry at most every
100 ms; no reconnect, reset, or bond erasure is introduced.

This intentionally retains the tested legacy GAP implementation. Continuous
independent telemetry during computer pairing would require migrating the shared
host to extended advertising/scanning and separately validating all three links.

## Validation

Host tests round-trip both wire formats through the existing Cube scanner parsers,
including modifiers, unknown/zero battery, layer truncation, and WPM saturation.
Run `python3 -m unittest discover -s scripts/tests -v` in a complete checkout.
Physical acceptance still needs a receiving scanner plus both halves and USB/BLE
computer output; host tests do not establish over-the-air latency or stability.

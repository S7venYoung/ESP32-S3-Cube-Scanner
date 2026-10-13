# Cube split bench USB diagnostics

This is for `codex/xingzhi-cube-dongle` input-only test firmware, **not a usable dongle**. Neither USB HID nor computer BLE HID is enabled. A successful compile does not establish actual pairing, low latency or audio coexistence.

Close Prospector and other serial software before testing. Install `pyserial` in an isolated Python environment. Read-only status:

```sh
python scripts/cube-split-diagnostics.py --port /dev/cu.usbmodemYOUR_PORT
```

The tool checks `CAPS` and requires a valid `DONGLE` response before any optional configuration write. It reports whether each half has a saved identity, whether its encrypted position subscription is ready, and total held-position count. It does not display typed text. It never flashes, toggles boot/reset, clears Bluetooth bonds or automatically reboots.

Only when you have verified the two real keyboard identity addresses, provision both sides:

```sh
python scripts/cube-split-diagnostics.py --port /dev/cu.usbmodemYOUR_PORT \
  --bind-left C1:23:45:67:89:AB --left-type 1 \
  --bind-right D1:23:45:67:89:AB --right-type 1
```

Those addresses are examples, not yours. Type 0 is public; type 1 is static random, not rotating RPA. New bindings are saved one side at a time; if the second write fails, the first remains saved. Reissue valid bindings as needed before manually rebooting. Each address/type pair is one atomic NVS string. After reboot, rerun read-only diagnostics. No identity means no connection attempt. Existing peripheral bonds to an old central can prevent pairing: plan recovery before changing working keyboard firmware, and do not erase settings automatically.

Never install keyboard peripherals expecting immediate typing: complete Cube output transports and real keyboard behaviors are still under development. The normal Codex/XiaoZhi build remains unchanged.

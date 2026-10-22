# Scanner artwork/motion fixes ported to dongle — 2026-10-22

Port sources: 1b427aab61c07110e0465b8fd18e0a97c258bbf6 (TODAY baseline,
clean decimal/M/K) and db265bc440d7bf5c84feaf097a5d7d1abe995d5e (3/10/25/40 WPM,
three-second super charge). Reuse the existing digit atlas without editing it;
its local host-test copy's Git blob matches the dongle's tracked atlas exactly.

The older dongle animation adapter needed the source windup/strike/recovery
state machine too. Effects now use strike_age, movement uses mirrored x/y,
and the LVGL animation timer is 40 ms. Existing dongle telemetry, modifier
cache and per-side battery cache are retained. Do not replace these with scanner
broadcast handling. Raw WPM, keyboard input, battery percentages, HID/DYA routing,
and pairing are unchanged. Returning to zero input finishes the current action
before idle; an offline link resets animation immediately.

The two health bars now reserve a 42 px center clock field for the existing
official five-hour reset HH:MM (not a countdown). Rows mirror: 5H LEFT 85% on
the left, 59% 7D LEFT on the right. Each health bar has 88 px fill width; right
fill remains right-aligned. Unknown reset is --:--. TODAY token total remains.

The lower right keyboard-energy gauge is now the exact horizontal pixel mirror
of the left gauge (bevel, cell slant and charge). The former code reversed only
the cell counter. As charge decreases, left empties from center toward left and
right empties from center toward right. Tests compare every RGBA pixel at
unknown/0/20/50/80/100%, check guards and confirm 50% charge stays at outer edges.

The user's USB-power requirement is restoration after both USB supply loss and
an exhausted battery, not merely post-flash reset. Startup now explicitly
releases the existing RTC power-pin hold after preloading its normal HIGH level.
Shutdown previously latched this pin LOW without startup release. No pin
assignment, charging current, voltage threshold or brownout protection changes.
This only helps once ESP32 code starts: a fully depowered MCU cannot run code to
enable its own external supply. Hardware power/EN sequencing or weak USB supply
may still require board-level diagnosis. Do not claim automatic cold start is
validated until tested safely; do not intentionally deep-discharge the battery.

Host ASAN/UBSAN tests cover token baseline/unit visibility/buffer guards, WPM
boundaries, three-second charge, phase completion, independent hands, wraparound,
and telemetry threshold mapping. Python checks cover layout geometry and power
hold call order. The dual build script runs the relevant C++ suites. ESP-IDF
build, LCD layout/fps/coexistence and restored-power cold-start tests are pending.
No Action is automatically started and the scanner branch is unchanged.

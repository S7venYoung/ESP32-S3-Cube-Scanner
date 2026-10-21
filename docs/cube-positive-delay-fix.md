# Positive scheduler delays — 2026-10-21

Live diagnostic samples reported reset=6 (task watchdog); an uptime decrease
confirmed another reboot. No panic backtrace was available from runtime CDC.
Previous dual-build sdkconfig records CONFIG_FREERTOS_HZ=100. FreeRTOS integer
conversion turns 2 ms and 5 ms into zero ticks at that frequency.

The USB HID worker and CDC backpressure wait now block at least one tick. The
BLE output callout and management/binding wait loops likewise clamp their delay
to a positive tick instead of potentially continuously requeueing or spinning.
The watchdog remains enabled. No pins, bonds, profiles, output preference,
keymap or persisted settings are modified. The user cancelled the proposed
USB fallback policy; the existing WantBle routing expression is unchanged.

This fixes a concrete starvation risk consistent with the observed reset,
not a proven exclusive cause. Verify the rebuilt target's actual tick rate and
run prolonged physical idle/typing/DYA testing; if reset recurs, obtain UART
watchdog task/backtrace diagnostics before declaring it resolved.

Validation includes tick arithmetic at 100/250/1000 Hz and source regression
checks for guarded USB delays, both BLE timer arms, management/binding waits
and unchanged output policy. ESP-IDF build and physical validation are pending.

# Dongle keyboard display binding — 2026-10-21

Codex layer/WPM/battery widgets and Macintosh layer/battery/radio widgets were
created as unavailable placeholders without retaining handles for refresh.
The dongle already publishes connected keyboard snapshots, but these widgets
never consumed them.

Retain their handles and update them from GetKeyboardStatus in the existing
locked periodic metrics refresh. Bind Macintosh modifier inversion there too;
do not reenter the display mutex through SetMacModifierState. Existing White
and Fight snapshot consumers remain unchanged. Unknown/disconnected battery
values stay --%, real zero stays 0%, and empty fills are hidden. No BLE host,
advertisement scanner, pairing reset, or peripheral firmware change is added.

Validation: four Python source-wiring checks passed. These check the call path,
retained widget handles, unknown-value guards and absence of recursive locking;
they do not execute LVGL. SDK compilation and physical validation remain pending.
Verify each theme after connecting either/both halves, changing layers, typing,
pressing modifiers, disconnecting and reconnecting. A battery still unavailable
after this fix needs separate GATT-path diagnosis; no percentage is fabricated.

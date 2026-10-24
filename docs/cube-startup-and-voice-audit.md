# Split handshake isolation, boot panic and wake-word audit

## Split links

ESP32-specific mitigation: do not start a new split scan/initiation while a half
is Connecting through Reading. Ready halves continue normal input; Closing is
not a global gate. The existing one-shot retry is rearmed while blocked, so the
second half resumes after readiness or disconnect. Gate both learned and known
advertisers. This is not a claim that native ZMK serializes its handshakes, nor
proof that parallelism caused the observed SMP timeout. Bonds and encryption
validation are unchanged. Power-cycle hardware validation is required.

## Second boot

Live `reset=4` means ESP-IDF `ESP_RST_PANIC`, not software restart or brownout.
The existing dongle firmware-upgrade guard remains; the provisioning request is
still needed for XiaoZhi activation. No panic backtrace has been captured, so
the crash location is unproven. Do not suppress brownout/watchdogs or erase NVS.

`DONGLE BOOT DIAG` adds checksum-validated RTC RAM breadcrumbs, no flash writes:
sequence, previous_stage, current stage. A normal power-on or brownout discards
old RTC state. Stages: 1 NVS, 2 board/display/audio initialization, 3 USB/split/Wi-Fi
start, 4 startup returned, 5 main loop, 6 activation, 7 assets checked,
8 protocol configured. They are last-written markers, not a stack trace; an
asynchronous task can update them. Full power loss may discard all markers.

## XiaoZhi wake word

No microphone pins, models, AFE engine or voice activation flow are changed.
Wake word normally becomes enabled in idle after activation; this alone cannot
prove why the user's wake word stopped working. Use `DONGLE AUDIO DIAG` to read
application state, wake_running, wake_status, input_frames, input_errors.
Wake status: 0 disabled/not requested, 1 enabled, 2 wake callback fired,
-1 engine initialization failed, -2 missing wake model, -3 lite resource restore
failed (not S3). Counters report successful codec input reads and failed reads,
not sound level, AFE consumption or microphone correctness. No raw audio is
stored or transmitted by these diagnostics. Queries never initialize the engine.

Tests cover production handshake gating, RTC trace functions and production wake
enable results with host shims and ASan/UBSan. Full ESP-IDF compilation, actual
panic trace and physical microphone/power-cycle tests are still pending. These
diagnostics are not claims that reboot and wake-word failures are fixed.

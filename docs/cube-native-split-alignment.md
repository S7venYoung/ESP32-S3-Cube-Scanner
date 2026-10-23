# Align connection establishment with native ZMK split

Reference: `S7venYoung/zmk-elashvelvet` branch `codex/nano-dongle` uses
`cormoran/zmk main+dya`, not unmodified upstream ZMK. Its central explicitly
passes split connection parameters and discovers services after connection.
It can continue scanning for the second half; do not assume full handshake
serialization is its design.

Cube now passes explicit initial parameters matching that implementation's
Kconfig defaults: interval 6 (7.5 ms), latency 30, supervision timeout 400 (4 s).
Initiator scan interval/window are 60/30 ms. These are requested initial values,
not proof of final negotiated values. No board pins or host profiles are changed.

Keep Cube's encryption-before-discovery guard: first-time Cube side identity and
auto-binding verification depend on it. Instead of initiating SMP inside CONNECT,
schedule it through the existing 100 ms NimBLE telemetry event after at least
100 ms. Check current peer phase, live handle and encryption before each start.
Already encrypted goes directly to discovery; successful/in-progress starts
are not repeated; EBUSY/EAGAIN retry no sooner than 100 ms under the existing
15-second handshake timeout. Fatal errors (including 514) are not ignored.
Disconnect and generation checks still invalidate old work. No extra thread,
sleep, unbounded queue, bond deletion or authentication bypass is introduced.

This corrects differences in connection parameters and callback timing, but is
not a proven root-cause fix for the observed controller error 514. Full ESP-IDF
build and repeated Cube-only power cycles with both halves powered are required.
Production-function ASan/UBSan host tests cover both sides, deferred start,
stale/disconnected peers, already encrypted, bounded busy retries and fatal
errors. Existing connection/identity checks are retained. No automatic Actions.

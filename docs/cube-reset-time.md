# Codex reset clock time (dongle only) — 2026-10-21

The Codex dashboard keeps TODAY TOTAL and its daily-token number. Its former
TOKENS caption now shows a drawn hourglass and primary-window reset HH:MM,
without a RESET/5H label or countdown. Other themes and scanner-branch firmware
are unchanged.

CODEX3 extends the existing frame with reset epoch and timezone offset seconds:
`CODEX3 left weekly tokens age ttl reset_at utc_offset`. Unknown reset is -1.
The Mac supplies the official primary reset timestamp and its timezone offset
at that instant (including DST), never inferring it from percentage or rounded
remaining minutes. Cube does not depend on successful network time sync to
format HH:MM. Offline/unknown/stale quota shows --:--. Daily tokens are unchanged.

Legacy CODEX/CODEX2 frames remain accepted and have unknown reset time. CAPS
still identifies CUBE-CODEX/2 for older companion apps; the optional CAPS3 query
returns CUBE-CODEX/3. Updated companion selects CODEX3 only after this USB probe.
Over Wi-Fi it tries CODEX3 and falls back once to CODEX2 only on HTTP 400, not
authentication or network errors. Both USB and Wi-Fi use the same parser.

Companion and dongle application updates are both needed for reset display.
Old devices remain usable with the companion's fallback. No NVS change/reset or
peripheral firmware update is required. Existing releases are not replaced.

Validation: C++ ASAN/UBSAN parser/format tests cover legacy, UTC offsets,
unknown/offline states and malformed input. Standalone Swift compilation of
the real Cube bridge and official reader passed frame/timezone/DST assertions.
XCTest coverage is added; full macOS GUI tests, SDK build and on-screen hardware
validation remain pending. No build is triggered automatically.

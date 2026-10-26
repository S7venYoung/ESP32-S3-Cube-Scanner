# Fast keyboard-screen layer synchronization

The Cube's active layer used to be sent as `vlcat` inside the one-second system
dashboard publisher, which skipped work while the RPC queue was occupied. Short
momentary layer holds could therefore never be observed on either keyboard screen.

Layer state now uses a bounded latest-value slot per half, separate from the
four-entry ordinary relay queue. Key processing and the existing 5ms input tick
observe changes. Unchanged state is refreshed every second; connection generation
changes force a resend, and a disconnected half loses its pending state. No new
tasks, BLE connections or blocking waits are introduced.

At relay-message boundaries the pump alternates pending layer updates with
ordinary traffic so sustained layer changes cannot starve DYA. An in-flight
fragmented message is never interrupted. Existing generation, encryption and
ready-state checks still gate all writes. This is best-effort state reporting,
not a guarantee that every sub-50ms layer tap will be visible on a display.

The matching `zmk-esp32-cube` keyboard update polls both screen models every 50ms
(previously left 250ms, right 1000ms), retaining unchanged-frame suppression so
system information is not redrawn at 20Hz. The `vlcat` wire format is unchanged:
new Cube firmware remains compatible with older keyboard firmware, but the faster
screen refresh requires updating both keyboard halves.

Local regression tests cover layer changes before the heartbeat, coalescing rapid
press/release, heartbeat refresh, disconnect, new generation, bounded storage,
side fairness and source integration. Hardware timing, DYA coexistence and actual
screen refresh still require verification after building and flashing.

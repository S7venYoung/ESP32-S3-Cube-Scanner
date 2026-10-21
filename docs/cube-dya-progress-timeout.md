# BLE DYA progress deadline — 2026-10-21

The USB+BLE dongle's Studio transport previously timed out only an indication
already accepted by NimBLE. Repeated EBUSY/EAGAIN/ENOMEM at submission reset the
pending flag and could retry indefinitely without reaching that timeout.

The progress deadline now starts with the first chunk attempt and covers both
submission retries and ATT confirmation. Only confirmed progress or session
reset starts a new five-second interval. Fatal submission errors close the
computer connection immediately. Timeout returns without sending another chunk.
The existing disconnect handler isolates and clears that Studio session.

This closes the computer BLE connection, not the left/right keyboard links;
it does not erase bonds, NVS, profiles or keymaps. It cannot repair RF congestion,
and a reconnect is still necessary after a stalled configuration session.

Validation: studio_transport_test.cc compiled with clang++ C++17, -Wall -Wextra
-Werror, AddressSanitizer and UndefinedBehaviorSanitizer and exited successfully.
Regression cases cover exact deadline boundary (including uptime zero), reset,
repeated submission retries without progress, existing fragmentation, ACK
advancement, epoch isolation, RX capacity and bounded TX queues.

The ESP-IDF firmware build and physical BLE/DYA validation have not been run.
No GitHub Action is triggered by this commit. This is a transport stability fix,
not a claim that every DYA feature has been hardware-validated.

# DYA system and USB bindings — 2026-10-21

Standard and fast keymap registration now expose Bootloader (9), Studio Unlock
(10), System Reset (11) and USB Output (12). Existing IDs 1..8 and stored Kind
ordinals are unchanged. Existing HostAction values 0..6 are unchanged; new
values 7..10 represent USB, download, unlock and reboot. Bindings have no
parameters and reject nonzero arguments. Fast metadata count, aliases and
fingerprints include all twelve behaviors.

System Reset reboots Cube; it never factory-resets or erases bonds. Bootloader
requests the existing USB ROM-download path and requires USB attached. Both
wait for physical neutral and completion of release reports on the old host;
if this cannot finish in three seconds, cancel instead of forcing a reset.
Execution is scheduled on the application task, not performed in the BLE host
callback. Studio Unlock uses the same physical-unlock request as BOOT double
click and requires an attached USB or ready BLE Studio connection. It does not
globally disable locking. Held system bindings trigger only once per press.

USB Output selects the existing USB preference through the host mailbox.
Volume Up long press / DONGLE BT NEXT retain the previously implemented cycle
USB -> BLE 1..5 -> USB. No automatic fallback policy is introduced. System
bindings target Cube, not keyboard-half bootloaders; peripheral firmware is
unchanged. Optional encoder/combo engines are not extended to execute system
reset actions in this patch.

The compact yellow-notice change is already on the same branch (b3ad1bca):
14 px visual text, fixed-width single line, ellipsis, 24 px banner and padding.
It needs a new build before users see it.

Host tests check all new binding wire round-trips, parameter rejection,
metadata, saved-model reload, fast-keymap count and one-shot press semantics.
SDK build and physical DYA categorization, USB switching and reboot/download
validation remain pending. Existing spontaneous watchdog reboots are not
claimed fixed by these added behaviors. No action is triggered automatically.

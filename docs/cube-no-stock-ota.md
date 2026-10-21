# Dual dongle: no automatic stock firmware check — 2026-10-21

The USB+BLE Cube build ignores the firmware metadata in XiaoZhi bootstrap
responses entirely, including the force flag. Startup does not call
UpgradeFirmware or display the checking-version status. These guards are
unconditional for CONFIG_CUBE_BLE_DONGLE_OUTPUT; other board paths are unchanged.

One bootstrap request remains necessary for activation, server time and voice
transport configuration. It is not an automatic firmware update check. If that
request fails, return from the version/bootstrap loop immediately instead of
the ten-attempt exponential retry and update-failure alert loop. Activation
handling after a successful response is retained. A failed bootstrap can leave
voice unavailable; it does not justify fabricating activation or config data.

This does not prove the task-watchdog reset has been fixed. No watchdog is
disabled and no pairing, Wi-Fi or activation data is erased. Manual USB flashing
is unchanged. The firmware has not been SDK-built or physically verified yet.

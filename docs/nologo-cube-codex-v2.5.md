# Nologo Xingzhi Cube 1.54 TFT Wi-Fi / Codex + Xiaozhi v2.5

Target: official v2.5.0 nologo/xingzhi-cube-1.54tft-wifi. Custom build variant:
xingzhi-cube-1.54tft-wifi-codex. Do not use this firmware on Zhengchen hardware.

Features inherited from the current main branch: Codex, Macintosh and Arcade
themes; Arcade default when no saved choice exists; volume-down long press
cycles themes; voice theme switching and floating Xiaozhi assistant; always-on
brightness; USB-UART and Wi-Fi companion sync; BLE Prospector modifiers and
CUBE-FIGHT left/right WPM/battery telemetry; retained fighter assets, idle and
attack animations; VS/TODAY hide while WPM is positive and recover after 1.5s
at zero; right gauges empty from left to right; text-free battery gauges with
orange low battery and a neutral unknown marker. No fake keyboard data.

Official hardware mapping is preserved:
- ST7789 240x240, SPI3: MOSI10/SCLK9/DC8/CS14/RESET18/backlight13.
- Buttons: BOOT0, volume-up40, volume-down39.
- Microphone WS4/SCK5/DIN6; speaker DOUT7/BCLK15/LRCK16.
- Power hold21, charging38, battery ADC2 channel6 (official calibration retained).
- Companion serial: UART0 TX43/RX44, 115200. A USB-to-UART path is required;
  native USB Serial/JTAG is not enabled by this variant.

Battery acquisition starts after ADC initialization. Temporary ADC read errors
do not reboot the board; battery remains unknown until enough samples arrive.
ADC2/Wi-Fi coexistence, orientation, buttons, sound, charging and microphone
still require hardware validation. Compilation alone does not prove them.

Build: bash scripts/build-nologo-cube-v25.sh (ESP-IDF v6.1).
The stock Nologo build remains available without scanner mode.

First flash for this port: use merged-binary.bin at 0x0, ESP32-S3 / 16MB /
DIO / 80MHz, serial baud115200. Back up the existing device first. Do not use
the Zhengchen-only preset in Cube Flash. The official release ZIP linked by
the user is a stock firmware, not the custom dashboard firmware.

Application image: xingzhi_cube_154_wifi_codex.bin (same bytes as xiaozhi.bin).
App-only flashing requires the same partition layout and known active OTA slot;
do not assume an address or use an app image to recover an erased bootloader.

# ESP32-S3 Cube ZMK Scanner

Standalone BLE scanner firmware for the NologoTech Xingzhi Cube 2.0 TFT (ESP32-S3). It discovers named BLE advertisements from ZMK split keyboard halves and shows the advertised name, BLE address, and signal strength on the 240 × 292 display.

This is an observer. ZMK split key events travel over an encrypted connection, so the scanner reports presence and signal strength rather than key presses.

## Build and flash

Install ESP-IDF 5.x and activate its environment, then run:

```sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build flash monitor
```

Set `Cube ZMK scanner -> Advertised name filter` to a substring from the keyboard's advertised BLE name. Leave it empty to show every named BLE device.

## Cube 2.0 TFT wiring

| Signal | ESP32-S3 GPIO |
| --- | ---: |
| LCD MOSI | 10 |
| LCD SCLK | 9 |
| LCD DC | 8 |
| LCD CS | 14 |
| LCD reset | 18 |
| Backlight | 13 |

The display uses ST7789, 292 × 240 pixels, SPI mode 3. BLE scanning uses the ESP-IDF NimBLE observer role.

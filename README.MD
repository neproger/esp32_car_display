# ESP32 Car Display

Base ESP-IDF project for `Waveshare ESP32-C6 LCD 1.47"`.

Current status:
- Added a minimal LCD bring-up for ST7789T (SPI) using ESP-IDF `esp_lcd`.
- Added backlight PWM control.
- Added LVGL integration and test UI:
  - fixed UI orientation is landscape (`320x172`)
  - red border around full active area
  - centered text (`CENTER TEST`)
- Added local `kwp1281` ESP-IDF component based on `KLineKWP1281Lib_ESP32`
- Split app structure into layers:
  - `display`
  - `ui`
  - `state`
  - `kline_uart` + `kwp1281`

## Display config (from Waveshare official demo)

- Resolution: `172x320`
- Controller: `ST7789T`
- SPI host: `SPI2_HOST`
- Pins:
  - `SCLK=7`
  - `MOSI=6`
  - `CS=14`
  - `DC=15`
  - `RST=21`
  - `BL=22`
- Frame offset: `X=34`, `Y=0`
- Fixed UI mode used by this project:
  - hardware rotation: landscape
  - LVGL resolution: `320x172`
  - flush offset: `X=0`, `Y=34`

## Build and flash

1. Open ESP-IDF shell.
2. Configure target:
   - `idf.py set-target esp32c6`
3. Build:
   - `idf.py build`
4. Flash and monitor:
   - `idf.py -p <PORT> flash monitor`

If LVGL is initialized correctly, you should see a red border and centered text.

## Next step

Replace placeholder K-line pins/config with the real interface wiring and start querying a control module.

> Part of [**app-pixels.com**](https://www.app-pixels.com) — browse + flash this app at [`/apps/worldclock`](https://www.app-pixels.com/apps/worldclock).

# worldclock

**World Clock** · v1.0.2

Landscape dot-matrix world clock. BOOT cycles through 21 cities (one per major UTC offset); `HOME_TIMEZONE` in `setup.txt` picks the boot city.

**Hardware:** Waveshare ESP32-S3 AMOLED 1.8"

**Tags:** `#COMMUNITY` `#clock`

NTP-synced once at boot. After that, every BOOT press just changes the displayed timezone — the device already knows UTC, so switching cities is instant and offline. Departure-board dot-matrix look, locked to landscape (448×368).

## Controls

```
  BOOT short — cycle to the next city's local time
  PWR  short — cycle brightness
```

## SD card setup

Place a `/setup/setup.txt` on the SD card. Recognised keys:

```
SSID =
PASSWORD =
SSID2 =
PASSWORD2 =
SSID3 =
PASSWORD3 =
HOME_TIMEZONE =
CLOCK_COLOR =
BRIGHTNESS =
TIMEOUT =
```

`HOME_TIMEZONE` is matched against the built-in city list (case-insensitive). One per major UTC offset, ordered west-to-east:

```
HONOLULU      ANCHORAGE     L.A.          DENVER
CHICAGO       NEW YORK      SANTIAGO      SAO PAULO
LONDON        BERLIN        CAIRO         ATHENS
MOSCOW        DUBAI         MUMBAI        DHAKA
BANGKOK       SHANGHAI      TOKYO         SYDNEY
AUCKLAND
```

Defaults to `BERLIN` if the value isn't recognised.

## Build

1. Install [arduino-cli](https://arduino.github.io/arduino-cli/) or Arduino IDE 2.x.
2. Add the ESP32 board package (≥ 3.1.0):

   ```
   arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli core install esp32:esp32 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Install the required Arduino libraries:

   - Adafruit XCA9554
   - GFX Library for Arduino (moononournation)
   - XPowersLib (lewishe)

4. Compile and upload:

   ```
   FQBN='esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600,LoopCore=1,EventsCore=1'
   arduino-cli compile -b "$FQBN" --build-path /tmp/worldclock_build .
   arduino-cli upload  -b "$FQBN" --input-dir /tmp/worldclock_build -p /dev/ttyACM0 .
   ```

   For browser flashing without a build environment, use the [pre-built binary](https://www.app-pixels.com/apps/worldclock).

## License

MIT — see [LICENSE](LICENSE). Do whatever you want with it.

---

Part of the [app-pixels.com](https://www.app-pixels.com) catalogue · live listing: https://www.app-pixels.com/apps/worldclock

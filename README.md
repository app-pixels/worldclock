# worldclock

Landscape dot-matrix world clock for the Waveshare ESP32-S3-Touch-AMOLED-1.8,
built on the app-pixels framework.

NTP-synced. Press **BOOT** to cycle through 8 cities. Press **PWR** to dim.

## Setup

In `/setup/setup.txt` on the SD card:

```
SSID = your-wifi
PASSWORD = your-password
HOME_TIMEZONE = BERLIN
```

`HOME_TIMEZONE` picks which city is shown on boot. Recognised values:

```
LOS ANGELES   NEW YORK   LONDON      BERLIN
DUBAI         MUMBAI     TOKYO       SYDNEY
```

Defaults to `BERLIN` if the value isn't recognised.

Optional:

```
CLOCK_COLOR = #FBCC04    # override the default white+yellow palette
BRIGHTNESS  = 180        # 1–255
TIMEOUT     = 0          # seconds to auto-off; 0 disables
```

## Build

Submit via [app-pixels.com/submit](https://www.app-pixels.com/submit) — CI
compiles against the canonical sketchbook. Local build uses the same FQBN as
every other app-pixels app.

## License

MIT — see [LICENSE](LICENSE). Do whatever you want with it.

# Zentouch

A gesture-controlled NeoPixel ring lamp. Wave your hand near the sensor to
turn the lamp on/off, cycle through preset colors, or adjust brightness -
no buttons, no app.

Author: Emmanuel Romain — Firmware version 2.0 - New version use OTA and other features

## Hardware

- **Controller:** Adafruit Feather HUZZAH (ESP8266)
- **Gesture sensor:** PAJ7620(U2) I2C gesture sensor
- **Lighting:** 12-LED NeoPixel ring (WS2812-family, wired for `NEO_RGB` color order)

### Pin mapping

| Signal              | Feather HUZZAH pin |
|---------------------|--------------------|
| NeoPixel ring data  | GPIO12 (`LED_PIN`) |
| PAJ7620 SCL (I2C)   | GPIO5              |
| PAJ7620 SDA (I2C)   | GPIO4              |

## Required libraries

- [`Adafruit_NeoPixel`](https://github.com/adafruit/Adafruit_NeoPixel)
- `EEPROM` (bundled with the ESP8266 Arduino core)
- `paj7620.h` / `paj7620.cpp` — **not included in this folder.** This is the
  PAJ7620(U2) gesture-sensor driver (commonly distributed as part of
  Seeed Studio's Gesture_PAJ7620 library). You must source it separately
  and add it to your Arduino `libraries` folder (or alongside the sketch)
  before this project will compile.

## How it works

On boot, the sketch initializes the NeoPixel ring and the PAJ7620 sensor,
plays a short rainbow fade-in/out animation, restores the last saved
brightness from EEPROM (if any), then flashes the ring once to report
sensor init status. From then on, `loop()` continuously polls the PAJ7620
for a detected gesture and reacts accordingly.

### Gesture -> behavior

| Gesture                        | Effect                                            |
|---------------------------------|----------------------------------------------------|
| Swipe right                    | Next color in the preset palette                   |
| Swipe left                     | Previous color in the preset palette               |
| Swipe down                     | Turn the ring off                                  |
| Swipe up                       | Turn the ring on (at current color/brightness)     |
| Rotate fingers (clockwise)     | Enter brightness mode, decrease brightness         |
| Rotate fingers (counter-clockwise) | Enter brightness mode, increase brightness     |
| Swipe down (while in brightness mode) | Save brightness to EEPROM and exit the mode |

While color-cycling, brightness is clamped between `BRIGHTNESS_MIN` (25)
and `BRIGHTNESS_MAX` (240) in the firmware.

> **Note on the numeric gesture codes:** `handleGestures()` matches raw
> integer flags read from the PAJ7620 (`2`, `1`, `16`, `32`, `64`, `128`)
> rather than named constants from `paj7620.h`, because that header isn't
> bundled here and its exact `GES_*_FLAG` values weren't available to
> double-check against. These values were kept unchanged from the
> original, hardware-verified firmware. If you drop in a different build
> of `paj7620.h`, confirm its flag values still line up with the table
> above.

### Color palette

The ring is constructed with `NEO_RGB + NEO_KHZ800`, meaning
`strip.Color(r, g, b)` is sent to the LEDs without channel re-ordering.
The color constants in the sketch (`colorGreen`, `colorWhite`,
`colorSkyBlue`, `colorViolet`, `colorTurquoise`, `colorEmerald`,
`colorLimeGreen`, `colorYellow`, `colorRed`) were renamed to match what
they actually render — two of the original names (`colorRed` /
`colorGreen`) were swapped relative to their real rendered color. Only
names were changed; the underlying RGB values (and therefore the lamp's
actual on-ring colors) are unchanged from the original firmware.

## Flashing

1. Install the ESP8266 board package in the Arduino IDE (or PlatformIO)
   and select "Adafruit Feather HUZZAH ESP8266" as the board.
2. Install `Adafruit_NeoPixel` via the Library Manager.
3. Obtain `paj7620.h`/`paj7620.cpp` (see above) and place them where your
   toolchain will find them (e.g. a `libraries/PAJ7620/` folder, or next
   to the `.ino` file).
4. Open `zentouch/zentouch.ino`, select the correct serial
   port, and upload.
5. Open the Serial Monitor at 115200 baud to see init/status messages.

## Known limitations / TODOs

- The exact meaning of each numeric gesture flag (see table above) is
  taken on faith from the original, physically-tested firmware; it
  couldn't be independently re-verified here since `paj7620.h` isn't part
  of this folder. Treat the flag numbers as a black box unless you can
  test on real hardware.
- The ready/error startup flash intentionally shows `colorRed` on success
  and `colorGreen` on sensor error — the reverse of the usual
  green-is-good/red-is-bad convention. This was already true in the
  original firmware (the color constants were simply mislabelled), and
  was left as-is since it only affects which color flashes, not any
  functional behavior. Swap the two calls in `setup()` if you'd rather
  match the usual convention.
## Related

`pictures/` (sibling folder, not part of the sketch) contains product
photos of the physical lamp in its available colors (White, Violine, Blue,
Pink, Red, Orange, Yellow, Green).

## License

MIT — see [LICENSE](LICENSE).

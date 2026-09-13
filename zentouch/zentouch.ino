/*
  Zentouch Simple Gesture controlled NeoPixel ring
  Author:            Emmanuel Romain
  Version:           2.0
  Controller Board:  Feather HUZZAH (ESP8266)

  Wiring note: gesture sensor (PAJ7620) connected via I2C,
  yellow-marked wires -> SCL = GPIO5, SDA = GPIO4.
*/

#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include "paj7620.h"

/* ---- Gesture sensor state ---- */
// Minimum time (ms) to wait after handling a gesture before the sensor
// is polled again, so a single hand movement isn't read as several gestures.
#define GES_QUIT_TIME  350

uint8_t gestureFlags = 0;   // last gesture code read from the PAJ7620 (see handleGestures())
uint8_t sensorError = 0;    // 0 = PAJ7620 initialised OK, non-zero = init/communication error

/* ---- NeoPixel ring ---- */
const byte LED_PIN = 12;        // GPIO driving the NeoPixel ring (DIN)
const uint16_t LED_COUNT = 12;  // number of LEDs on the ring

// Brightness bounds and current value used with strip.setBrightness().
// setBrightness() takes 0-255; these limits keep the lamp comfortable to
// look at (never fully off/blinding via the brightness gesture) while
// the dedicated "move down" gesture can still switch the ring fully off.
const int BRIGHTNESS_MAX = 240;
const int BRIGHTNESS_MIN = 25;

int currentBrightness = 200;

// Ring is wired as NEO_RGB (i.e. strip.Color(r, g, b) is sent as-is, no
// channel re-ordering), so the constants below are named after the color
// they actually render, not after any earlier/assumed naming.
Adafruit_NeoPixel strip = Adafruit_NeoPixel(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);

// NOTE: names were fixed to match their real rendered color under NEO_RGB.
// Two pairs were previously swapped/mislabelled (e.g. what used to be called
// "colorRed" was actually rendering green, and vice-versa for "colorGreen").
// Numeric values are unchanged from the original firmware so the lamp's
// actual on-ring colors are identical to before - only the names changed.
const uint32_t colorGreen     = strip.Color(0, 255, 0);
const uint32_t colorWhite     = strip.Color(220, 220, 220);
const uint32_t colorSkyBlue   = strip.Color(30, 149, 255);
const uint32_t colorViolet    = strip.Color(127, 0, 255);
const uint32_t colorTurquoise = strip.Color(0, 255, 203);
const uint32_t colorEmerald   = strip.Color(0, 237, 0);
const uint32_t colorLimeGreen = strip.Color(80, 243, 20);
const uint32_t colorYellow    = strip.Color(243, 255, 20);
const uint32_t colorRed       = strip.Color(250, 22, 70);

// Sentinel indices used to wrap the color selection around colorPalette[].
// Index 0 and index colorIndexHighSentinel are NOT meant to be reached by
// normal gesture cycling (they act as "one past each end" markers) - the
// user-selectable colors are colorPalette[1..colorIndexHighSentinel-1].
uint8_t colorIndexLowSentinel = 0;
uint8_t colorIndexHighSentinel = 9;

int currentColorIndex = 1;

// Colors offered to the "swipe left/right" gesture.
uint32_t colorPalette[9] = {
  colorGreen,
  colorWhite,
  colorSkyBlue,
  colorViolet,
  colorTurquoise,
  colorEmerald,
  colorLimeGreen,
  colorYellow,
  colorRed
};

// Colors used for the power-on rainbow wipe animation only (one entry per LED).
uint32_t startupColors[12] = {
  colorWhite,
  strip.Color(119, 181, 254),
  strip.Color(90, 161, 254),
  strip.Color(49, 140, 231),
  strip.Color(49, 160, 255),
  strip.Color(85, 184, 251),
  strip.Color(75, 224, 210),
  strip.Color(60, 205, 175),
  strip.Color(175, 105, 175),
  strip.Color(235, 55, 235),
  strip.Color(175, 75, 175),
  strip.Color(205, 125, 205)
};

// EEPROM layout: brightness value is stored as ASCII digits starting at
// address 3 (addresses 0-2 are left free/reserved, matching the original
// firmware's layout). Up to 3 digits are enough since BRIGHTNESS_MAX < 1000.
const int EE_BRIGHTNESS_ADDR = 3;
const int EE_BRIGHTNESS_LEN  = 3;

void setup()
{
  Serial.begin(115200);

  delay(500);

  Serial.println(F("> Initialise ring led"));

  strip.begin();
  strip.show();
  strip.setBrightness(1);

  Serial.println(F("> Initialise gesture"));

  sensorError = paj7620Init();

  // Power-on animation: fade brightness up while showing the rainbow
  // pattern, then fade back down before applying the saved brightness.
  fadeBrightnessUp(1, currentBrightness, 50);
  fadeBrightnessDown();

  if (sensorError)
  {
    delay(1000);
    // Retry gesture sensor initialisation once before giving up.
    sensorError = paj7620Init();
  }

  // Restore last saved brightness from EEPROM, if any was saved.
  String eeplight = readEEPROM(EE_BRIGHTNESS_ADDR, EE_BRIGHTNESS_LEN);

  if (eeplight.length() > 1) {
    Serial.println(F("> SetBrightness"));
    Serial.println(eeplight);

    currentBrightness = eeplight.toInt();
  }

  fadeBrightnessUp(50, currentBrightness, 25);

  // Flash the ring to indicate whether the gesture sensor came up OK.
  // NOTE: with the color names now matching their actual rendered color,
  // the "ready" flash below renders as colorRed and the "error" flash
  // renders as colorGreen - the reverse of the usual red=error/green=ok
  // convention. This was already the case in the original firmware (the
  // constants were simply mislabelled before); left unchanged here since
  // it only affects which color is shown, not any functional behavior.
  if (!sensorError)
  {
    currentColorIndex = 8;
    fillRing(colorRed);
    Serial.println(F("> All is ready, enjoy!"));
  }
  else {
    currentColorIndex = 0;
    fillRing(colorGreen);
  }

  strip.show();

  Serial.println(F("> Setup Finished"));
}


void loop()
{
  handleGestures();
}


// Reads the gesture register and reacts to whichever single gesture flag
// is currently set. Flags are read from PAJ7620 register 0x43:
//   2   = swipe right   -> next color
//   1   = swipe left    -> previous color
//   16  = swipe down    -> turn ring off
//   32  = swipe up      -> turn ring on
//   64/128 = clockwise / counter-clockwise finger rotation -> enter
//            brightness-adjustment mode until a "swipe down" confirms/saves
// TODO: these numeric flag values come from the vendor paj7620.h header,
// which isn't included in this folder (see README). They were kept as-is
// from the original, verified-on-hardware firmware; if you swap in a
// different version of paj7620.h, double-check its GES_*_FLAG values still
// match these numbers.
void handleGestures()
{
  gestureFlags = 0;

  sensorError = paj7620ReadReg(0x43, 1, &gestureFlags); // read detected gesture(s)

  if (!sensorError && gestureFlags != 0)
  {
    if (gestureFlags == 2) // swipe right -> next color
    {
      if (isLightOff() == false)
      {
        currentColorIndex += 1;
        if (currentColorIndex == colorIndexHighSentinel)
        {
          currentColorIndex = colorIndexLowSentinel + 1;
        }
      }

      lightOn();
      delay(GES_QUIT_TIME);
    }

    if (gestureFlags == 1) // swipe left -> previous color
    {
      if (isLightOff() == false)
      {
        currentColorIndex -= 1;
        if (currentColorIndex == colorIndexLowSentinel)
        {
          currentColorIndex = colorIndexHighSentinel - 1;
        }
      }
      lightOn();
      delay(GES_QUIT_TIME);
    }

    if (gestureFlags == 16) // swipe down -> turn ring off
    {
      lightOff();
      delay(2000);
    }

    if (gestureFlags == 32) // swipe up -> turn ring on
    {
      lightOn();
      delay(GES_QUIT_TIME);
    }

    if (gestureFlags == 64 || gestureFlags == 128) // rotating fingers -> brightness mode
    {
      bool adjustingBrightness = true;

      // Visual cue for "brightness adjustment mode": white ring with a
      // pink cross marking every quarter LED.
      flashCrossPattern(colorWhite, colorTurquoise);

      while (adjustingBrightness)
      {
        gestureFlags = 0;
        paj7620ReadReg(0x43, 1, &gestureFlags);
        if (gestureFlags != 0)
        {
          if (gestureFlags == 64)
          {
            decreaseBrightness();
          }
          else
          {
            if (gestureFlags == 128)
            {
              increaseBrightness();
            }
            else {
              if (gestureFlags == 16) // swipe down -> confirm and save
              {
                Serial.println(F("> Save config"));
                saveEEPROM(String(currentBrightness), EE_BRIGHTNESS_ADDR, EE_BRIGHTNESS_LEN);
                Serial.println(F("> Config saved"));
                adjustingBrightness = false;
              }
            }
          }
        }
      }
      lightOn();
      delay(GES_QUIT_TIME);
    }
  }
}


// Increases brightness by a fixed step, clamped to BRIGHTNESS_MAX.
void increaseBrightness() {
  currentBrightness = currentBrightness + 20;
  if (currentBrightness >= BRIGHTNESS_MAX)
  {
    currentBrightness = BRIGHTNESS_MAX;
  }
  strip.setBrightness(currentBrightness);
  strip.show();
  delay(5);
}

// Decreases brightness by a fixed step, clamped to BRIGHTNESS_MIN
// (the dedicated "swipe down" gesture is used to fully turn the ring off).
void decreaseBrightness() {
  currentBrightness = currentBrightness - 20;
  if (currentBrightness <= BRIGHTNESS_MIN)
  {
    currentBrightness = BRIGHTNESS_MIN;
  }
  strip.setBrightness(currentBrightness);
  strip.show();
  delay(5);
}

// Turns the ring on at the current brightness/color.
bool lightOn() {
  strip.setBrightness(currentBrightness);
  strip.show();
  colorWipe(colorPalette[currentColorIndex], 50);
  return true;
}

// Turns the ring off by setting brightness to 0 (color selection is kept).
bool lightOff() {
  strip.setBrightness(0);
  strip.show();
  return false;
}

bool isLightOff() {
  uint8_t brightness = strip.getBrightness();
  return (brightness == 0);
}


// Ramps brightness from sMin to sMax (inclusive) while showing the
// startup rainbow pattern, used for the power-on fade-in effect.
void fadeBrightnessUp(int sMin, int sMax, int stepDelay) {
  for (uint16_t i = sMin; i <= sMax; i++) {
    strip.setBrightness(i);
    showStartupColors();
    delay(stepDelay);
  }
}

// Ramps brightness back down from currentBrightness to 50, used right
// after the power-on fade-in. Must run before currentBrightness is
// overwritten by the EEPROM-restored value (see setup()), otherwise a
// saved brightness below 50 would make the loop counter (unsigned) wrap.
void fadeBrightnessDown() {
  for (uint16_t i = currentBrightness; i >= 50; i--) {
    strip.setBrightness(i);
    showStartupColors();
    delay(25);
  }
}

// Displays the fixed startup rainbow pattern on the ring.
void showStartupColors() {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, startupColors[i]);
  }
  strip.show();
}

// Lights each LED in turn with the given color (a simple "wipe" animation).
void colorWipe(uint32_t color, uint8_t wait)
{
  int step = 0;
  int pos = 0;
  do
  {
    strip.setPixelColor(pos, color);
    strip.show();

    delay(wait);

    pos += 1;
    if (pos == LED_COUNT)
    {
      pos = 0;
    }
    step += 1;
  } while (step < LED_COUNT);
}

// Fills the ring with initcolor, then overlays fourColors on 4 evenly
// spaced LEDs (a "+" cross pattern) - used as a mode indicator.
void flashCrossPattern(uint32_t initcolor, uint32_t fourColors) {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, initcolor);
  }
  if (LED_COUNT % 4 == 0)
  {
    int quarter = LED_COUNT / 4;
    strip.setPixelColor(0, fourColors);
    strip.setPixelColor(quarter, fourColors);
    strip.setPixelColor(quarter * 2, fourColors);
    strip.setPixelColor(quarter * 3, fourColors);
  }
  strip.show();
}


// Fills the whole ring with a single solid color.
void fillRing(uint32_t colorRing)
{
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, colorRing);
  }
  strip.show();
}

// Writes `buffer` (up to `length` bytes) into EEPROM starting at `startAddr`.
void saveEEPROM(String buffer, int startAddr, int length) {
  EEPROM.begin(512);
  delay(10);
  for (int pos = 0; pos < length; ++pos) {
    EEPROM.write(startAddr + pos, buffer[pos]);
  }
  EEPROM.commit();
}

// Reads `length` bytes from EEPROM starting at `startAddr`, keeping only
// alphanumeric characters (EEPROM cells that were never written read back
// as 0xFF, which is filtered out here so an empty/unused EEPROM yields an
// empty string rather than garbage).
String readEEPROM(int startAddr, int length) {
  EEPROM.begin(512);
  delay(10);
  String buffer;
  for (int pos = startAddr; pos < startAddr + length; ++pos)
    if (isAlphaNumeric(EEPROM.read(pos)))
      buffer += char(EEPROM.read(pos));
  return buffer;
}

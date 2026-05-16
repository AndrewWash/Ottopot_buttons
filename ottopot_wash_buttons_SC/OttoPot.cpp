/*
OTTOPOT
LICENSE: GPL v3 (http://www.gnu.org/licenses/gpl.html)
*/

#include "OttoPot.h"
#include "LEDRingSmall.h"
#include "debug.h"
#include <Arduino.h>
#include <Mux.h>

OttoPot::OttoPot(admux::Mux *rmux, admux::Mux *rmux2, int rmuxc, int rcc,
                 int rchannel, uint8_t ledRingAddress)
    : leds(ledRingAddress) {
  cc = rcc;
  channel = rchannel;
  mux = rmux;
  mux2 = rmux2;
  muxc = rmuxc;

  value = 0;
  dzValue = 0;
  pendingDelta = 0;
  receivedHSB = 0;
  receivedLSB = 0;

  previousMillis = millis();
  previousMicros = micros();
  interactionMillis = millis();

  offColor.setAll(0);
  onColor.r = COLOR_R;
  onColor.g = COLOR_G;
  onColor.b = COLOR_B;

  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    currentLEDs[i].setAll(0);
    newLEDs[i].setAll(0);
  }
#ifdef DEBUG_DZ_LOGS
  dzMax = 0;
  dzIntervalMillis = 0;
#endif
}

void OttoPot::initialize() {
  mux->channel(muxc);
  pot.updateValues(mux->read(), mux2->read());
  leds.LEDRingSmall_Reset();
  delay(20);
  leds.LEDRingSmall_Configuration(0x01);
  leds.LEDRingSmall_PWMFrequencyEnable(1);
  leds.LEDRingSmall_SpreadSpectrum(0b0010110);
  leds.LEDRingSmall_GlobalCurrent(0x05);
  leds.LEDRingSmall_SetScaling(0xFF);
  leds.LEDRingSmall_PWM_MODE();
}

// Re-seed the pot's previousValueA/B baseline from a fresh read. initialize()
// seeds it once, but the ~1600 ms boot intro runs before the first loop() — a
// knob moved during the intro would otherwise produce a spurious first delta.
void OttoPot::reseedBaseline() {
  mux->channel(muxc);
  pot.updateValues(mux->read(), mux2->read());
}

void OttoPot::sendMidiCC(int rawVal) {
  // rawVal is already integral; shift/mask give integer hsb/lsb directly.
  // The old float + round(floor()) / round(ceil()) round-trip was a no-op.
  int hsb = rawVal >> 7;
  int lsb = rawVal & 127;

  usbMIDI.sendControlChange(cc, hsb, channel);
  usbMIDI.sendControlChange(cc + 32, lsb, channel);
}

void OttoPot::handleControlChange(byte rchannel, byte rcontrol, byte rvalue) {
  if (rchannel - 1 == muxc) {
    uint8_t cvalue = map(rvalue, 0, 127, 0, 255);
    switch (rcontrol) {
    case 101:
      onColor.r = cvalue;
      break;
    case 102:
      onColor.g = cvalue;
      break;
    case 103:
      onColor.b = cvalue;
      break;

    case 104:
      offColor.r = cvalue;
      break;
    case 105:
      offColor.g = cvalue;
      break;
    case 106:
      offColor.b = cvalue;
      break;
    }
  }

  if (rchannel != channel)
    return;
  unsigned long currentMillis = millis();

  // Only act on incoming CCs if there hasn't been manual interaction for a bit
  if (currentMillis - interactionMillis >= 100) {
    if (rchannel == channel && (rcontrol == cc || rcontrol == cc + 32)) {
      if (rcontrol == cc)
        receivedHSB = rvalue;
      if (rcontrol == cc + 32) {
        receivedLSB = rvalue;
      }
      int nvalue = (receivedHSB << 7) + receivedLSB;
      setNewValue(map(nvalue, 0, 16383, 0, MAX_POT_VALUE));
    }
  }
}

void OttoPot::setNewValue(int newValue) {
  if (newValue > value) {
    for (uint8_t i = map(value, 0, MAX_POT_VALUE, 0, 22);
         i < map(newValue, 0, MAX_POT_VALUE, 0, 22); i++) {
      newLEDs[23 - i].r = onColor.r;
      newLEDs[23 - i].g = onColor.g;
      newLEDs[23 - i].b = onColor.b;
    }
  } else {
    for (uint8_t i = map(newValue, 0, MAX_POT_VALUE, 0, 22);
         i < map(value, 0, MAX_POT_VALUE, 0, 22); i++) {
      newLEDs[23 - i].r = onColor.r;
      newLEDs[23 - i].g = onColor.g;
      newLEDs[23 - i].b = onColor.b;
    }
  }
  value = newValue;
}

int sort_desc(const void *cmp1, const void *cmp2) {
  int a = *((int *)cmp1);
  int b = *((int *)cmp2);
  return a > b ? -1 : (a < b ? 1 : 0);
}

/*
Let's talk about the deadzone handling a bit.
Since the resolution is so high and we're using analog potentiometers, it is
much easier to get unwanted parameter changes; analog noise, microscopic
movements, maybe cosmic radiation or something like that. You *really* don't
want a CC sent when you're not actively changing a value since there's things
like automation takeover that can easily ruin a whole live set with one stray
CC. With regular 7 bit CCs, the chance for a CC to fire when you're not moving
the dial is relatively low. In my experience, you can just ignore the issue and
things will most likely be fine. With our higher resolution, things get more
complicated. There are several methods here to work against this:
- Read the analog value multiple times and use the median
- Use the lower of the delta values of both wipers (in EndlessPotentiometer.cpp)
- And last but certainly not least the deadzone
The idea of the deadzone is that very small changes are ignored until they reach
a certain threshold — then the pot is “unlocked” and even the smallest movements
from this point on will be transmitted until the average movement over a certain
time has gotten so low that we put the pot back into the “locked” status again.
This was super important during the inital revisions but the other methods were
added later and helped so much that the deadzone is more of a final safety net
now and it is probably too complicated for how little it actually does. It still
is needed and I like how the debugging constants I added help to tune the
parameters to keep it as low as possible, so I'm leaving it in its current
state.
*/
void OttoPot::updateValue(unsigned long currentMillis,
                          unsigned long currentMicros) {

#define READS 5
#define MEDIAN 2

  int delta = 0, newValue;
  int pin1[READS];
  int pin2[READS];
  mux->channel(muxc);

  // Throwaway read: charge the ADC sample/hold cap to the newly-selected mux
  // channel before the real reads, avoiding cross-channel ghosting on the
  // first conversion.
  (void)mux->read();
  (void)mux2->read();

  for (uint8_t i = 0; i < READS; i++) {
    pin1[i] = mux->read();
    pin2[i] = mux2->read();
  }

  // Sort pin read values and use median
  uint8_t lt_length = sizeof(pin1) / sizeof(pin1[0]);
  qsort(pin1, lt_length, sizeof(pin1[0]), sort_desc);
  qsort(pin2, lt_length, sizeof(pin1[0]), sort_desc);

  pot.updateValues(pin1[MEDIAN], pin2[MEDIAN]);
  // I *think* overflows are no problem this way? Not 100% sure but I haven't
  // noticed any problems
  unsigned long deltaMicros = currentMicros - previousMicros;

  delta = pot.linearDelta;

  dzValue = max(dzValue, 0.0f);
  dzValue += abs(delta);
  dzValue = min(dzValue, 40.0f);

#ifdef DEBUG_DZ_LOGS
  dzMax = max(dzMax, (int)dzValue);
  if (currentMillis - dzIntervalMillis >= 5000) {
    debugln("dzMax: %d", dzMax);
    dzMax = 0;
    dzIntervalMillis = currentMillis;
  }
#endif

  if (dzValue > 10) {
    interactionMillis = currentMillis;
    // Flush the run-up travel that accumulated while the deadzone was locked,
    // so the gesture doesn't visibly lag the knob on unlock.
    newValue = value + delta + pendingDelta;
    pendingDelta = 0;
    if (newValue < 0) {
      newValue = 0;
    } else if (newValue > MAX_POT_VALUE) {
      newValue = MAX_POT_VALUE;
    }
    if (newValue != value) {
      setNewValue(newValue);
      sendMidiCC(map(value, 0, MAX_POT_VALUE, 0, 16383));
    }
  } else {
    // Locked: buffer travel so the run-up isn't dropped when we unlock.
    pendingDelta += delta;
  }

  // Time-based decay, decoupled from loop rate: ~1.0 per 5 ms regardless of
  // how fast the loop runs (the old integer deltaMicros/5000 truncated to 1,
  // sometimes 0, and depended entirely on delay(5) existing).
  dzValue -= (float)deltaMicros / 5000.0f;
  dzValue = max(dzValue, 0.0f);
  // Relocked: discard the buffered run-up so a slow noise drift while locked
  // can't quietly build up into a latent jump.
  if (dzValue <= 0.0f) {
    pendingDelta = 0;
  }
  previousMillis = currentMillis;
  previousMicros = currentMicros;

// Debugging graph for tuning the dead zone
#ifdef DEBUG_DZ_TUNE
  if (muxc == 0) {
    debug("%dpin1:%d", muxc, pin1[MEDIAN]);
    debug(",%dpin2:%d", muxc, pin2[MEDIAN]);
  }
  if (muxc == 0) {
    debugln(",interaction:%d,%ddzValue:%d", (dzValue > 10) * 4000, muxc,
            (int)max(dzValue * 100.0f, 0.0f));
  }
#endif
}

void OttoPot::setOffColor(uint8_t r, uint8_t g, uint8_t b) {
  offColor.r = r;
  offColor.g = g;
  offColor.b = b;
}

void OttoPot::updateLEDs() {

  uint8_t t_r = onColor.r;
  uint8_t t_g = onColor.g;
  uint8_t t_b = onColor.b;

  newLEDs[0].setAll(0);

  for (uint8_t i = 23; i >= 1; i--) {
    newLEDs[i].r = max(newLEDs[i].r - 40, offColor.r);
    newLEDs[i].g = max(newLEDs[i].g - 40, offColor.g);
    newLEDs[i].b = max(newLEDs[i].b - 40, offColor.b);
  }

  float targetLeds = (float)value / (float)MAX_POT_VALUE * (22.0);
  int col2 = (targetLeds - (int)targetLeds) * 255;
  int col1 = 255 - col2;
  int ledNum = 22 - floor(targetLeds);

  if (ledNum > 0) {
    newLEDs[ledNum].r = map(col2, 0, 255, offColor.r, t_r);

    newLEDs[ledNum].g = map(col2, 0, 255, offColor.g, t_g);

    newLEDs[ledNum].b = map(col2, 0, 255, offColor.b, t_b);
  }

  newLEDs[ledNum + 1].r = map(col1, 0, 255, offColor.r, t_r);
  newLEDs[ledNum + 1].g = map(col1, 0, 255, offColor.g, t_g);
  newLEDs[ledNum + 1].b = map(col1, 0, 255, offColor.b, t_b);

  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    if (currentLEDs[i].r != newLEDs[i].r) {
      leds.LEDRingSmall_Set_RED(i, newLEDs[i].r);
      currentLEDs[i].r = newLEDs[i].r;
    }
    if (currentLEDs[i].g != newLEDs[i].g) {
      leds.LEDRingSmall_Set_GREEN(i, newLEDs[i].g);
      currentLEDs[i].g = newLEDs[i].g;
    }
    if (currentLEDs[i].b != newLEDs[i].b) {
      leds.LEDRingSmall_Set_BLUE(i, newLEDs[i].b);
      currentLEDs[i].b = newLEDs[i].b;
    }
  }
}

void OttoPot::intro(unsigned long intromillis, uint8_t offset) {
  uint i = 23;
  uint out = 600;
  uint offadd = 50 * offset;

  if (intromillis > 0 && intromillis < out + offadd) {
    int r = map(intromillis, 0, out + offadd, 0, onColor.r);
    int g = map(intromillis, 0, out + offadd, 0, onColor.g);
    int b = map(intromillis, 0, out + offadd, 0, onColor.b);
    leds.LEDRingSmall_Set_RED(i, r);
    leds.LEDRingSmall_Set_GREEN(i, g);
    leds.LEDRingSmall_Set_BLUE(i, b);
  }
}

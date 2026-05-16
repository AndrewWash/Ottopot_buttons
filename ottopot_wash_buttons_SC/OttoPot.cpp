/*
OTTOPOT
LICENSE: GPL v3 (http://www.gnu.org/licenses/gpl.html)
*/

#include "OttoPot.h"
#include "LEDRingSmall.h"
#include "debug.h"
#include <Arduino.h>
#include <Mux.h>

// ---- Deadzone tuning (Method 2: signed unlock gate) ---------------------
// dzValue detects the initial unlock burst (stray-CC / automation-takeover
// guard). It is a NET SIGNED displacement accumulator: opposite-sign noise
// cancels toward 0, so only a genuine one-directional turn crosses the gate.
// This decouples responsiveness from noise rejection — the threshold no
// longer has to be a compromise. (The earlier dzValue summed abs(delta), so
// noise of either sign only ever pushed it UP; a low threshold then admitted
// noise and ghosted CCs — commit 9efa6b0.) Staying unlocked is time-based: a
// CONTINUOUS net-signed displacement accumulator (netAccum) refreshes
// lastMovementMillis the moment it reaches DZ_NET_MOVE_COUNTS, and the pot
// relocks only after DZ_PAUSE_TIMEOUT_MS of stillness.
#define DZ_UNLOCK_THRESHOLD  1.0f  // net signed travel (counts) to unlock.
                                   // Safe at 1.0 now that noise self-cancels;
                                   // raise toward 1.5 only if ghosting recurs
                                   // in the field.                       [TUNE]
#define DZ_PAUSE_TIMEOUT_MS  300   // relock after this much stillness   [TUNE]
#define DZ_NET_MOVE_COUNTS   1     // net counts that count as "genuine" [TUNE]
// Phase 2: unlock also requires this many consecutive same-sign deltas. Idle
// ADC dither reverses sign every 1-2 samples so it never builds the run; a
// genuine turn clears it in ~3 loops (~3 ms) — imperceptible.            [TUNE]
#define DZ_MIN_RUN           3

// Which knob (muxc 0-7) the DEBUG_DZ_TUNE serial plotter prints. The per-CC
// TX log below is unconditional across all 8 pots; this only steers the plot.
#define DZ_TUNE_KNOB         0

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

  locked = true;                 // boot silent — must not transmit
  lastMovementMillis = millis();
  netAccum = 0;
  dzRun = 0;
  dzRunDir = 0;

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

  dzValue += delta;                             // net signed travel — noise cancels
  dzValue = constrain(dzValue, -40.0f, 40.0f);

  // Directional-persistence run counter (Phase 2): count consecutive same-sign
  // deltas. A genuine turn produces a long run; idle dither reverses every
  // 1-2 samples. Zero deltas don't reset it — slow turns have many zero loops
  // between integer crossings. A stale run is expired in the decay block once
  // dzValue has fully decayed back to 0.
  if (delta != 0) {
    int dir = (delta > 0) ? 1 : -1;
    if (dir == dzRunDir) {
      dzRun++;
    } else {
      dzRunDir = dir;
      dzRun = 1;
    }
  }

#ifdef DEBUG_DZ_LOGS
  dzMax = max(dzMax, (int)fabs(dzValue));
  if (currentMillis - dzIntervalMillis >= 5000) {
    debugln("dzMax: %d", dzMax);
    dzMax = 0;
    dzIntervalMillis = currentMillis;
  }
#endif

  // --- Deadzone state machine (Finding #5) -------------------------------
  // dzValue detects only the initial unlock burst. Staying unlocked is driven
  // by a CONTINUOUS net-signed displacement accumulator (no fixed window): the
  // earlier 40 ms window reset discarded slow movement before it could
  // accumulate, reintroducing a speed floor (diag-pot Obs 3).
  if (locked) {
    // Initial unlock requires a deliberate rate burst (dzValue) AND a
    // sustained one-directional run of DZ_MIN_RUN samples (dzRun). Idle ADC
    // dither reverses sign every 1-2 samples so it never builds the run —
    // this is the stray-CC / automation-takeover guard. (Phase 2.)
    if (fabs(dzValue) > DZ_UNLOCK_THRESHOLD && dzRun >= DZ_MIN_RUN) {
      locked = false;
      lastMovementMillis = currentMillis;
      netAccum = 0;
      dzValue = 0.0f;
      dzRun = 0;
      dzRunDir = 0;
    } else {
      // Still locked: buffer run-up travel so the gesture doesn't lag the
      // knob on unlock. Done only when we stay locked, so the transmit block
      // below (value + delta + pendingDelta) never double-counts this delta.
      pendingDelta += delta;
    }
  } else {
    // Accumulate net signed travel continuously — never reset on a timer. The
    // moment it reaches DZ_NET_MOVE_COUNTS that is confirmed genuine movement:
    // refresh the pause timer and reset the accumulator. A turn of ANY speed
    // eventually reaches the threshold, so it never relocks mid-gesture; idle
    // delta is 0, so the accumulator never grows while the knob is still.
    netAccum += delta;
    if (abs(netAccum) >= DZ_NET_MOVE_COUNTS) {
      lastMovementMillis = currentMillis;
      netAccum = 0;
    }
    // Relock only after a true pause — DZ_PAUSE_TIMEOUT_MS of no movement.
    if (currentMillis - lastMovementMillis > DZ_PAUSE_TIMEOUT_MS) {
      locked = true;
      // Discard buffered run-up so a stray drift can't build a latent jump;
      // clear dzValue so re-unlocking needs a fresh deliberate burst.
      pendingDelta = 0;
      netAccum = 0;
      dzValue = 0.0f;
      dzRun = 0;
      dzRunDir = 0;
    }
  }

  // Transmit whenever unlocked — including the loop that just unlocked, hence
  // a plain `if` (not `else if`) so pendingDelta flushes the same pass.
  if (!locked) {
    newValue = value + delta + pendingDelta;
    pendingDelta = 0;
    if (newValue < 0) {
      newValue = 0;
    } else if (newValue > MAX_POT_VALUE) {
      newValue = MAX_POT_VALUE;
    }
    if (newValue != value) {
      // Refresh on every transmit so the 100 ms incoming-CC gate stays closed
      // for the whole gesture, however slow.
      interactionMillis = currentMillis;
      setNewValue(newValue);
      sendMidiCC(map(value, 0, MAX_POT_VALUE, 0, 16383));
#ifdef DEBUG
      // Per-CC transmit log — fires for ALL 8 pots (the DZ plotter below
      // covers only one). During an idle soak this prints only on a ghost CC,
      // naming the culprit pot and its accumulator state at the moment it
      // transmitted. See code_analysis/ghost-cc-tightening-plan.md.
      debugln("TX cc:%d k:%d val:%d delta:%d dz:%d pend:%d", cc, muxc, value,
              delta, (int)(dzValue * 100.0f), pendingDelta);
#endif
    }
  }

  // Time-based decay of the unlock-burst accumulator, decoupled from loop
  // rate: ~1.0 per 5 ms regardless of how fast the loop runs (the old integer
  // deltaMicros/5000 truncated to 1, sometimes 0, and depended on delay(5)).
  float dzDecay = (float)deltaMicros / 5000.0f;
  if (dzValue > 0.0f)
    dzValue = max(dzValue - dzDecay, 0.0f);
  else if (dzValue < 0.0f)
    dzValue = min(dzValue + dzDecay, 0.0f);
  // Expire a stale run: a run only means something while dzValue still holds
  // recent net travel. Once it has decayed exactly to 0, isolated blips
  // seconds apart must not slowly accumulate a false unlock. (max/min above
  // clamp exactly to 0.0f, so this compare is safe.)
  if (dzValue == 0.0f) {
    dzRun = 0;
    dzRunDir = 0;
  }
  previousMillis = currentMillis;
  previousMicros = currentMicros;

// Debugging graph for tuning the dead zone (Serial plotter).
//  unlocked  - the true transmit gate (high = transmitting)
//  dzValue   - net-signed unlock accumulator (x100); cancels toward 0 on noise
//  delta     - per-loop movement (x100); 0 when still, nonzero while turning
//  dzRun     - consecutive same-sign deltas; unlock needs >= DZ_MIN_RUN
//  sinceMove - ms since genuine movement; relock fires at DZ_PAUSE_TIMEOUT_MS
#ifdef DEBUG_DZ_TUNE
  if (muxc == DZ_TUNE_KNOB) {
    debug("%dpin1:%d", muxc, pin1[MEDIAN]);
    debug(",%dpin2:%d", muxc, pin2[MEDIAN]);
    // sinceMove is clamped to 600 ms: while idle it would climb unbounded and
    // wreck the plotter's autoscale. Anything past DZ_PAUSE_TIMEOUT_MS (300)
    // is meaningless anyway — the pot has already relocked.
    debugln(",unlocked:%d,dzValue:%d,delta:%d,dzRun:%d,sinceMove:%d",
            (!locked) * 4000, (int)(dzValue * 100.0f),
            delta * 100, dzRun,
            (int)min(currentMillis - lastMovementMillis, (unsigned long)600));
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

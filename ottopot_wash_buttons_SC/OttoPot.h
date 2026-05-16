/*
OTTOPOT
LICENSE: GPL v3 (http://www.gnu.org/licenses/gpl.html)
*/

#ifndef OttoPot_h
#define OttoPot_h

#include "Arduino.h"
#include "EndlessPotentiometer.h"
#include "LEDRingSmall.h"
#include "main.h"
#include <Mux.h>

#define NUM_LEDS 24

struct rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  void setAll (uint8_t set) {
  	r = g = b = set;
  }
};

class OttoPot {
public:
  int channel;
  int cc;
  int receivedHSB;
  int receivedLSB;
  int value;
  float dzValue;
  int pendingDelta;

  // Deadzone state machine (Finding #5). dzValue still drives the initial
  // unlock burst; staying unlocked is now time-based — see updateValue().
  bool locked;                      // true = silent (not transmitting)
  unsigned long lastMovementMillis; // last time genuine (net) movement seen
  unsigned long netWindowMillis;    // start of current net-displacement window
  int netWindowSum;                 // signed sum of delta over current window

  rgb offColor;
  rgb onColor;
  rgb currentLEDs[24];
  rgb newLEDs[24];

  unsigned long interactionMillis;
  unsigned long previousMillis;
  unsigned long previousMicros;
  admux::Mux *mux, *mux2;
  int muxc;
  
  #ifdef DEBUG_DZ_LOGS
  int dzMax;
  unsigned long dzIntervalMillis;
  #endif
  
  EndlessPotentiometer pot;

  LEDRingSmall leds;

  OttoPot(admux::Mux *rmux, admux::Mux *rmux2, int rmuxc, int cc, int channel,
          uint8_t ledRingAddress);
  void sendMidiCC(int rawVal);
  void updateValue(unsigned long currentMillis, unsigned long currentMicros);
  void setNewValue(int newValue);

  void setOffColor(uint8_t r, uint8_t g, uint8_t b);
  void updateLEDs();
  void intro(unsigned long intromillis, uint8_t offset);
  void initialize();
  void reseedBaseline();
  void handleControlChange(byte rchannel, byte rcontrol, byte rvalue);
};

#endif

/*
OTTOPOT
LICENSE: GPL v3 (http://www.gnu.org/licenses/gpl.html)

Sources:
https://www.duppa.net/shop/rgb-led-ring-small/
https://www.printables.com/de/model/347536-encoder-knob
https://github.com/juanlittledevil/EndlessPotentiometer
*/

#include "EndlessPotentiometer.h"
#include "LEDRingSmall.h"
#include "OttoPot.h"
#include "debug.h"
#include <ADC.h>
#include <Mux.h>
#include <Wire.h>

// This is the fps for the LED updates.
// The i2c calls are pretty expensive so we're only updating when necessary
#define LED_UPDATES_PER_SECOND 60
#define LED_UPDATE_MILLIS 1000 / LED_UPDATES_PER_SECOND

unsigned long previousMillis = 0;
int receivedHSB = 0;
int receivedLSB = 0;
bool showintro = true;
unsigned long introMillis = 0;

#define NUMPOTS 8

// Both mux's use the same pins because they'll always switch at the same time,
// only the read pin is different
using namespace admux;
Mux mux(Pin(A0, INPUT, PinType::Analog), Pinset(20, 21, 22));
Mux mux2(Pin(A1, INPUT, PinType::Analog), Pinset(20, 21, 22));

// Initialize pots
// mux, mux2, mux_channel, CC number (LSB, MSB will be calculated), midi
// channel, i2c address
OttoPot ottopot[] = {
    OttoPot(&mux, &mux2, 0, 9, MIDI_CHANNEL, ISSI3746_SJ1 | ISSI3746_SJ6),
    OttoPot(&mux, &mux2, 1, 10, MIDI_CHANNEL, ISSI3746_SJ3 | ISSI3746_SJ8),
    OttoPot(&mux, &mux2, 2, 11, MIDI_CHANNEL, ISSI3746_SJ1 | ISSI3746_SJ8),
    OttoPot(&mux, &mux2, 3, 12, MIDI_CHANNEL, ISSI3746_SJ2 | ISSI3746_SJ5),

    OttoPot(&mux, &mux2, 4, 13, MIDI_CHANNEL, ISSI3746_SJ2 | ISSI3746_SJ6),
    OttoPot(&mux, &mux2, 5, 14, MIDI_CHANNEL, ISSI3746_SJ2 | ISSI3746_SJ7),
    OttoPot(&mux, &mux2, 6, 15, MIDI_CHANNEL, ISSI3746_SJ2 | ISSI3746_SJ8),
    OttoPot(&mux, &mux2, 7, 16, MIDI_CHANNEL, ISSI3746_SJ3 | ISSI3746_SJ6)

};

void receiveMIDICC(byte rchannel, byte rcontrol, byte rvalue) {
  for (int i = 0; i < NUMPOTS; i++) {
    ottopot[i].handleControlChange(rchannel, rcontrol, rvalue);
  }
}

void setup() {
  analogReadResolution(12);
  delay(200);
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  Wire.begin();
  Wire.setClock(400000);

  for (int i = 0; i < NUMPOTS; i++) {
    ottopot[i].initialize();
  }

  usbMIDI.setHandleControlChange(receiveMIDICC);
  introMillis = millis();
}

void loop() {
  unsigned long currentMillis = millis();
  if (showintro) {
    // Naive intro animation
    if (currentMillis - previousMillis >= LED_UPDATE_MILLIS) {

      for (int i = 0; i < NUMPOTS; i++) {
        ottopot[i].intro(currentMillis - introMillis, i);
      }

      previousMillis = currentMillis;
    }
    if (currentMillis - introMillis > 1600) {
      showintro = false;
      for (int i = 0; i < NUMPOTS; i++) {
        ottopot[i].intro(currentMillis - introMillis, i);
      }
    }
  } else {
    // Regular operation
    for (int i = 0; i < NUMPOTS; i++) {
      ottopot[i].updateValue(currentMillis, micros());
    }

    if (currentMillis - previousMillis >= LED_UPDATE_MILLIS) {
      for (int i = 0; i < NUMPOTS; i++) {
        ottopot[i].updateLEDs();
      }
      previousMillis = currentMillis;
    }
  }

  usbMIDI.send_now();
  while (usbMIDI.read()) {
  }
	delay(5);
}

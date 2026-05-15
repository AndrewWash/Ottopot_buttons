/*
  Ottopot Wash + Buttons
  Self-contained Arduino sketch for Teensy 4.0.
  Adds two momentary switches on pins 2 (Prev Bank) and 3 (Next Bank)
  to the existing 8-knob LED-ring controller. The switches transmit
  CC 110 / CC 111 (value 127) on the Ottopot MIDI channel (6); the
  Bitwig controller script "ottopot_wash_buttons.control.js" binds
  them to RemoteControlsPage select prev/next.

  LICENSE: GPL v3 (http://www.gnu.org/licenses/gpl.html)
  Based on the Ottopot firmware by the upstream Ottopot authors.
*/

#include "EndlessPotentiometer.h"
#include "LEDRingSmall.h"
#include "OttoPot.h"
#include "debug.h"
#include "main.h"          // MAX_POT_VALUE, COLOR_*, MIDI_CHANNEL
#include <ADC.h>
#include <Bounce2.h>
#include <Mux.h>
#include <Wire.h>

// ---------- LED frame rate ------------------------------------------------
#define LED_UPDATES_PER_SECOND 60
#define LED_UPDATE_MILLIS      (1000 / LED_UPDATES_PER_SECOND)

// ---------- Switches ------------------------------------------------------
#define PIN_SW_PREV    2
#define PIN_SW_NEXT    3
#define CC_PREV_BANK   110     // outside 9-16, 41-48, 101-106 (existing CC map)
#define CC_NEXT_BANK   111
#define SW_DEBOUNCE_MS 5

Bounce swPrev = Bounce();
Bounce swNext = Bounce();

// ---------- Pots / Muxes --------------------------------------------------
#define NUMPOTS 8

using namespace admux;
Mux mux (Pin(A0, INPUT, PinType::Analog), Pinset(20, 21, 22));
Mux mux2(Pin(A1, INPUT, PinType::Analog), Pinset(20, 21, 22));

OttoPot ottopot[] = {
    OttoPot(&mux, &mux2, 0,  9, MIDI_CHANNEL, ISSI3746_SJ1 | ISSI3746_SJ6),
    OttoPot(&mux, &mux2, 1, 10, MIDI_CHANNEL, ISSI3746_SJ3 | ISSI3746_SJ8),
    OttoPot(&mux, &mux2, 2, 11, MIDI_CHANNEL, ISSI3746_SJ1 | ISSI3746_SJ8),
    OttoPot(&mux, &mux2, 3, 12, MIDI_CHANNEL, ISSI3746_SJ2 | ISSI3746_SJ5),
    OttoPot(&mux, &mux2, 4, 13, MIDI_CHANNEL, ISSI3746_SJ2 | ISSI3746_SJ6),
    OttoPot(&mux, &mux2, 5, 14, MIDI_CHANNEL, ISSI3746_SJ2 | ISSI3746_SJ7),
    OttoPot(&mux, &mux2, 6, 15, MIDI_CHANNEL, ISSI3746_SJ2 | ISSI3746_SJ8),
    OttoPot(&mux, &mux2, 7, 16, MIDI_CHANNEL, ISSI3746_SJ3 | ISSI3746_SJ6),
};

// ---------- State ---------------------------------------------------------
unsigned long previousMillis = 0;
bool          showintro      = true;
unsigned long introMillis    = 0;

// ---------- MIDI ingest ---------------------------------------------------
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

    pinMode(PIN_SW_PREV, INPUT_PULLUP);
    pinMode(PIN_SW_NEXT, INPUT_PULLUP);
    swPrev.attach(PIN_SW_PREV);
    swNext.attach(PIN_SW_NEXT);
    swPrev.interval(SW_DEBOUNCE_MS);
    swNext.interval(SW_DEBOUNCE_MS);

    usbMIDI.setHandleControlChange(receiveMIDICC);
    introMillis = millis();
}

void loop() {
    unsigned long currentMillis = millis();

    if (showintro) {
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
        for (int i = 0; i < NUMPOTS; i++) {
            ottopot[i].updateValue(currentMillis, micros());
        }

        if (currentMillis - previousMillis >= LED_UPDATE_MILLIS) {
            for (int i = 0; i < NUMPOTS; i++) {
                ottopot[i].updateLEDs();
            }
            previousMillis = currentMillis;
        }

        swPrev.update();
        swNext.update();
        if (swPrev.fell()) usbMIDI.sendControlChange(CC_PREV_BANK, 127, MIDI_CHANNEL);
        if (swNext.fell()) usbMIDI.sendControlChange(CC_NEXT_BANK, 127, MIDI_CHANNEL);
    }

    usbMIDI.send_now();
    while (usbMIDI.read()) { /* drain */ }
    delay(5);
}

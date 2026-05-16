// This is the max analog resolution we configure in main.cpp setup
#define MAX_POT_VALUE 4095

// This is the (maximum) color we want the LEDs to use
#define COLOR_R 120
#define COLOR_G 100
#define COLOR_B 255

#define MIDI_CHANNEL 6

// Debug flags — all OFF for the production / distribution build.
// For a deadzone tuning session: enable DEBUG + DEBUG_DZ_TUNE and flash with
// USB Type = "Serial + MIDI". Production builds use USB Type = "MIDI".
// #define DEBUG
// #define DEBUG_DZ_TUNE
// #define DEBUG_DZ_LOGS

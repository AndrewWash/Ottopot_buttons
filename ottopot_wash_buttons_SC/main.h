// This is the max analog resolution we configure in main.cpp setup
#define MAX_POT_VALUE 4095

// This is the (maximum) color we want the LEDs to use
#define COLOR_R 120
#define COLOR_G 100
#define COLOR_B 255

#define MIDI_CHANNEL 6

// --- DIAGNOSTIC BUILD (branch diag-pot) -----------------------------------
// DEBUG + DEBUG_DZ_TUNE enabled to stream the deadzone tuning plot over USB
// Serial. Requires USB Type = "Serial + MIDI" when flashing. Re-comment both
// (and reflash as USB Type "MIDI") for the production build.
#define DEBUG
#define DEBUG_DZ_TUNE
// #define DEBUG_DZ_LOGS   // leave OFF: its "dzMax:" lines corrupt the plot

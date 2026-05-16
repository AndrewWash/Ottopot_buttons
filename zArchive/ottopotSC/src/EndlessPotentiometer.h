#ifndef EndlessPotentiometer_h
#define EndlessPotentiometer_h

#include "main.h"

class EndlessPotentiometer {
public:
  int value;        // Internal variable to hold a pot value.
  float prevAngle = 0.0;
  int valueChanged; // This is the amount of change between last collection and
  // this current one.
  int linearDelta;
  float flinearDelta = 0.0;
  EndlessPotentiometer();
  void updateValues(int valueA, int valueB);

private:
  int valueA;
  int valueB;
  int previousValueA = 0;
  int previousValueB = 0;
  const int adcMaxValue =
      MAX_POT_VALUE; // the max value from analogRead for each pin.
};

#endif

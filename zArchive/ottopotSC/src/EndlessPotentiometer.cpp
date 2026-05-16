#include "EndlessPotentiometer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const int HALF_POT_VALUE = (MAX_POT_VALUE >> 1);

EndlessPotentiometer::EndlessPotentiometer() {
  valueA = 0;
  valueB = 0;
  valueChanged = 0;
  value = 0;
}

void EndlessPotentiometer::updateValues(int valueA, int valueB) {
  previousValueA = this->valueA;
  previousValueB = this->valueB;

  this->valueA = valueA;
  this->valueB = valueB;

  // This will give us an angle from -pi to pi, but it is supposed to work with sine waves
  // while we are working with “flat” triangle waves, so the output is a bit wobbly over the whole range 
  // We're using the angle for the direction and we're creating a linear delta below to use for the values
  float angle = atan2((float)(valueA - HALF_POT_VALUE) / (float)HALF_POT_VALUE,
                      (float)(valueB - HALF_POT_VALUE) / (float)HALF_POT_VALUE);
  int newValue = (angle + M_PI) * (float)MAX_POT_VALUE / (2.0 * M_PI);

  int delta = newValue - this->value;
  int adelta = abs(delta);

  // With a delta over half the range, we can be sure that we're actually wrapping.
  // -> such change, very wow. Wrap around!
  if (adelta > HALF_POT_VALUE) {
    if (delta > 0) {
      delta = (this->value + MAX_POT_VALUE - newValue) * -1;
      adelta = abs(delta);
    } else {
      delta = (this->value - newValue - MAX_POT_VALUE) * -1;
      adelta = abs(delta);
    }
  }

  // Now we're creating a linear delta based on the direct read values, not the atan2 result.
  // Since a wiper goes up and down once per rotation, the numeric deltas are twice the analog
  // read resultion during a full turn, so we're dividing by 2.
  // We're keeping values under a full integer and update to the next int once it hits above .0
  float linear_delta =
      abs(fmin(valueA - previousValueA, valueB - previousValueB)) / 2.0;
  if (angle < this->prevAngle)
    linear_delta = linear_delta * -1.0;
  this->flinearDelta += linear_delta;

  if (this->flinearDelta < 0) {
    linearDelta = ceil(this->flinearDelta);
  } else {
    linearDelta = floor(this->flinearDelta);
  }
  if (linearDelta != 0) {
	  // reset any floating point that was kept from previous invocations after we've applie the value
    this->flinearDelta = 0.0;
  }

  // This is all just for debugging and “compatibility” — the actual value to use is this->linearDelta.
  valueChanged = delta;
  value = newValue;
  this->prevAngle = angle;
}

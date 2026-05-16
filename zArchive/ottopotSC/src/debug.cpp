#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include "main.h"

void debug(const char *format, ...) {
#ifdef DEBUG
	char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, 255, format, args);

  Serial.print(buffer);
  va_end(args);
#endif
}
void debugln(const char *format, ...) {
#ifdef DEBUG
	char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, 255, format, args);

  Serial.println(buffer);
  va_end(args);
#endif
}

// Font display test - RocketClock
// Renders a glyph from fonts.h onto the matrix via the shared library.
#include <RocketMatrix.h>
#include "fonts.h"

RocketMatrix matrix;

const uint8_t NUM_LEDS = 64;

// FONT_LOOKUP[charIndex][i] is stored column-major: i = col*8 + row.
void setDisplayChar(int charIndex) {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t col = i / 8, row = i % 8;
    matrix.setPixel(col, row, FONT_LOOKUP[charIndex][i]);
  }
}

void setup() {
  Serial.begin(9600);
  matrix.begin(7);
}

void loop() {
  setDisplayChar(0);   // Display 'E'
  delay(500);
}

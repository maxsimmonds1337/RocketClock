// Static glyph test - RocketClock
// Draws a centered 'A' to sanity-check glyph orientation via the library.
#include <RocketMatrix.h>

RocketMatrix matrix;

// row 0 = top, col 0 = left.
const uint8_t letterA[8][8] = {
  {0, 0, 0, 1, 1, 0, 0, 0}, // row 0 (top)
  {0, 0, 1, 0, 0, 1, 0, 0}, // row 1
  {0, 1, 0, 0, 0, 0, 1, 0}, // row 2
  {0, 1, 1, 1, 1, 1, 1, 0}, // row 3 (crossbar)
  {0, 1, 0, 0, 0, 0, 1, 0}, // row 4
  {0, 1, 0, 0, 0, 0, 1, 0}, // row 5
  {0, 1, 0, 0, 0, 0, 1, 0}, // row 6
  {0, 0, 0, 0, 0, 0, 0, 0}  // row 7 (bottom)
};

void setup() {
  matrix.begin(3);
  // If the 'A' is mirrored or upside-down, calibrate once here:
  // matrix.setOrientation(/*flipCols*/false, /*flipRows*/false, /*transpose*/false);
  for (uint8_t row = 0; row < 8; row++)
    for (uint8_t col = 0; col < 8; col++)
      matrix.setPixel(col, row, letterA[row][col]);
}

void loop() {
  // static hold
}

// Rotating smiley - RocketClock
// Displays a smiley and rotates it in 90 deg steps (bit-shuffle rotation).
#include <RocketMatrix.h>

RocketMatrix matrix;

// Row-major bitmap: row 0 = top, bit7 = left column, bit0 = right column.
const uint8_t SMILEY[8] = {
  0b00111100,  // ..####..
  0b01000010,  // .#....#.
  0b10100101,  // #.#..#.#  (eyes)
  0b10000001,  // #......#
  0b10100101,  // #.#..#.#
  0b10011001,  // #..##..#  (smile)
  0b01000010,  // .#....#.
  0b00111100,  // ..####..
};

// Draw a row-major frame (bit7 = leftmost column) via logical (col,row).
void drawFrame(const uint8_t frame[8]) {
  for (uint8_t row = 0; row < 8; row++)
    for (uint8_t col = 0; col < 8; col++)
      matrix.setPixel(col, row, (frame[row] >> (7 - col)) & 1);
}

// Rotate 90 deg clockwise: new[row][col] = old[7-col][row]
void rotateCW(const uint8_t src[8], uint8_t dst[8]) {
  memset(dst, 0, 8);
  for (int row = 0; row < 8; row++)
    for (int col = 0; col < 8; col++)
      if ((src[7 - col] >> (7 - row)) & 1)
        dst[row] |= (1 << (7 - col));
}

void setup() {
  Serial.begin(115200);
  Serial.println("RocketClock smiley");
  matrix.begin(0);   // dim
}

void loop() {
  uint8_t frames[4][8];
  memcpy(frames[0], SMILEY, 8);
  rotateCW(frames[0], frames[1]);
  rotateCW(frames[1], frames[2]);
  rotateCW(frames[2], frames[3]);

  for (int i = 0; i < 4; i++) {
    drawFrame(frames[i]);
    delay(400);
  }
}

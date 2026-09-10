// LED scan test - RocketClock
// Walks a single lit LED through all 64 positions, one at a time, so you can
// confirm every LED works and the (col,row) mapping is correct.
#include <RocketMatrix.h>

RocketMatrix matrix;

void setup() {
  Serial.begin(115200);
  Serial.println("RocketClock LED scan test");
  matrix.begin(8);   // half brightness
}

void loop() {
  for (uint8_t i = 0; i < 64; i++) {
    uint8_t col = i / 8, row = i % 8;
    matrix.setPixel(col, row, true);
    Serial.printf("LED %2u  col %u  row %u\n", i, col, row);
    delay(150);
    matrix.setPixel(col, row, false);
  }
}

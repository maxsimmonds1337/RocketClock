// Orientation calibration - RocketClock
// Lights each logical corner in turn so you can confirm the library's
// (col,row) coordinates land where you expect on the physical panel.
// If a corner is wrong, fix it once via matrix.setOrientation(...).
#include <RocketMatrix.h>

RocketMatrix matrix;

void showCorner(const char *label, uint8_t col, uint8_t row) {
  Serial.printf("[TEST] col=%u row=%u  (%s)\n", col, row, label);
  matrix.clear();
  matrix.setPixel(col, row, true);
  delay(2000);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ORIENTATION CALIBRATION ===");
  matrix.begin(1);   // 10% brightness
  // matrix.setOrientation(false, false, false); // flipCols, flipRows, transpose
}

void loop() {
  showCorner("expected TOP-LEFT",     0, 0);
  showCorner("expected TOP-RIGHT",    7, 0);
  showCorner("expected BOTTOM-LEFT",  0, 7);
  showCorner("expected BOTTOM-RIGHT", 7, 7);
  Serial.println("--- End of cycle, repeating in 3 s ---");
  matrix.clear();
  delay(3000);
}

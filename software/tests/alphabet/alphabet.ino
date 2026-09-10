// Alphabet / glyph loop - RocketClock
// Cycles the full RocketFont glyph set (upper, lower, digits, punctuation,
// degree, pound) one at a time so each can be visually confirmed. Prints
// "[index] label" on Serial (115200).
#include <RocketFont.h>

RocketMatrix matrix;

const unsigned long HOLD_MS = 800;
int idx = -1;
unsigned long last = 0;

void next() {
  idx = (idx + 1) % FONT_COUNT;
  matrix.clear();
  RocketFont::drawGlyphAt(matrix, idx, 0);
  Serial.printf("[%2d] %s\n", idx, FONT_LABELS[idx]);
}

void setup() {
  Serial.begin(115200);
  matrix.begin(5);
  next();
  last = millis();
}

void loop() {
  if (millis() - last >= HOLD_MS) {
    last = millis();
    next();
  }
}

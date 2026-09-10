// 20-minute timer - RocketClock
// Fills the 8x8 (64 LEDs) cumulatively over 20 minutes, one LED every 18.75 s.
// When the 20 minutes elapse: hold all 64 lit and loop a sci-fi tune on the
// buzzer until the board is reset.
#include <RocketMatrix.h>
#include <RocketBuzzer.h>
#include <RocketMelodies.h>

RocketMatrix matrix;
RocketBuzzer buzzer;

const unsigned long TIME_PER_LED  = 20UL * 60UL * 1000UL / 64;  // 18,750 ms
const unsigned long TOTAL_TIME_MS = TIME_PER_LED * 64;          // 20 minutes

unsigned long startTime;
int  lastLED  = -1;
bool finished = false;

void setup() {
  Serial.begin(115200);
  matrix.begin(5);
  buzzer.begin();
  startTime = millis();
}

void loop() {
  if (finished) {                 // all 64 lit; serenade until reset
    buzzer.playMelody(RocketMelodies::HEDWIG, RocketBuzzer::VOL_MAX);
    delay(1500);
    return;
  }

  unsigned long elapsed = millis() - startTime;
  if (elapsed >= TOTAL_TIME_MS) {
    matrix.fill(true);            // ensure every LED is on
    finished = true;
    return;
  }

  int ledIndex = elapsed / TIME_PER_LED;   // 0..63
  if (ledIndex == lastLED) return;

  for (int i = lastLED + 1; i <= ledIndex; i++)
    matrix.setPixel(i / 8, i % 8);         // cumulative fill (col=DIG, row=SEG)
  lastLED = ledIndex;
}

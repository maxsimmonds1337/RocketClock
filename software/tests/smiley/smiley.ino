// MAX7219 rotating smiley — RocketClock
//   DIN  -> GPIO14
//   CLK  -> GPIO12
//   CS   -> GPIO13

#define PIN_DIN  14
#define PIN_CLK  12
#define PIN_CS   13

#define REG_DECODE_MODE  0x09
#define REG_INTENSITY    0x0A
#define REG_SCAN_LIMIT   0x0B
#define REG_SHUTDOWN     0x0C

// Smiley face — bit7 = left column, bit0 = right column
const uint8_t SMILEY[8] = {
  0b00111100,  // ..####..
  0b01000010,  // .#....#.
  0b10100101,  // #.#..#.#  (eyes)
  0b10000001,  // #......#
  0b10100101,  // #.#..#.#  (nose dots)
  0b10011001,  // #..##..#  (smile)
  0b01000010,  // .#....#.
  0b00111100,  // ..####..
};

void max7219Send(uint8_t address, uint8_t data) {
  uint16_t word = ((uint16_t)address << 8) | data;
  digitalWrite(PIN_CS, LOW);
  for (int i = 15; i >= 0; i--) {
    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_DIN, (word >> i) & 1);
    digitalWrite(PIN_CLK, HIGH);
  }
  digitalWrite(PIN_CS, HIGH);
}

void displayFrame(const uint8_t frame[8]) {
  for (uint8_t i = 0; i < 8; i++) {
    max7219Send(i + 1, frame[i]);
  }
}

// Rotate 90° clockwise: new[row][col] = old[7-col][row]
void rotateCW(const uint8_t src[8], uint8_t dst[8]) {
  memset(dst, 0, 8);
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      if ((src[7 - col] >> (7 - row)) & 1) {
        dst[row] |= (1 << (7 - col));
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("RocketClock smiley");

  pinMode(PIN_DIN, OUTPUT);
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_CS,  OUTPUT);
  digitalWrite(PIN_CS,  HIGH);
  digitalWrite(PIN_CLK, LOW);

  max7219Send(REG_SHUTDOWN,     0x01);
  max7219Send(REG_DECODE_MODE,  0x00);
  max7219Send(REG_SCAN_LIMIT,   0x07);
  max7219Send(REG_INTENSITY,    0x00);
}

void loop() {
  // Build 4 rotation frames
  uint8_t frames[4][8];
  memcpy(frames[0], SMILEY, 8);
  rotateCW(frames[0], frames[1]);
  rotateCW(frames[1], frames[2]);
  rotateCW(frames[2], frames[3]);

  for (int i = 0; i < 4; i++) {
    displayFrame(frames[i]);
    delay(400);
  }
}

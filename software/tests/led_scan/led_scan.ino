// MAX7219 LED scan test — RocketClock
// Lights each of the 64 LEDs sequentially: on 1s, off 1s, next LED.
//
// Bit-banged SPI (GPIO not on hardware SPI pins):
//   DIN  -> GPIO14
//   CLK  -> GPIO12
//   CS   -> GPIO13

#define PIN_DIN  14
#define PIN_CLK  12
#define PIN_CS   13

// MAX7219 register addresses
#define REG_DECODE_MODE  0x09
#define REG_INTENSITY    0x0A
#define REG_SCAN_LIMIT   0x0B
#define REG_SHUTDOWN     0x0C

#define ROW1 0x00
#define ROW2 0x01
#define ROW3 0x02
#define ROW4 0x03
#define ROW5 0x04
#define ROW6 0x05
#define ROW7 0x06
#define ROW8 0x07

#define COL1 0x00
#define COL2 0x01
#define COL3 0x02
#define COL4 0x03
#define COL5 0x04
#define COL6 0x05
#define COL7 0x06
#define COL8 0x07


// Send one 16-bit word to MAX7219: [address byte][data byte]
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

void clearAll() {
  for (uint8_t digit = 1; digit <= 8; digit++) {
    max7219Send(digit, 0x00);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("RocketClock LED scan test");

  pinMode(PIN_DIN, OUTPUT);
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_CS,  OUTPUT);
  digitalWrite(PIN_CS,  HIGH);  // CS idle high
  digitalWrite(PIN_CLK, LOW);   // CLK idle low (SPI mode 0)

  // MAX7219 init
  max7219Send(REG_SHUTDOWN,     0x01);  // exit shutdown / normal operation
  max7219Send(REG_DECODE_MODE,  0x00);  // no-decode mode (raw segment control)
  max7219Send(REG_SCAN_LIMIT,   0x07);  // scan all 8 digits
  max7219Send(REG_INTENSITY,    0x08);  // half brightness

  clearAll();
}

void loop() {
  // 8 digits (rows) x 8 segments (columns) = 64 LEDs
  // Digit registers: 0x01 (DIG0) ... 0x08 (DIG7)
  // Segment bits:    D0=SEG_G ... D7=SEG_DP (bit-per-column in no-decode mode)
  //
  max7219(b0000000000000000);
  // for (uint8_t digit = 1; digit <= 8; digit++) {
  //   for (uint8_t seg = 0; seg < 8; seg++) {
  //     uint8_t data = (1 << seg);
  //
  //     max7219Send(digit, data);
  //     Serial.printf("digit %d  seg bit %d\n", digit, seg);
  //     delay(10);
  //
  //     max7219Send(digit, 0x00);
  //     delay(10);
  //   }
  // }
}

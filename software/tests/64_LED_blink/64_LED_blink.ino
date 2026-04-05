#include <SPI.h>

void setup() {

  Serial.begin(115200);
  Serial.println("RocketClock V1.0.0!");
}

void loop() {

  Serial.println("...");
  delay(1000);
}

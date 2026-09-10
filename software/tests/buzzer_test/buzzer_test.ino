// Buzzer test - RocketClock
// Plays a sci-fi melody on boot and loops it, so the Qwiic buzzer (ATtiny84
// over I2C, addr 0x34) can be verified in seconds. Prints whether the buzzer
// ACKed on the bus - if it doesn't, the ATtiny84 likely needs its Qwiic
// firmware flashed (see software/attiny84_buzzer).
#include <RocketBuzzer.h>
#include <RocketMelodies.h>

RocketBuzzer buzzer;

void setup() {
  Serial.begin(115200);
  delay(200);
  buzzer.begin();
  Serial.println(buzzer.present() ? "Buzzer found at 0x34" :
                                    "Buzzer NOT found - flash ATtiny84 firmware?");
}

void loop() {
  buzzer.playMelody(RocketMelodies::HEDWIG, RocketBuzzer::VOL_MAX);
  delay(1500);
}

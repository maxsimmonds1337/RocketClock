#pragma once
#include <Arduino.h>
#include <Wire.h>

// RocketBuzzer - drives the on-board SparkFun Qwiic Buzzer (ATtiny84, U5) over
// I2C. On the ESP-12F the bus is SDA=GPIO4, SCL=GPIO5 (Wire defaults).
//
// Register map (SparkFun Qwiic Buzzer firmware, default addr 0x34):
//   0x03 freq MSB   0x04 freq LSB   0x05 volume(0-4)
//   0x06 dur  MSB   0x07 dur  LSB   0x08 active(1=play)
// Writing 0x03..0x08 in one transaction (auto-incrementing pointer) starts a
// tone. duration 0 = play until stop() is called. An absent/unflashed buzzer
// just NACKs - no hang.
//
// Note frequencies (Hz) for melodies are provided below.
namespace RocketNote {
  enum : uint16_t {
    REST = 0,
    C3=131, D3=147, E3=165, F3=175, G3=196, A3=220, B3=247,
    C4=262, Cs4=277, D4=294, Ds4=311, E4=330, F4=349, Fs4=370,
    G4=392, Gs4=415, A4=440, As4=466, B4=494,
    C5=523, Cs5=554, D5=587, Ds5=622, E5=659, F5=698, Fs5=740,
    G5=784, Gs5=831, A5=880, As5=932, B5=988,
    C6=1047, Cs6=1109, D6=1175, Ds6=1245, E6=1319,
  };
}

// A named melody: parallel freq/duration arrays. Used for song selection.
struct Melody {
  const uint16_t *freqs;
  const uint16_t *durs;
  int n;
  const char *name;
};

class RocketBuzzer {
public:
  static const uint8_t VOL_OFF = 0, VOL_MIN = 1, VOL_LOW = 2, VOL_MID = 3, VOL_MAX = 4;

  RocketBuzzer(uint8_t addr = 0x34) : _addr(addr) {}

  void begin() { Wire.begin(); }   // ESP8266 default SDA=4 SCL=5

  // Start a tone. durMs=0 plays until stop(). Returns true if the buzzer ACKed.
  bool tone(uint16_t freq, uint16_t durMs, uint8_t vol = VOL_MAX) {
    Wire.beginTransmission(_addr);
    Wire.write(0x03);
    Wire.write(freq >> 8); Wire.write(freq & 0xFF);
    Wire.write(vol);
    Wire.write(durMs >> 8); Wire.write(durMs & 0xFF);
    Wire.write(0x01);                 // active
    return Wire.endTransmission() == 0;
  }

  void stop() {
    Wire.beginTransmission(_addr);
    Wire.write(0x08); Wire.write(0x00);   // active = 0
    Wire.endTransmission();
  }

  bool present() {                    // is the buzzer on the bus?
    Wire.beginTransmission(_addr);
    return Wire.endTransmission() == 0;
  }

  // Play a melody: freqs[] in Hz (0=rest), durs[] in ms. Blocks until done.
  void playMelody(const uint16_t *freqs, const uint16_t *durs, int n,
                  uint8_t vol = VOL_MAX, uint16_t gapMs = 40) {
    for (int i = 0; i < n; i++) {
      if (freqs[i] == RocketNote::REST) stop();
      else tone(freqs[i], durs[i], vol);
      delay(durs[i] + gapMs);
    }
    stop();
  }

  void playMelody(const Melody &m, uint8_t vol = VOL_MAX, uint16_t gapMs = 40) {
    playMelody(m.freqs, m.durs, m.n, vol, gapMs);
  }

private:
  uint8_t _addr;
};

# RocketClock Board Bring-Up Test Procedure

**Board Revision:** v1.0.0
**Date:** 2026-03-23

## Required Equipment

- USB-C cable (test both orientations due to issue #2)
- Multimeter (voltage and continuity)
- USB-to-Serial adapter (backup, if CH340G fails)
- Logic analyzer or oscilloscope (optional but helpful)
- Computer with:
  - Arduino IDE or PlatformIO
  - ESP8266 board support
  - Serial terminal (e.g., minicom, PuTTY, screen)

## Pre-Power Checklist

### 1. Visual Inspection

| Check | Status |
|-------|--------|
| No solder bridges on fine-pitch ICs (ESP-12F, CH340G, MAX7219) | [ ] |
| All components present and correctly oriented | [ ] |
| USB-C connector properly seated | [ ] |
| No damaged/missing passives | [ ] |
| No PCB defects (scratches exposing copper, delamination) | [ ] |

### 2. Continuity Checks (POWER OFF)

| Test | Expected | Actual | Status |
|------|----------|--------|--------|
| +5V to GND | Open (no short) | | [ ] |
| +3.3V to GND | Open (no short) | | [ ] |
| USB VBUS to +5V net | Continuous | | [ ] |
| GND pins connected (spot check 3 locations) | Continuous | | [ ] |

---

## Stage 1: Power Supply Verification

### 1.1 Initial Power-On

**Procedure:**
1. Connect USB-C cable to a current-limited supply if available (set limit to 500mA)
2. If no current-limited supply, use a standard USB port but monitor for excessive heat
3. Observe current draw on power-up

| Measurement | Expected | Actual | Status |
|-------------|----------|--------|--------|
| Initial current draw | 50-150mA | | [ ] |
| No components getting hot | Cool to touch | | [ ] |
| No smoke/burning smell | None | | [ ] |

**STOP if current exceeds 300mA with ESP8266 not transmitting - indicates short**

### 1.2 Voltage Rail Verification

Measure with multimeter, referenced to GND:

| Rail | Test Point | Expected | Tolerance | Actual | Status |
|------|------------|----------|-----------|--------|--------|
| +5V (VBUS) | USB connector pin | 5.0V | ±0.25V | | [ ] |
| +5V (after protection) | C2 positive | 5.0V | ±0.25V | | [ ] |
| +3.3V | U1 pin 5 (OUT) | 3.3V | ±0.1V | | [ ] |
| +3.3V | ESP-12F VCC | 3.3V | ±0.1V | | [ ] |
| +3.3V | CH340G VCC | 3.3V | ±0.1V | | [ ] |

### 1.3 USB-C Orientation Test (Critical - Issue #2)

**Note:** Due to known issue #2, the board may only work in one cable orientation.

| Test | Status |
|------|--------|
| Orientation A: Power LED/voltage present | [ ] |
| Orientation B: Power LED/voltage present | [ ] |
| Both orientations work | [ ] |

**If only one orientation works:** Confirms issue #2. Board is functional but add CC2 pull-down resistor bodge for production.

---

## Stage 2: USB-to-Serial (CH340G)

### 2.1 USB Enumeration

**Procedure:**
1. Connect board to computer via USB-C
2. Check for new serial device

**Linux:**
```bash
dmesg | tail -20
# Look for: ch341-uart converter now attached to ttyUSBx

ls /dev/ttyUSB*
```

**macOS:**
```bash
ls /dev/tty.usbserial*
# or
ls /dev/tty.wchusbserial*
```

**Windows:**
- Device Manager → Ports (COM & LPT)
- Look for "USB-SERIAL CH340 (COMx)"

| Test | Expected | Actual | Status |
|------|----------|--------|--------|
| CH340G enumerated | Yes | | [ ] |
| Serial port appears | /dev/ttyUSBx or COMx | | [ ] |
| No driver errors | Clean enumeration | | [ ] |

### 2.2 Serial Loopback Test

**If ESP8266 is not programmed yet, test CH340G independently:**

1. Short CH340G TX to RX temporarily (careful - do at connector or test point)
2. Open serial terminal at 115200 baud
3. Type characters - they should echo back

| Test | Expected | Status |
|------|----------|--------|
| Loopback echo works | Characters echo | [ ] |

---

## Stage 3: ESP-12F (ESP8266)

### 3.1 Boot Mode Verification

**Check boot strapping pins with scope/logic analyzer or multimeter (with board powered):**

| Pin | Required State | Measured | Status |
|-----|----------------|----------|--------|
| GPIO0 (via R20) | HIGH (3.3V) | | [ ] |
| GPIO2 | HIGH (internal pull-up) | | [ ] |
| GPIO15 (via R22) | LOW (0V) | | [ ] |
| EN (via R21) | HIGH (3.3V) | | [ ] |
| ~RST (via R19) | HIGH (3.3V) | | [ ] |

### 3.2 Serial Boot Log

**Procedure:**
1. Open serial terminal at 74880 baud (ESP8266 boot ROM baud rate)
2. Press reset button (SW1)
3. Observe boot message

**Expected output (approximately):**
```
ets Jan  8 2013,rst cause:1, boot mode:(3,7)
load 0x40100000, len XXXX, room 16
...
```

| Test | Expected | Status |
|------|----------|--------|
| Boot message appears at 74880 baud | Yes | [ ] |
| boot mode:(3,x) - normal flash boot | Mode 3 | [ ] |
| No continuous reboot loop | Stable | [ ] |

### 3.3 Flash Test Firmware

**Upload blink/serial test sketch:**

```cpp
// File: test_firmware.ino
void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.println("\n\n=== RocketClock Bring-Up Test ===");
    Serial.println("ESP8266 running!");
}

void loop() {
    Serial.println("Heartbeat...");
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(1000);
}
```

**Arduino IDE Settings:**
- Board: "Generic ESP8266 Module" or "NodeMCU 1.0"
- Flash Size: 4MB (or as appropriate)
- Upload Speed: 115200
- Port: (your serial port)

| Test | Expected | Status |
|------|----------|--------|
| Firmware uploads successfully | No errors | [ ] |
| Serial output at 115200 baud | "Heartbeat..." messages | [ ] |
| Auto-reset for programming works | Yes | [ ] |

---

## Stage 4: I2C Bus

### 4.1 I2C Pull-Up Verification

**Measure with power ON, no I2C activity:**

| Signal | Test Point | Expected | Actual | Status |
|--------|------------|----------|--------|--------|
| SDA | R1 | 3.3V (pulled high) | | [ ] |
| SCL | R2 | 3.3V (pulled high) | | [ ] |

### 4.2 I2C Device Scan

**Upload I2C scanner:**

```cpp
// File: i2c_scan.ino
#include <Wire.h>

void setup() {
    Wire.begin(4, 5);  // SDA=GPIO4, SCL=GPIO5 (adjust if different)
    Serial.begin(115200);
    Serial.println("\nI2C Scanner");
}

void loop() {
    Serial.println("Scanning...");
    int devices = 0;

    for (byte addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("Found device at 0x%02X\n", addr);
            devices++;
        }
    }

    Serial.printf("Found %d device(s)\n\n", devices);
    delay(5000);
}
```

**Expected I2C Devices:**

| Device | Address | Found | Status |
|--------|---------|-------|--------|
| TMP112 (U3) | 0x48 | [ ] | [ ] |
| ATtiny84A (U5) - if I2C slave firmware | 0x?? | [ ] | [ ] |

### 4.3 TMP112 Temperature Read

```cpp
// File: tmp112_test.ino
#include <Wire.h>

#define TMP112_ADDR 0x48

void setup() {
    Wire.begin(4, 5);
    Serial.begin(115200);
    Serial.println("\nTMP112 Temperature Test");
}

void loop() {
    Wire.beginTransmission(TMP112_ADDR);
    Wire.write(0x00);  // Temperature register
    Wire.endTransmission();

    Wire.requestFrom(TMP112_ADDR, 2);
    if (Wire.available() == 2) {
        int16_t raw = (Wire.read() << 8) | Wire.read();
        raw >>= 4;  // 12-bit resolution
        float tempC = raw * 0.0625;
        Serial.printf("Temperature: %.2f C\n", tempC);
    } else {
        Serial.println("TMP112 read failed!");
    }

    delay(1000);
}
```

| Test | Expected | Actual | Status |
|------|----------|--------|--------|
| TMP112 responds | Yes | | [ ] |
| Temperature reading | ~20-30°C (room temp) | | [ ] |
| Reading changes when touched | Yes (increases) | | [ ] |

---

## Stage 5: MAX7219 LED Matrix

### 5.1 SPI Signal Verification (Optional)

**With oscilloscope/logic analyzer, verify signals during SPI activity:**

| Signal | Pin | Expected | Status |
|--------|-----|----------|--------|
| CLK | | Clean clock, no ringing | [ ] |
| DIN (MOSI) | | Clean data | [ ] |
| ~CS | | Active low during transfer | [ ] |

**Note:** Due to issue #1 (3.3V driving 5V inputs), signals may have marginal high levels. Check if V_IH is reached.

### 5.2 LED Matrix Test

```cpp
// File: max7219_test.ino
// Requires LedControl library

#include <LedControl.h>

// DIN, CLK, CS, number of devices
LedControl lc = LedControl(13, 14, 15, 1);  // Adjust pins as needed

void setup() {
    Serial.begin(115200);
    Serial.println("\nMAX7219 LED Matrix Test");

    lc.shutdown(0, false);  // Wake up
    lc.setIntensity(0, 8);  // Medium brightness
    lc.clearDisplay(0);

    Serial.println("Displaying test pattern...");
}

void loop() {
    // All LEDs on
    Serial.println("All ON");
    for (int row = 0; row < 8; row++) {
        lc.setRow(0, row, 0xFF);
    }
    delay(1000);

    // All LEDs off
    Serial.println("All OFF");
    lc.clearDisplay(0);
    delay(1000);

    // Checkerboard
    Serial.println("Checkerboard");
    for (int row = 0; row < 8; row++) {
        lc.setRow(0, row, (row % 2) ? 0xAA : 0x55);
    }
    delay(1000);

    // Walking row
    Serial.println("Walking row");
    for (int row = 0; row < 8; row++) {
        lc.clearDisplay(0);
        lc.setRow(0, row, 0xFF);
        delay(200);
    }
}
```

| Test | Expected | Status |
|------|----------|--------|
| MAX7219 initializes (no hang) | Yes | [ ] |
| All LEDs light up | Yes | [ ] |
| All LEDs turn off | Yes | [ ] |
| Checkerboard pattern correct | Yes | [ ] |
| No flickering or glitches | Stable display | [ ] |
| Brightness control works | Yes | [ ] |

**If display is glitchy:** Likely issue #1 (level shifting). Try reducing SPI clock speed or add level shifter.

---

## Stage 6: Buzzer (ATtiny84A)

### 6.1 ATtiny84A Power Verification

| Measurement | Expected | Actual | Status |
|-------------|----------|--------|--------|
| U5 VCC | 3.3V | | [ ] |
| U5 GND | 0V | | [ ] |

### 6.2 ATtiny84A ISP Programming

**If ATtiny84A needs firmware:**

1. Connect ISP programmer to J? (ISP header)
2. Verify connections: MOSI, MISO, SCK, RESET, VCC, GND
3. Program with test firmware

```cpp
// ATtiny84A buzzer test (simplified)
// Assumes buzzer on specific pin via Q3 PNP driver

#define BUZZER_PIN 0  // Adjust to actual pin

void setup() {
    pinMode(BUZZER_PIN, OUTPUT);
}

void loop() {
    // 1kHz tone for 500ms
    for (int i = 0; i < 500; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delayMicroseconds(500);
        digitalWrite(BUZZER_PIN, LOW);
        delayMicroseconds(500);
    }
    delay(1000);
}
```

| Test | Expected | Status |
|------|----------|--------|
| ATtiny84A detected by programmer | Yes | [ ] |
| Firmware uploads successfully | Yes | [ ] |
| Buzzer produces tone | Audible 1kHz beep | [ ] |

### 6.3 I2C Slave Mode (if applicable)

If ATtiny84A runs as I2C slave for ESP8266 control:

| Test | Expected | Status |
|------|----------|--------|
| ATtiny84A appears on I2C scan | Yes (at programmed address) | [ ] |
| ESP8266 can trigger buzzer via I2C | Yes | [ ] |

---

## Stage 7: Full System Integration Test

### 7.1 Combined Firmware Test

Upload comprehensive test firmware to ESP8266:

```cpp
// File: full_system_test.ino
#include <Wire.h>
#include <LedControl.h>

#define TMP112_ADDR 0x48

LedControl lc = LedControl(13, 14, 15, 1);  // Adjust pins

void setup() {
    Serial.begin(115200);
    Wire.begin(4, 5);

    lc.shutdown(0, false);
    lc.setIntensity(0, 4);
    lc.clearDisplay(0);

    Serial.println("\n========================================");
    Serial.println("   RocketClock Full System Test v1.0");
    Serial.println("========================================\n");
}

void loop() {
    // Read temperature
    Wire.beginTransmission(TMP112_ADDR);
    Wire.write(0x00);
    Wire.endTransmission();
    Wire.requestFrom(TMP112_ADDR, 2);

    float tempC = 0;
    if (Wire.available() == 2) {
        int16_t raw = (Wire.read() << 8) | Wire.read();
        raw >>= 4;
        tempC = raw * 0.0625;
        Serial.printf("[TMP112] Temperature: %.1f C\n", tempC);
    }

    // Display temperature on matrix (simple digit display)
    int tempInt = (int)(tempC + 0.5);
    displayNumber(tempInt);

    // Heartbeat
    static int counter = 0;
    Serial.printf("[SYSTEM] Heartbeat #%d\n", ++counter);

    delay(2000);
}

void displayNumber(int num) {
    // Simple 2-digit display (left half = tens, right half = ones)
    lc.clearDisplay(0);

    int tens = (num / 10) % 10;
    int ones = num % 10;

    // This is a simplified example - implement proper digit rendering
    lc.setRow(0, 3, tens);
    lc.setRow(0, 4, ones);
}
```

### 7.2 System Test Checklist

| Test | Status |
|------|--------|
| ESP8266 boots reliably | [ ] |
| Serial output works | [ ] |
| Temperature sensor reads | [ ] |
| LED matrix displays data | [ ] |
| No crashes over 10 minutes | [ ] |
| WiFi connects (if tested) | [ ] |

### 7.3 Power Consumption

| State | Expected | Measured | Status |
|-------|----------|----------|--------|
| Idle (WiFi off) | ~80mA | | [ ] |
| Active (WiFi connected) | ~100-150mA | | [ ] |
| WiFi TX burst | ~300-400mA peak | | [ ] |
| All LEDs on (MAX7219) | +up to 200mA | | [ ] |

---

## Stage 8: Known Issues Verification

Check if known issues from the design review are present:

| Issue # | Description | Observed? | Workaround Applied? |
|---------|-------------|-----------|---------------------|
| #1 | 3.3V→5V level shifting missing | [ ] | [ ] |
| #2 | USB-C works only in one orientation | [ ] | [ ] |
| #3 | GPIO2 boot issues | [ ] | [ ] |
| #4 | Net naming (causes ERC issues?) | [ ] | N/A |
| #5 | Power instability (no bulk cap) | [ ] | [ ] |

---

## Test Summary

| Stage | Description | Pass | Fail | Notes |
|-------|-------------|------|------|-------|
| 1 | Power Supply | [ ] | [ ] | |
| 2 | USB-to-Serial | [ ] | [ ] | |
| 3 | ESP-12F | [ ] | [ ] | |
| 4 | I2C Bus | [ ] | [ ] | |
| 5 | LED Matrix | [ ] | [ ] | |
| 6 | Buzzer | [ ] | [ ] | |
| 7 | Full System | [ ] | [ ] | |
| 8 | Known Issues | [ ] | [ ] | |

**Overall Result:** [ ] PASS / [ ] FAIL

**Tested By:** _________________
**Date:** _________________
**Notes:**

---

## Troubleshooting Guide

### ESP8266 won't boot
1. Check GPIO0, GPIO2, GPIO15 strapping
2. Verify 3.3V rail is stable
3. Check for shorts on flash pins (GPIO9, GPIO10)
4. Try external serial adapter bypassing CH340G

### No USB enumeration
1. Check USB-C cable (try different cable)
2. Try other USB-C orientation (issue #2)
3. Verify 5V present on VBUS
4. Check CH340G crystal (12MHz) is oscillating

### I2C devices not found
1. Verify pull-ups present (3.3V on SDA/SCL when idle)
2. Check correct GPIO pins in firmware
3. Verify TMP112 address (ADD0 pin state)

### LED matrix glitches
1. Likely issue #1 - marginal logic levels
2. Reduce SPI clock speed
3. Check for noise on power rails
4. Verify ISET resistor value (R4)

### Board gets hot
1. Immediately disconnect power
2. Check for solder bridges
3. Verify no reversed polarity components
4. Check LDO isn't oscillating (proper decoupling)

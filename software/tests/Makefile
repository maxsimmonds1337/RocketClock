# Usage: make flash SKETCH=<name> [PORT=/dev/cu.xxx]
# Sketches: timer rotate alphabet buzzer_test led_scan orientation smiley font temp
# Shared libs live in ../lib (RocketMatrix, RocketFont, RocketBuzzer).
# (64_LED_blink is a serial-only test and doesn't use the matrix.)
FQBN    = esp8266:esp8266:generic:ResetMethod=nodemcu,baud=115200
BAUD ?= 115200
PORT    ?= /dev/cu.wchusbserial10
HERE    = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SKETCH ?= timer
SKETCH_PATH = $(HERE)$(SKETCH)
LIBS    = $(HERE)../lib

.PHONY: compile upload flash monitor clean

compile:
	arduino-cli compile --fqbn $(FQBN) --libraries $(LIBS) $(SKETCH_PATH)

upload:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH_PATH)

flash: compile upload

monitor:
	arduino-cli monitor -p $(PORT) --config baudrate=$(BAUD)

clean:
	rm -rf $(SKETCH_PATH)/build

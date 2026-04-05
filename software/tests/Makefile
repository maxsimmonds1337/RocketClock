FQBN    = esp8266:esp8266:generic:ResetMethod=nodemcu,baud=115200
PORT    = /dev/cu.wchusbserial10
HERE    = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SKETCH ?= 64_LED_blink
SKETCH_PATH = $(HERE)$(SKETCH)

.PHONY: compile upload flash monitor clean

compile:
	arduino-cli compile --fqbn $(FQBN) $(SKETCH_PATH)

upload:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH_PATH)

flash: compile upload

monitor:
	arduino-cli monitor -p $(PORT) --config baudrate=115200

clean:
	rm -rf $(SKETCH_PATH)/build

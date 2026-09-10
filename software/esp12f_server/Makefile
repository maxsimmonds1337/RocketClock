FQBN    = esp8266:esp8266:generic:ResetMethod=nodemcu,baud=115200
PORT   ?= /dev/cu.wchusbserial10
HERE    = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SKETCH  = $(HERE)esp12f_server
LIBS    = $(HERE)../lib

.PHONY: compile upload flash monitor clean

compile:
	arduino-cli compile --fqbn $(FQBN) --libraries $(LIBS) $(SKETCH)

upload:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)

flash: compile upload

monitor:
	arduino-cli monitor -p $(PORT) --config baudrate=115200

clean:
	rm -rf $(SKETCH)/build

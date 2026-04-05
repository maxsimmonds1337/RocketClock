FQBN        = ATTinyCore:avr:attinyx4:chip=84
PROGRAMMER  = usbasp        # or arduinoisp, etc.
PORT        = usb
HERE        = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SKETCH      = $(HERE)attiny84_buzzer

.PHONY: compile upload flash

compile:
	arduino-cli compile --fqbn $(FQBN) $(SKETCH)

upload:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) --programmer $(PROGRAMMER) $(SKETCH)

flash: compile upload

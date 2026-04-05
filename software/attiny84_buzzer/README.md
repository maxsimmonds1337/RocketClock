# ATtiny84 Buzzer Firmware

Fork of the SparkFun buzzer firmware for ATtiny84.

## Setup

1. Clone the upstream repo here:
   ```
   git clone <sparkfun-repo-url> .
   ```

2. Install ATTinyCore board support:
   ```
   arduino-cli config set board_manager.additional_urls \
     http://drazzy.com/package_drazzy.com_index.json
   arduino-cli core update-index
   arduino-cli core install ATTinyCore:avr
   ```

3. Build and flash via the Makefile.

## Makefile usage

See `Makefile` — requires a USBasp or similar ISP programmer.

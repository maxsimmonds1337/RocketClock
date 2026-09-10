# RocketClock Control System — Design & Roadmap

Status: **design / in progress.** This doc is the handoff so we can pick up where
we left off. It captures the target architecture for controlling the display
over WiFi from a laptop/phone client, the modes, the API, and what's built vs.
deferred.

> **MCU note:** the board is an **ESP8266 (ESP-12F)**, not an ESP32 (early
> project notes speculated ESP32). The server base is `ESP8266WebServer`.
> This matters for memory budgeting — see [Feeds & APIs](#feeds--apis).

---

## 1. Current state (built this far)

**Firmware libraries** (`software/lib/`, shared via `--libraries` in the Makefile):
- **RocketMatrix** — MAX7219 8×8 driver. Handles the no-decode segment-bit remap
  so logical `(col,row)` maps to the right LED. `setOrientation()` for panel
  calibration. Framebuffer + `setPixel/fill/clear/show/setBrightness`.
- **RocketFont** — full 8×8 glyph set (A–Z, a–z, 0–9, punctuation, °, £) from
  public-domain font8x8. Scroll-ready: `drawGlyphAt(m, g, xOffset)`,
  `drawText(m, s, xOffset)`, `indexOf(char)`, `textWidth()`.
- **RocketBuzzer** — I2C driver for the on-board SparkFun Qwiic Buzzer (ATtiny84,
  addr `0x34`). `tone()`, `playMelody()`, note-frequency constants.

**Test sketches** (`software/tests/*`, `make flash SKETCH=<name>`): `timer`,
`rotate`, `alphabet`, `buzzer_test`, `led_scan`, `orientation`, `smiley`,
`font`, `temp`.

**Server v0** (`software/esp12f_server/`): `ESP8266WebServer` on port 80 with a
mode engine (`text` scroll / `timer` / `temp` / `off`), config struct, JSON API
(`/api/status|mode|config|text|timer|buzzer|temp`), and an embedded PROGMEM
dashboard at `/`. STA WiFi with SoftAP (`RocketClock`) fallback. **Compile-
verified only — not yet run on hardware** (fill in WiFi creds or use the AP).

**Hardware map** (from `design/RocketClock.net`):
- MAX7219 (U2): bit-banged SPI — DIN=GPIO14, CLK=GPIO12, CS=GPIO13.
- I2C bus: SDA=GPIO4, SCL=GPIO5.
  - Qwiic Buzzer / ATtiny84 (U5) @ `0x34`.
  - TMP112 temp sensor (U3) @ `0x48`.

---

## 2. Target architecture

```
  Laptop / phone browser  ──HTTP/JSON──►  ESP-12F (server)  ──►  MAX7219 matrix
   (dashboard: SPA from                    - mode engine          Qwiic buzzer
    LittleFS)                              - config store          TMP112
                                           - feed fetchers
```

- **Board = server.** Hosts a single-page dashboard from LittleFS and a JSON API.
- **Client = static SPA** (HTML/CSS/JS, no build step) served by the board.
  Talks to the API to set mode, configure it, and read status.
- **Mode engine** on the board runs one active mode in `loop()`, driven by a
  shared config struct. Rendering always goes through RocketMatrix/RocketFont.
- **Config persistence** in LittleFS (JSON) so settings survive reset.

---

## 3. Modes

| Mode | Description | Key config |
|------|-------------|-----------|
| `timer` | Count up, fill 64 LEDs over a duration, buzzer at end | duration, end-tune |
| `countdown` | Count down to zero / a target time, buzzer at end | target, end-tune |
| `clock` | Show time (scrolling HH:MM or digits) | 12/24h, format |
| `text` | Horizontal scrolling text | message, speed |
| `temp` | Show TMP112 temperature (e.g. `23°C`) | units, interval |
| `news` | Scroll a headline feed | source, refresh |
| `calendar` | Next event / countdown to it | calendar source |
| `launch` | Next rocket launch name + T- countdown | provider, pad filter |

Cross-cutting config (all modes): **brightness** (0–15), **scroll speed**,
**buzzer volume**, active-mode selection.

---

## 4. HTTP API (proposed)

JSON over HTTP. Keep it flat and small (ESP8266 RAM).

| Method | Path | Body / params | Purpose |
|--------|------|---------------|---------|
| GET | `/api/status` | — | `{mode, brightness, ...}` current state |
| POST | `/api/mode` | `{"mode":"text"}` | switch active mode |
| POST | `/api/config` | `{"brightness":8,"scrollMs":60}` | set global config |
| POST | `/api/text` | `{"message":"HELLO","speed":60}` | configure text mode |
| POST | `/api/timer` | `{"seconds":1200,"tune":"scifi"}` | configure timer/countdown |
| POST | `/api/buzzer` | `{"tune":"scifi"}` or `{"freq":440,"ms":300}` | play a sound now |
| GET | `/api/temp` | — | current temperature |

Config writes persist to LittleFS. Consider a single `POST /api/config` with a
partial JSON merge to keep the surface small.

---

## 5. Feeds & APIs (later)

- **Rocket launches:** [Launch Library 2](https://thespacedevs.com/llapi)
  (`/2.2.0/launch/upcoming/`) — next launch name + net time for a T- countdown.
- **Calendar:** Google Calendar (this repo already has Google MCP access for
  prototyping the data shape) or a published iCal URL.
- **News:** an RSS/JSON headline source.

> **ESP8266 constraint:** TLS (HTTPS) is RAM-tight on the ESP8266 and many of
> these APIs are HTTPS-only. Options: (a) `BearSSL` with a pinned cert and tight
> heap management, (b) a **tiny proxy** (home server / cloud function) that
> fetches + trims JSON to a minimal shape the board can pull over plain HTTP.
> The proxy is the pragmatic path and keeps secrets off the device.

---

## 6. Multi-module horizontal scrolling (next hardware step)

A second 8×8 board will be **daisy-chained in series** (MAX7219 DOUT→DIN). Plan:
- Extend RocketMatrix to N chained modules: framebuffer becomes `8*N` columns;
  `show()` clocks out N words per digit register (farthest module first).
- RocketFont already renders by x-offset, so scrolling across modules is just a
  wider virtual canvas and the same "advance xOffset" loop.
- Verify refresh rate stays >50 Hz as modules are added (vision-doc concern).

---

## 7. Issue tracking

- **Now:** `ISSUES.md` at repo root (hardware + firmware + feature backlog).
- **Later:** an "Issues" tab in the dashboard SPA (read/annotate the same list,
  served by the board or a small file). Deferred — not built yet.

---

## 8. Next steps (pick up here)

1. **Verify buzzer** with `make flash SKETCH=buzzer_test` — confirm the tune and
   that the ATtiny84 is flashed with Qwiic firmware (`software/attiny84_buzzer`).
2. **Flash the real 20-min `timer`** (done: fills over 20 min, buzzer finale).
3. **Mode engine skeleton** in `esp12f_server`: config struct + `/api/mode`,
   `/api/config`, render dispatch. Start with `text` (scroll) and `timer`.
4. **Dashboard SPA** in LittleFS: mode picker, brightness/speed sliders, text
   box, buzzer test button. Plain HTML/JS, no build.
5. **Second board** → extend RocketMatrix to chained modules; test scrolling.
6. **Feeds** via a trimming proxy; add `launch` mode first (most fun).

## References
- Vision/narrative: `maxsimmonds.engineer/led_streaming_display___rocket_countdown/index.md`
- Board bring-up: `BOARD_BRINGUP.md`
- Netlist: `design/RocketClock.net`

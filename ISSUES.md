# RocketClock — Issue Tracker

Lightweight issue log (hardware, firmware, features). Eventually this feeds an
"Issues" tab in the dashboard (see `docs/CONTROL_SYSTEM.md` §7). Status: `open`,
`in-progress`, `done`, `wontfix`.

## Hardware

| # | Status | Severity | Title | Notes |
|---|--------|----------|-------|-------|
| H1 | open | high | No 3.3V→5V level shifting on MAX7219 SPI | ESP drives 3.3V into 5V logic; marginal V_IH. Works so far; add shifter for production. |
| H2 | open | med | USB-C works in only one orientation | Missing CC2 pull-down. Bodge: add CC pull-down. |
| H3 | open | med | GPIO2 boot strapping | Verify no boot issues from GPIO2 usage. |
| H4 | open | low | Net naming causes ERC noise | Cosmetic/ERC. |
| H5 | open | med | No bulk cap — power instability | Add bulk cap near MAX7219 for LED current surges. |
| H6 | open | low | **Dead pixel D65** (col7,row7 = DIG_7 ∩ SEG_DP) | Found in `led_scan`. Both drive lines good → isolated joint/LED. Reflow D65, else replace. |

## Firmware

| # | Status | Severity | Title | Notes |
|---|--------|----------|-------|-------|
| F1 | done | — | MAX7219 fill order off-by-one | Fixed: no-decode segment-bit remap in RocketMatrix (`rowBit`). |
| F2 | done | — | Shared driver libs | RocketMatrix / RocketFont / RocketBuzzer in `software/lib`. |
| F3 | open | high | Buzzer not verified end-to-end | `buzzer_test` flashes; confirm tune + ATtiny84 has Qwiic firmware. |
| F4 | open | med | Panel orientation not finalised | `setOrientation()` exists; corner test (`orientation`) confirmed default OK for images — re-check per assembled unit. |
| F5 | open | low | font8x8 glyphs thinner than hand-drawn set | Option: keep bold uppercase, font8x8 for rest, or center glyphs in cell. |
| F6 | done | med | Random all-LEDs-lit at boot | Fixed `RocketMatrix::begin()` order: configure+clear while shut down, enable display LAST, display-test off. |

## Features / backlog (see docs/CONTROL_SYSTEM.md)

| # | Status | Title |
|---|--------|-------|
| B1 | in-progress | Mode engine + config struct on server | v0 built (text/timer/temp modes + config). Compiles; untested on HW (needs WiFi/SoftAP). |
| B2 | done | Dashboard SPA | Mission-control revamp: CONTROL/TELEMETRY tabs, live CONNECTED/OFFLINE indicator, panel styling. Served at `/` (8.6KB). Verified HTTP 200. |
| B19 | done | Serial-over-WiFi log streaming | Ring buffer (40 lines) mirrors serial; `/api/logs?since=N` polled by TELEMETRY console. Verified boot telemetry streams. |
| B3 | in-progress | Horizontal scrolling across chained modules | Driver rewritten for N panels + serpentine/180-flip (`setLayout`). 1x1 verified on HW; multi-panel needs 2nd board to calibrate. |
| B4 | in-progress | `launch` mode + T- countdown — **namesake**. Cloudflare proxy DEPLOYED (rocketclock-launch, lldev endpoint to dodge per-IP throttle) + wired in secrets.h. Verified proxy returns 110B. Flash+HW test pending. |
| B5 | open | `calendar` + `news` modes |
| B6 | open | In-dashboard issues tab |
| B11 | open | Vertical text scrolling (blog wishlist; only horizontal done) |
| B12 | open | Word of the day (dictionary API) — from blog wishlist |
| B13 | open | Image display mode: greyscale→kernel-avg→threshold@127 (helper `image_to_led_grid.c` exists) |
| B14 | open | Launch-synced countdown (10s/T- countdown aligned to a real launch time) |
| B7 | open | Persist config to LittleFS (settings reset on reboot) |
| B8 | done | NTP `clock` mode + `alarm` (HH:MM, fires buzzer). Verified: time syncs, API works. |
| B15 | in-progress | `mars` mode: Mars time (MTC), Sol Date, Curiosity/Perseverance sols, season — all on-device (Mars24), no API. Compiles; math verified vs NASA (Ls 240.09 vs 240.20). Flash+eyeball pending. |
| B16 | in-progress | Live Curiosity Mars temp via trimming proxy. Cloudflare Worker DEPLOYED (rocketclock-mars, trims NASA MSL 1.7MB→150B) + wired in secrets.h. Verified live (-71/-5C Sunny). Flash pending. |
| B17 | wontfix | Rover location / "moving vs sleeping" status: no working free live API (NASA mars-photos backend 404; no public motion-state feed). |
| B9 | done | `weather` mode via wttr.in (HTTP, IP-located). Verified: fetch returns, no hang. |
| B18 | in-progress | `air` mode via Open-Meteo Air Quality (free, no key, 397B): European AQI + band + PM2.5/PM10. Verified off-board (AQI 20 Fair). Coords `AQ_LAT/AQ_LON` default Tallinn. Flash+test pending. |
| B10 | done | Live panel-layout config (cols/rows/serpentine/flip) via dashboard + `/api/panels`. |

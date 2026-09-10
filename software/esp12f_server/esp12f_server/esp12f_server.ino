// RocketClock server (v0) - ESP-12F
// Hosts a dashboard + JSON API to control the 8x8 display. Mode engine renders
// through RocketMatrix/RocketFont; buzzer via RocketBuzzer. UI is embedded in
// PROGMEM (no LittleFS upload needed). If WiFi STA fails it falls back to a
// SoftAP so the dashboard is always reachable.
//
// NOTE: untested on hardware yet - needs real WiFi creds (or use the SoftAP)
// and a browser. Compile-verified only. See docs/CONTROL_SYSTEM.md.
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <time.h>
#include <RocketMatrix.h>
#include <RocketFont.h>
#include <RocketBuzzer.h>
#include <RocketMelodies.h>
#include "secrets.h"          // WIFI_SSID / WIFI_PASS (gitignored; see secrets.h.example)

// ---- config ----------------------------------------------------------------
const char *STA_SSID = WIFI_SSID;
const char *STA_PASS = WIFI_PASS;
const char *AP_SSID  = "RocketClock";     // SoftAP fallback (open)
const uint8_t TMP112_ADDR = 0x48;

// Timezone for the NTP clock/alarm. Europe/Tallinn (Estonia, EET/EEST).
// Change to yours: https://github.com/nayarsystems/posix_tz_db
#define TZ_INFO "EET-2EEST,M3.5.0/3,M10.5.0/4"

// Optional: Cloudflare Worker URLs for trimmed feeds (see proxy/). Set these in
// secrets.h to enable. MARS proxy -> live Curiosity temps. LAUNCH proxy ->
// cached/trimmed next launch (else the firmware hits Launch Library 2 directly).
#ifndef MARS_PROXY_URL
#define MARS_PROXY_URL ""
#endif
#ifndef LAUNCH_PROXY_URL
#define LAUNCH_PROXY_URL ""
#endif

// Air-quality location (Open-Meteo, free, no key). Default: Tallinn, EE.
#define AQ_LAT "59.437"
#define AQ_LON "24.754"

enum Mode { MODE_TEXT, MODE_TIMER, MODE_TEMP, MODE_CLOCK, MODE_WEATHER, MODE_LAUNCH, MODE_MARS, MODE_AQI, MODE_OFF };

// Panel layout defaults - edit here, or change live from the dashboard
// (POST /api/panels). cols x rows modules; serpentine = snake wiring;
// flip = odd rows rotated 180. See RocketMatrix::setLayout.
#define PANEL_COLS 1      // panels are hot-set from the dashboard (Cols=2/3 when attached)
#define PANEL_ROWS 1
#define PANEL_SERPENTINE true
#define PANEL_FLIP       true

struct Config {
  Mode     mode      = MODE_TEXT;
  uint8_t  brightness = 5;      // 0..15
  uint16_t scrollMs   = 60;     // ms per scroll step
  char     text[64]   = "ROCKETCLOCK";
  uint32_t timerSecs  = 1200;   // 20 min
  uint8_t  cols = PANEL_COLS, rows = PANEL_ROWS;
  bool     serpentine = PANEL_SERPENTINE, flip = PANEL_FLIP;
  uint8_t  alarmH = 7, alarmM = 30;   // alarm time (24h)
  bool     alarmOn = false;
  char     city[32] = "";       // weather location; empty = auto (by IP)
} cfg;

// ---- hardware --------------------------------------------------------------
RocketMatrix matrix;
RocketBuzzer buzzer;
ESP8266WebServer server(80);

// ---- runtime state ---------------------------------------------------------
int scrollX = 8;
unsigned long lastScroll = 0;
unsigned long timerStart = 0;
int  timerLast = -1;
bool timerDone = false;
char tempStr[16] = "";
unsigned long lastTemp = 0;

// ---- rendering helpers -----------------------------------------------------
void scrollText(const char *s) {
  if (millis() - lastScroll < cfg.scrollMs) return;
  lastScroll = millis();
  RocketFont::drawText(matrix, s, scrollX);
  if (--scrollX < -RocketFont::textWidth(s)) scrollX = matrix.width();
}

// Apply the panel layout to the driver (must re-init the chain).
void applyLayout() {
  matrix.setLayout(cfg.cols, cfg.rows, cfg.serpentine, cfg.flip);
  matrix.begin(cfg.brightness);
  scrollX = matrix.width();
}

// --- clock / alarm / weather ---
char clockStr[8]    = "--:--";
char weatherStr[48] = "WEATHER...";
unsigned long lastWeather = 0;
int  lastAlarmMin = -1;

bool timeReady() { return time(nullptr) > 100000UL; }  // NTP has synced

void updateClockStr() {
  if (!timeReady()) { strcpy(clockStr, "SYNC"); return; }
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  snprintf(clockStr, sizeof(clockStr), "%02d:%02d", t->tm_hour, t->tm_min);
}

// Fire the alarm once when local time hits alarmH:alarmM.
void checkAlarm() {
  if (!cfg.alarmOn || !timeReady()) return;
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  if (t->tm_hour == cfg.alarmH && t->tm_min == cfg.alarmM && lastAlarmMin != t->tm_min) {
    lastAlarmMin = t->tm_min;
    for (int i = 0; i < 3; i++) {
      matrix.fill(true);  buzzer.playMelody(RocketMelodies::HEDWIG, RocketBuzzer::VOL_MAX);
      matrix.clear();     delay(200);
    }
  }
}

// Fetch weather text from wttr.in over HTTP (no key; empty city = by IP).
// wttr.in returns UTF-8; we strip the 0xC2 lead byte so "°" maps to Latin-1.
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) { strcpy(weatherStr, "NO WIFI"); return; }
  WiFiClient client; HTTPClient http;
  String url = String("http://wttr.in/") + cfg.city + "?format=%t+%C";
  http.begin(client, url);
  http.setUserAgent("curl");              // wttr.in returns plain text for curl
  int code = http.GET();
  if (code == 200) {
    String p = http.getString(); p.trim();
    int j = 0;
    for (size_t i = 0; i < p.length() && j < (int)sizeof(weatherStr) - 1; i++) {
      uint8_t c = p[i];
      if (c == 0xC2) continue;            // UTF-8 lead for degree -> keep 0xB0
      weatherStr[j++] = (char)c;
    }
    weatherStr[j] = 0;
  } else {
    snprintf(weatherStr, sizeof(weatherStr), "WX ERR %d", code);
  }
  http.end();
}

// --- rocket launch (Launch Library 2) ---
char launchName[72]  = "";
char launchStr[110]  = "LAUNCH...";
time_t launchNet     = 0;
unsigned long lastLaunchFetch = 0, lastLaunchBuild = 0;

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + doe - 719468L;
}
time_t utcToEpoch(int Y, int M, int D, int h, int mi, int s) {
  return (time_t)(daysFromCivil(Y, M, D) * 86400L + h * 3600L + mi * 60L + s);
}

// Fetch the next launch (name + net time) from LL2 over HTTPS. Rate-limited,
// so callers refresh infrequently. Crude string extraction (no JSON lib).
void fetchLaunch() {
  if (WiFi.status() != WL_CONNECTED) { strcpy(launchName, "NO WIFI"); launchNet = 0; return; }
  WiFiClientSecure client; client.setInsecure(); client.setTimeout(15000);
  HTTPClient http;
  // Use the trimming proxy if configured; else hit Launch Library 2 directly.
  const char *url = strlen(LAUNCH_PROXY_URL) ? LAUNCH_PROXY_URL
                    : "https://ll.thespacedevs.com/2.2.0/launch/upcoming/"
                      "?limit=1&hide_recent_previous=true&mode=list";
  if (!http.begin(client, url)) { strcpy(launchName, "HTTPS ERR"); launchNet = 0; return; }
  http.setUserAgent("RocketClock");
  int code = http.GET();
  if (code == 200) {
    String p = http.getString();
    int r = p.indexOf("\"results\"");
    int ni = p.indexOf("\"name\":\"", r);
    int ti = p.indexOf("\"net\":\"", r);
    if (ni >= 0) { ni += 8; int e = p.indexOf('"', ni); p.substring(ni, e).toCharArray(launchName, sizeof(launchName)); }
    if (ti >= 0) {
      String iso = p.substring(ti + 7, ti + 7 + 19);   // YYYY-MM-DDThh:mm:ss
      launchNet = utcToEpoch(iso.substring(0, 4).toInt(), iso.substring(5, 7).toInt(),
                             iso.substring(8, 10).toInt(), iso.substring(11, 13).toInt(),
                             iso.substring(14, 16).toInt(), iso.substring(17, 19).toInt());
    }
  } else {
    snprintf(launchName, sizeof(launchName), "LL2 ERR %d", code); launchNet = 0;
  }
  http.end();
}

// Compose "<name>  T-HH:MM:SS" (or T-Nd HH:MM far out, T+ after liftoff).
void buildLaunchStr() {
  if (launchName[0] == 0) { strcpy(launchStr, "LAUNCH..."); return; }
  if (launchNet == 0 || !timeReady()) { snprintf(launchStr, sizeof(launchStr), "%s", launchName); return; }
  long d = (long)(launchNet - time(nullptr));
  char t[24];
  long a = d < 0 ? -d : d;
  const char *sign = d < 0 ? "T+" : "T-";
  if (a >= 86400) snprintf(t, sizeof(t), "%s%ldd %02ld:%02ld", sign, a / 86400, (a / 3600) % 24, (a / 60) % 60);
  else            snprintf(t, sizeof(t), "%s%02ld:%02ld:%02ld", sign, a / 3600, (a / 60) % 60, a % 60);
  snprintf(launchStr, sizeof(launchStr), "%s  %s", launchName, t);
}

// --- Mars (time/sols/season computed on-device; live temp via optional proxy) ---
char marsStr[160] = "MARS...";
char marsWx[40]   = "";        // " -71/-5C Sunny" from proxy, or empty
unsigned long lastMarsBuild = 0, lastMarsWx = 0;

// Pull a value out of flat JSON (quoted or bare), e.g. jsonVal(p,"min_c").
String jsonVal(const String &p, const char *key) {
  int i = p.indexOf(String("\"") + key + "\"");
  if (i < 0) return "";
  i = p.indexOf(':', i); if (i < 0) return "";
  for (i++; i < (int)p.length() && (p[i] == ' ' || p[i] == '"'); i++) {}
  int j = i;
  while (j < (int)p.length() && p[j] != '"' && p[j] != ',' && p[j] != '}') j++;
  String v = p.substring(i, j); v.trim();
  return v;
}

// Fetch trimmed Curiosity weather from the proxy (if MARS_PROXY_URL is set).
void fetchMarsWx() {
  marsWx[0] = 0;
  if (strlen(MARS_PROXY_URL) == 0 || WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client; client.setInsecure(); client.setTimeout(12000);
  HTTPClient http;
  if (!http.begin(client, MARS_PROXY_URL)) return;
  if (http.GET() == 200) {
    String p = http.getString();
    String lo = jsonVal(p, "min_c"), hi = jsonVal(p, "max_c"), sky = jsonVal(p, "sky");
    if (lo.length()) snprintf(marsWx, sizeof(marsWx), "  %s/%sC %s", lo.c_str(), hi.c_str(), sky.c_str());
  }
  http.end();
}

// Mars Coordinated Time + Sol Date + rover mission sols + season (Ls).
// Algorithms: Mars24 (Allison & McEwen). Rover sol-0 Mars Sol Dates are const.
void buildMarsStr() {
  if (!timeReady()) { strcpy(marsStr, "MARS SYNC"); return; }
  double T = (double)time(nullptr);
  double JDTT = 2440587.5 + T / 86400.0 + 69.184 / 86400.0;  // TT Julian date
  double dt  = JDTT - 2451545.0;                             // days since J2000
  double MSD = (dt - 4.5) / 1.027491252 + 44796.0 - 0.00096; // Mars Sol Date
  double MTC = fmod(24.0 * MSD, 24.0);                        // Coordinated Mars Time
  int h = (int)MTC, m = (int)fmod(MTC * 60, 60), s = (int)fmod(MTC * 3600, 60);
  long cur = (long)(MSD - 49269.245);   // Curiosity landed 2012-08-06
  long per = (long)(MSD - 52304.447);   // Perseverance landed 2021-02-18
  double M = (19.3870 + 0.52402075 * dt) * PI / 180.0;       // Mars mean anomaly
  double Ls = fmod(270.3863 + 0.52403840 * dt + 10.691 * sin(M) + 0.623 * sin(2 * M)
                   + 0.050 * sin(3 * M) + 0.005 * sin(4 * M) + 0.0005 * sin(5 * M), 360.0);
  if (Ls < 0) Ls += 360;
  const char *season[] = {"N.SPRING", "N.SUMMER", "N.AUTUMN", "N.WINTER"};
  snprintf(marsStr, sizeof(marsStr),
    "MARS %02d:%02d:%02d  SOL %ld  CURIOSITY SOL %ld  PERSEVERANCE SOL %ld  %s%s",
    h, m, s, (long)MSD, cur, per, season[((int)(Ls / 90)) % 4], marsWx);
}

// --- air quality (Open-Meteo, free, no key) ---
char airStr[80]  = "AIR...";
unsigned long lastAir = 0;

void fetchAir() {
  if (WiFi.status() != WL_CONNECTED) { strcpy(airStr, "NO WIFI"); return; }
  WiFiClientSecure client; client.setInsecure(); client.setTimeout(12000);
  HTTPClient http;
  String url = String("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=")
             + AQ_LAT + "&longitude=" + AQ_LON + "&current=european_aqi,pm2_5,pm10";
  if (!http.begin(client, url)) { strcpy(airStr, "AQ ERR"); return; }
  if (http.GET() == 200) {
    String p = http.getString();
    String aqi = jsonVal(p, "european_aqi"), pm25 = jsonVal(p, "pm2_5"), pm10 = jsonVal(p, "pm10");
    const char *bands[] = {"GOOD", "FAIR", "MODERATE", "POOR", "V.POOR", "EXT.POOR"};
    int bi = aqi.toInt() / 20; if (bi > 5) bi = 5; if (bi < 0) bi = 0;
    snprintf(airStr, sizeof(airStr), "AIR %s %s  PM2.5 %s  PM10 %s",
             aqi.c_str(), bands[bi], pm25.c_str(), pm10.c_str());
  } else { strcpy(airStr, "AQ ERR"); }
  http.end();
}

float readTempC() {
  Wire.beginTransmission(TMP112_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return NAN;
  Wire.requestFrom((int)TMP112_ADDR, 2);
  if (Wire.available() < 2) return NAN;
  int16_t raw = (Wire.read() << 8) | Wire.read();
  return (raw >> 4) * 0.0625f;
}

void startTimer() { timerStart = millis(); timerLast = -1; timerDone = false; }

void runTimer() {
  if (timerDone) {
    buzzer.playMelody(RocketMelodies::HEDWIG, RocketBuzzer::VOL_MAX);
    delay(1200);
    return;
  }
  unsigned long total = cfg.timerSecs * 1000UL;
  unsigned long elapsed = millis() - timerStart;
  if (elapsed >= total) { matrix.fill(true); timerDone = true; return; }
  int idx = (int)(elapsed / (total / 64));
  if (idx == timerLast) return;
  for (int i = timerLast + 1; i <= idx && i < 64; i++) matrix.setPixel(i / 8, i % 8);
  timerLast = idx;
}

// ---- API -------------------------------------------------------------------
void sendStatus() {
  const char *m = cfg.mode == MODE_TEXT ? "text" : cfg.mode == MODE_TIMER ? "timer"
                : cfg.mode == MODE_TEMP ? "temp" : cfg.mode == MODE_CLOCK ? "clock"
                : cfg.mode == MODE_WEATHER ? "weather" : cfg.mode == MODE_LAUNCH ? "launch"
                : cfg.mode == MODE_MARS ? "mars" : cfg.mode == MODE_AQI ? "air" : "off";
  char buf[560];
  snprintf(buf, sizeof(buf),
    "{\"mode\":\"%s\",\"brightness\":%u,\"scrollMs\":%u,\"text\":\"%s\",\"timerSecs\":%lu,"
    "\"cols\":%u,\"rows\":%u,\"serpentine\":%s,\"flip\":%s,"
    "\"alarm\":\"%02u:%02u\",\"alarmOn\":%s,\"city\":\"%s\",\"time\":\"%s\",\"launch\":\"%s\"}",
    m, cfg.brightness, cfg.scrollMs, cfg.text, (unsigned long)cfg.timerSecs,
    cfg.cols, cfg.rows, cfg.serpentine ? "true" : "false", cfg.flip ? "true" : "false",
    cfg.alarmH, cfg.alarmM, cfg.alarmOn ? "true" : "false", cfg.city,
    (updateClockStr(), clockStr), launchStr);
  server.send(200, "application/json", buf);
}

void handleMode() {
  String m = server.arg("mode");
  if (m == "text") cfg.mode = MODE_TEXT;
  else if (m == "timer") { cfg.mode = MODE_TIMER; startTimer(); }
  else if (m == "temp") cfg.mode = MODE_TEMP;
  else if (m == "clock") cfg.mode = MODE_CLOCK;
  else if (m == "weather") { cfg.mode = MODE_WEATHER; lastWeather = 0; }
  else if (m == "launch")  { cfg.mode = MODE_LAUNCH; lastLaunchFetch = 0; }
  else if (m == "mars")    { cfg.mode = MODE_MARS; lastMarsBuild = 0; }
  else if (m == "air")     { cfg.mode = MODE_AQI; lastAir = 0; }
  else if (m == "off")  { cfg.mode = MODE_OFF; matrix.clear(); }
  scrollX = matrix.width();
  sendStatus();
}

void handleAlarm() {
  if (server.hasArg("time")) {              // "HH:MM"
    String tm = server.arg("time");
    int colon = tm.indexOf(':');
    if (colon > 0) {
      cfg.alarmH = constrain(tm.substring(0, colon).toInt(), 0, 23);
      cfg.alarmM = constrain(tm.substring(colon + 1).toInt(), 0, 59);
    }
  }
  if (server.hasArg("enabled")) cfg.alarmOn = server.arg("enabled") == "1";
  lastAlarmMin = -1;
  sendStatus();
}

void handleWeather() {                       // set city + switch to weather mode
  if (server.hasArg("city")) server.arg("city").toCharArray(cfg.city, sizeof(cfg.city));
  cfg.mode = MODE_WEATHER; lastWeather = 0; scrollX = matrix.width();
  sendStatus();
}

void handlePanels() {
  if (server.hasArg("cols")) cfg.cols = constrain(server.arg("cols").toInt(), 1, 16);
  if (server.hasArg("rows")) cfg.rows = constrain(server.arg("rows").toInt(), 1, 16);
  if (cfg.cols * cfg.rows > RocketMatrix::MAX_PANELS) cfg.rows = RocketMatrix::MAX_PANELS / cfg.cols;
  if (server.hasArg("serpentine")) cfg.serpentine = server.arg("serpentine") == "1";
  if (server.hasArg("flip"))       cfg.flip       = server.arg("flip") == "1";
  applyLayout();
  sendStatus();
}

void handleConfig() {
  if (server.hasArg("brightness")) {
    cfg.brightness = constrain(server.arg("brightness").toInt(), 0, 15);
    matrix.setBrightness(cfg.brightness);
  }
  if (server.hasArg("scrollMs"))
    cfg.scrollMs = constrain(server.arg("scrollMs").toInt(), 5, 1000);
  sendStatus();
}

void handleText() {
  if (server.hasArg("message")) {
    server.arg("message").toCharArray(cfg.text, sizeof(cfg.text));
    scrollX = 8;
  }
  cfg.mode = MODE_TEXT;
  sendStatus();
}

void handleTimer() {
  if (server.hasArg("seconds"))
    cfg.timerSecs = max(1L, server.arg("seconds").toInt());
  cfg.mode = MODE_TIMER;
  startTimer();
  sendStatus();
}

void handleBuzzer() {
  String t = server.arg("tune");
  if (t == "close_encounters") buzzer.playMelody(RocketMelodies::CLOSE_ENCOUNTERS, RocketBuzzer::VOL_MAX);
  else buzzer.playMelody(RocketMelodies::HEDWIG, RocketBuzzer::VOL_MAX);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleTemp() {
  float t = readTempC();
  char buf[48];
  snprintf(buf, sizeof(buf), "{\"tempC\":%.2f}", isnan(t) ? 0.0 : t);
  server.send(200, "application/json", buf);
}

// Dashboard SPA (served from PROGMEM).
const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RocketClock</title><style>
body{font-family:system-ui,sans-serif;max-width:520px;margin:1.5rem auto;padding:0 1rem;background:#0b0e14;color:#e6e6e6}
h1{font-size:1.3rem}fieldset{border:1px solid #333;border-radius:10px;margin:.8rem 0;padding:.8rem}
legend{color:#8ab4f8;padding:0 .4rem}button,input,select{font-size:1rem;padding:.5rem;border-radius:8px;border:1px solid #444;background:#161b22;color:#e6e6e6}
button{background:#1f6feb;border:0;cursor:pointer}button.sec{background:#30363d}label{display:block;margin:.5rem 0 .2rem}
.row{display:flex;gap:.5rem;flex-wrap:wrap}input[type=range]{width:100%}#status{font-family:monospace;font-size:.8rem;color:#9aa}
button.active{background:#2ea043;box-shadow:0 0 0 2px #8ab4f8}#cur{font-weight:600;color:#8ab4f8;margin:.2rem 0 .6rem}
</style></head><body>
<h1>🚀 RocketClock</h1>
<div id="cur">Mode: —</div>
<fieldset><legend>Mode</legend><div class="row" id="modes">
<button data-mode="text" onclick="mode('text')">Text</button><button data-mode="timer" onclick="mode('timer')">Timer</button>
<button data-mode="clock" onclick="mode('clock')">Clock</button><button data-mode="temp" onclick="mode('temp')">Temp</button>
<button data-mode="weather" onclick="mode('weather')">Weather</button><button data-mode="launch" onclick="mode('launch')">🚀 Launch</button>
<button data-mode="mars" onclick="mode('mars')">🔴 Mars</button><button data-mode="air" onclick="mode('air')">🌫️ Air</button>
<button data-mode="off" class="sec" onclick="mode('off')">Off</button></div></fieldset>
<fieldset><legend>Scrolling text</legend>
<input id="msg" value="ROCKETCLOCK" style="width:70%"><button onclick="setText()">Set</button></fieldset>
<fieldset><legend>Timer</legend>
<input id="mins" type="number" value="20" step="0.5" style="width:40%"> minutes
<button onclick="setTimer()">Start</button></fieldset>
<fieldset><legend>Display</legend>
<label>Brightness <span id="bv">5</span></label><input id="br" type="range" min="0" max="15" value="5" oninput="bv.textContent=this.value" onchange="cfg()">
<label>Scroll speed (ms/step) <span id="sv">60</span></label><input id="sp" type="range" min="5" max="300" value="60" oninput="sv.textContent=this.value" onchange="cfg()"></fieldset>
<fieldset><legend>Alarm</legend>
<input id="atime" type="time" value="07:30" style="width:35%">
<label><input type="checkbox" id="aon" style="width:auto"> Enabled</label>
<button onclick="setAlarm()">Set</button></fieldset>
<fieldset><legend>Weather</legend>
<input id="city" placeholder="city (blank = auto by IP)" style="width:55%">
<button onclick="setWeather()">Show</button></fieldset>
<fieldset><legend>Buzzer</legend><div class="row">
<button onclick="buzz('hedwig')">Hedwig</button><button onclick="buzz('close_encounters')">Close Encounters</button></div></fieldset>
<fieldset><legend>Panels</legend>
<div class="row">Cols <input id="cols" type="number" min="1" max="16" value="1" style="width:20%">
Rows <input id="rows" type="number" min="1" max="16" value="1" style="width:20%"></div>
<label><input type="checkbox" id="snake" checked style="width:auto"> Serpentine (snake wiring)</label>
<label><input type="checkbox" id="flip" checked style="width:auto"> Flip reverse rows 180°</label>
<button onclick="setPanels()">Apply</button></fieldset>
<pre id="status">loading...</pre>
<script>
const q=(p,o)=>fetch(p+(o?'?'+new URLSearchParams(o):''),{method:'POST'}).then(r=>r.json()).then(show);
const show=s=>{status.textContent=JSON.stringify(s,null,1);cur.textContent='Mode: '+(s.mode||'?').toUpperCase();
document.querySelectorAll('#modes button').forEach(b=>b.classList.toggle('active',b.dataset.mode===s.mode));
if(s.cols){cols.value=s.cols;rows.value=s.rows;snake.checked=s.serpentine;flip.checked=s.flip;}
if(s.alarm){atime.value=s.alarm;aon.checked=s.alarmOn;}if(s.city!==undefined)city.value=s.city;
if(s.time&&s.mode!=='clock')cur.textContent+='  ('+s.time+')';};
const mode=m=>q('/api/mode',{mode:m});
const setText=()=>q('/api/text',{message:msg.value});
const setTimer=()=>q('/api/timer',{seconds:Math.max(1,Math.round(mins.value*60))});
const setPanels=()=>q('/api/panels',{cols:cols.value,rows:rows.value,serpentine:snake.checked?1:0,flip:flip.checked?1:0});
const setAlarm=()=>q('/api/alarm',{time:atime.value,enabled:aon.checked?1:0});
const setWeather=()=>q('/api/weather',{city:city.value});
const cfg=()=>q('/api/config',{brightness:br.value,scrollMs:sp.value});
const buzz=t=>fetch('/api/buzzer?tune='+t,{method:'POST'});
fetch('/api/status').then(r=>r.json()).then(show);
</script></body></html>)HTML";

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void setup() {
  Serial.begin(115200);
  matrix.setLayout(cfg.cols, cfg.rows, cfg.serpentine, cfg.flip);
  matrix.begin(cfg.brightness);
  buzzer.begin();               // Wire.begin() (SDA=4, SCL=5)

  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASS);
  Serial.print("WiFi");
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nSTA: " + WiFi.localIP().toString());
    configTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");   // NTP for clock/alarm
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    Serial.println("\nSoftAP: " + WiFi.softAPIP().toString() + " (SSID " + AP_SSID + ")");
  }

  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/mode",   HTTP_POST, handleMode);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/text",   HTTP_POST, handleText);
  server.on("/api/timer",  HTTP_POST, handleTimer);
  server.on("/api/buzzer", HTTP_POST, handleBuzzer);
  server.on("/api/panels", HTTP_POST, handlePanels);
  server.on("/api/alarm",  HTTP_POST, handleAlarm);
  server.on("/api/weather",HTTP_POST, handleWeather);
  server.on("/api/temp",   handleTemp);
  server.begin();
  Serial.println("HTTP server started");
}

void handleStatus() { sendStatus(); }

void loop() {
  server.handleClient();
  checkAlarm();                       // fires regardless of active mode

  switch (cfg.mode) {
    case MODE_TEXT: scrollText(cfg.text); break;
    case MODE_TIMER: runTimer(); break;
    case MODE_TEMP:
      if (millis() - lastTemp > 1000) {
        lastTemp = millis();
        float t = readTempC();
        if (isnan(t)) strcpy(tempStr, "TEMP?");
        else snprintf(tempStr, sizeof(tempStr), "%.1f\xB0""C", t);
      }
      scrollText(tempStr);
      break;
    case MODE_CLOCK:
      updateClockStr();
      scrollText(clockStr);
      break;
    case MODE_WEATHER:
      if (lastWeather == 0 || millis() - lastWeather > 15UL * 60 * 1000) {
        lastWeather = millis();
        fetchWeather();
      }
      scrollText(weatherStr);
      break;
    case MODE_LAUNCH:
      if (lastLaunchFetch == 0 || millis() - lastLaunchFetch > 30UL * 60 * 1000) {
        lastLaunchFetch = millis();
        fetchLaunch();
      }
      if (millis() - lastLaunchBuild > 1000) { lastLaunchBuild = millis(); buildLaunchStr(); }
      scrollText(launchStr);
      break;
    case MODE_MARS:
      if (lastMarsWx == 0 || millis() - lastMarsWx > 60UL * 60 * 1000) { lastMarsWx = millis(); fetchMarsWx(); }
      if (millis() - lastMarsBuild > 1000) { lastMarsBuild = millis(); buildMarsStr(); }
      scrollText(marsStr);
      break;
    case MODE_AQI:
      if (lastAir == 0 || millis() - lastAir > 30UL * 60 * 1000) { lastAir = millis(); fetchAir(); }
      scrollText(airStr);
      break;
    case MODE_OFF: break;
  }
}

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
#include <LittleFS.h>
#include <ArduinoJson.h>
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
#ifndef AIR_PROXY_URL
#define AIR_PROXY_URL ""
#endif
#ifndef NEWS_PROXY_URL
#define NEWS_PROXY_URL ""
#endif

// Air-quality location (Open-Meteo, free, no key). Default: Tallinn, EE.
#define AQ_LAT "59.437"
#define AQ_LON "24.754"

enum Mode { MODE_TEXT, MODE_TIMER, MODE_TEMP, MODE_CLOCK, MODE_WEATHER, MODE_LAUNCH, MODE_MARS, MODE_AQI, MODE_NEWS, MODE_OFF };

// Event-engine types up here so Arduino's auto-generated prototypes resolve them.
#define MAX_EVENTS 6
#define MAX_ACTIONS 6
enum { A_TEXT, A_WEATHER, A_NEWS, A_MARS, A_AIR, A_CLOCK, A_TEMP, A_BUZZER };
enum { T_AT, T_RANGE, T_THR, T_DELTA };
struct Action { uint8_t type, param; uint16_t value; bool sec; char text[28]; };
struct Event { bool en; char name[20]; uint8_t trig, h, m, eh, em, src; bool above; int16_t x;
               uint8_t nAct; Action act[MAX_ACTIONS]; };

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
  bool     timerReverse = false; // false = fill on; true = start full, empty off
  uint8_t  timerSound = 0;       // 0=siren 1=hedwig 2=close_encounters
  uint8_t  fillOrder = 0;        // 0=rows 1=cols 2=snake-rows 3=snake-cols 4=panel
  bool     flip180 = false;      // rotate whole display 180 (USB-down mounting)
} cfg;

// ---- hardware --------------------------------------------------------------
RocketMatrix matrix;
RocketBuzzer buzzer;
ESP8266WebServer server(80);

// ---- log ring: serial mirrored to /api/logs for the dashboard console ------
#define LOG_LINES 40
#define LOG_LEN   84
char logRing[LOG_LINES][LOG_LEN];
uint32_t logSeq = 0;

void logln(const char *fmt, ...) {
  char msg[LOG_LEN];
  va_list ap; va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  char line[LOG_LEN];
  snprintf(line, sizeof(line), "T+%lus  %s", millis() / 1000, msg);  // mission-elapsed
  strncpy(logRing[logSeq % LOG_LINES], line, LOG_LEN - 1);
  logRing[logSeq % LOG_LINES][LOG_LEN - 1] = 0;
  logSeq++;
  Serial.println(line);
}

// ---- persistence (LittleFS + ArduinoJson; config only, not the TLS path) ----
const char *CFG_PATH = "/config.json";
bool cfgDirty = false;
unsigned long cfgTouched = 0;
void markDirty() { cfgDirty = true; cfgTouched = millis(); }   // debounced save

void eventsToJson(JsonArray arr);      // defined in the event engine below
void eventsFromJson(JsonArray arr);

void saveConfig() {
  JsonDocument d;
  eventsToJson(d["events"].to<JsonArray>());
  d["mode"] = (int)cfg.mode; d["bright"] = cfg.brightness; d["scroll"] = cfg.scrollMs;
  d["text"] = cfg.text; d["timer"] = cfg.timerSecs;
  d["cols"] = cfg.cols; d["rows"] = cfg.rows; d["serp"] = cfg.serpentine; d["flip"] = cfg.flip;
  d["alarmH"] = cfg.alarmH; d["alarmM"] = cfg.alarmM; d["alarmOn"] = cfg.alarmOn; d["city"] = cfg.city;
  d["tRev"] = cfg.timerReverse; d["tSnd"] = cfg.timerSound; d["fill"] = cfg.fillOrder; d["flip180"] = cfg.flip180;
  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) { logln("CFG save FAILED"); return; }
  serializeJson(d, f); f.close();
  logln("CFG saved");
}

void loadConfig() {
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) { logln("CFG none - defaults"); return; }
  JsonDocument d;
  DeserializationError e = deserializeJson(d, f); f.close();
  if (e) { logln("CFG parse error"); return; }
  cfg.mode = (Mode)(int)(d["mode"] | (int)cfg.mode);
  cfg.brightness = d["bright"] | cfg.brightness;
  cfg.scrollMs = d["scroll"] | cfg.scrollMs;
  strlcpy(cfg.text, d["text"] | cfg.text, sizeof(cfg.text));
  cfg.timerSecs = d["timer"] | cfg.timerSecs;
  cfg.cols = d["cols"] | cfg.cols; cfg.rows = d["rows"] | cfg.rows;
  cfg.serpentine = d["serp"] | cfg.serpentine; cfg.flip = d["flip"] | cfg.flip;
  cfg.alarmH = d["alarmH"] | cfg.alarmH; cfg.alarmM = d["alarmM"] | cfg.alarmM;
  cfg.alarmOn = d["alarmOn"] | cfg.alarmOn;
  strlcpy(cfg.city, d["city"] | cfg.city, sizeof(cfg.city));
  cfg.timerReverse = d["tRev"] | cfg.timerReverse;
  cfg.timerSound = d["tSnd"] | cfg.timerSound;
  cfg.fillOrder = d["fill"] | cfg.fillOrder;
  cfg.flip180 = d["flip180"] | cfg.flip180;
  if (d["events"].is<JsonArray>()) eventsFromJson(d["events"].as<JsonArray>());
  logln("CFG loaded");
}

// ---- runtime state ---------------------------------------------------------
int scrollX = 8;
unsigned long lastScroll = 0;
unsigned long timerStart = 0;
int  timerLast = -1;
bool timerDone = false;
char tempStr[16] = "";
unsigned long lastTemp = 0;
float tempVal = NAN, aqiVal = NAN;   // latest sampled values for event triggers

// ---- rendering helpers -----------------------------------------------------
long scrollCycle = 0;   // bumps each time the scroll wraps (for cycle-based actions)
void scrollText(const char *s) {
  if (millis() - lastScroll < cfg.scrollMs) return;
  lastScroll = millis();
  RocketFont::drawText(matrix, s, scrollX);
  if (--scrollX < -RocketFont::textWidth(s)) { scrollX = matrix.width(); scrollCycle++; }
}

// Apply the panel layout to the driver (must re-init the chain).
void applyLayout() {
  matrix.setLayout(cfg.cols, cfg.rows, cfg.serpentine, cfg.flip);
  matrix.setFlip180(cfg.flip180);
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
    logln("*** ALARM FIRING %02u:%02u ***", cfg.alarmH, cfg.alarmM);
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
  logln("WX %s", weatherStr);
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
  WiFiClientSecure client; client.setInsecure(); client.setBufferSizes(2048, 512); client.setTimeout(15000);
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
    int r = p.indexOf("\"results\"");   // direct LL2 wraps in results[]; proxy is flat
    if (r < 0) r = 0;
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
  logln("LAUNCH %s", launchName);
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

// Pull a value out of flat JSON, e.g. jsonVal(p,"headline"). Quoted strings are
// read to their closing quote (so commas inside a value are kept); bare values
// (numbers) stop at the next delimiter.
String jsonVal(const String &p, const char *key) {
  int i = p.indexOf(String("\"") + key + "\"");
  if (i < 0) return "";
  i = p.indexOf(':', i); if (i < 0) return "";
  i++;
  while (i < (int)p.length() && p[i] == ' ') i++;
  if (i < (int)p.length() && p[i] == '"') {          // quoted string
    i++; int j = i;
    while (j < (int)p.length() && p[j] != '"') j++;  // to closing quote (keeps commas)
    return p.substring(i, j);
  }
  int j = i;                                         // bare value (number/bool)
  while (j < (int)p.length() && p[j] != ',' && p[j] != '}' && p[j] != ' ') j++;
  String v = p.substring(i, j); v.trim();
  return v;
}

// Fetch trimmed Curiosity weather from the proxy (if MARS_PROXY_URL is set).
void fetchMarsWx() {
  marsWx[0] = 0;
  if (strlen(MARS_PROXY_URL) == 0 || WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client; client.setInsecure(); client.setBufferSizes(2048, 512); client.setTimeout(12000);
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
  bool proxy = strlen(AIR_PROXY_URL) > 0;   // proxy avoids the heavy direct-TLS handshake
  String url = proxy ? String(AIR_PROXY_URL)
             : String("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=")
               + AQ_LAT + "&longitude=" + AQ_LON + "&current=european_aqi,pm2_5,pm10";
  WiFiClientSecure client; client.setInsecure(); client.setBufferSizes(2048, 512); client.setTimeout(12000);
  HTTPClient http;
  if (!http.begin(client, url)) { strcpy(airStr, "AQ ERR"); return; }
  if (http.GET() == 200) {
    String p = http.getString();
    String aqi, pm25, pm10;
    if (proxy) {                          // flat {aqi,pm25,pm10}
      aqi = jsonVal(p, "aqi"); pm25 = jsonVal(p, "pm25"); pm10 = jsonVal(p, "pm10");
    } else {                              // direct: skip "current_units" (unit labels)
      int c = p.indexOf("\"current\":"); String cur = (c >= 0) ? p.substring(c) : p;
      aqi = jsonVal(cur, "european_aqi"); pm25 = jsonVal(cur, "pm2_5"); pm10 = jsonVal(cur, "pm10");
    }
    const char *bands[] = {"GOOD", "FAIR", "MODERATE", "POOR", "V.POOR", "EXT.POOR"};
    int bi = aqi.toInt() / 20; if (bi > 5) bi = 5; if (bi < 0) bi = 0;
    aqiVal = aqi.toInt();        // cache numeric AQI for event triggers
    snprintf(airStr, sizeof(airStr), "AIR %s %s  PM2.5 %s  PM10 %s",
             aqi.c_str(), bands[bi], pm25.c_str(), pm10.c_str());
  } else { strcpy(airStr, "AQ ERR"); }
  http.end();
  logln("%s", airStr);
}

// ---- news (BBC via proxy) --------------------------------------------------
char newsStr[120] = "NEWS...";
unsigned long lastNews = 0;

void fetchNews() {
  if (strlen(NEWS_PROXY_URL) == 0) { strcpy(newsStr, "NEWS: set NEWS_PROXY_URL"); return; }
  if (WiFi.status() != WL_CONNECTED) { strcpy(newsStr, "NO WIFI"); return; }
  WiFiClientSecure client; client.setInsecure(); client.setBufferSizes(2048, 512); client.setTimeout(12000);
  HTTPClient http;
  if (!http.begin(client, NEWS_PROXY_URL)) { strcpy(newsStr, "NEWS ERR"); return; }
  if (http.GET() == 200) {
    String p = http.getString();
    String h = jsonVal(p, "headline");
    if (h.length()) snprintf(newsStr, sizeof(newsStr), "%s", h.c_str());
  } else { strcpy(newsStr, "NEWS ERR"); }
  http.end();
  logln("NEWS %s", newsStr);
}

// ============================ EVENT ENGINE ============================
// An event = a trigger + an ordered sequence of timed actions. On trigger the
// sequencer takes over the display, plays each action for its window (cycles or
// seconds), then returns to the base mode. Types are defined near the top of
// the file (so Arduino's auto-generated prototypes can see them).
Event events[MAX_EVENTS];
uint8_t nEvents = 0;

int evActive = -1, evStep = -1;
unsigned long actStart = 0; long actStartCycle = 0, lastTrigTick = 0, lastAqiSample = 0;
int evLastMin[MAX_EVENTS]; bool evPrevCond[MAX_EVENTS], evValInit[MAX_EVENTS];
float evLastVal[MAX_EVENTS];

float srcValue(uint8_t src) { return src == 1 ? aqiVal : tempVal; }

void eventsToJson(JsonArray arr) {
  for (int i = 0; i < nEvents; i++) {
    Event &e = events[i];
    JsonObject o = arr.add<JsonObject>();
    o["en"] = e.en; o["name"] = e.name; o["trig"] = e.trig;
    o["h"] = e.h; o["m"] = e.m; o["eh"] = e.eh; o["em"] = e.em;
    o["src"] = e.src; o["above"] = e.above; o["x"] = e.x;
    JsonArray a = o["act"].to<JsonArray>();
    for (int j = 0; j < e.nAct; j++) {
      Action &c = e.act[j];
      JsonObject ao = a.add<JsonObject>();
      ao["t"] = c.type; ao["p"] = c.param; ao["v"] = c.value; ao["s"] = c.sec; ao["x"] = c.text;
    }
  }
}

void eventsFromJson(JsonArray arr) {
  nEvents = 0;
  for (JsonObject o : arr) {
    if (nEvents >= MAX_EVENTS) break;
    Event &e = events[nEvents];
    e.en = o["en"] | true; strlcpy(e.name, o["name"] | "event", sizeof(e.name));
    e.trig = o["trig"] | 0; e.h = o["h"] | 0; e.m = o["m"] | 0; e.eh = o["eh"] | 0; e.em = o["em"] | 0;
    e.src = o["src"] | 0; e.above = o["above"] | true; e.x = o["x"] | 0;
    e.nAct = 0;
    for (JsonObject ao : o["act"].as<JsonArray>()) {
      if (e.nAct >= MAX_ACTIONS) break;
      Action &c = e.act[e.nAct];
      c.type = ao["t"] | 0; c.param = ao["p"] | 0; c.value = ao["v"] | 3; c.sec = ao["s"] | false;
      strlcpy(c.text, ao["x"] | "", sizeof(c.text));
      e.nAct++;
    }
    evLastMin[nEvents] = -1; evPrevCond[nEvents] = false; evValInit[nEvents] = false;
    nEvents++;
  }
}

// Render the current action; true when its duration (cycles or seconds) elapsed.
bool renderAction(Action &a) {
  const char *s = a.text;
  switch (a.type) {
    case A_WEATHER: s = weatherStr; break;
    case A_NEWS:    s = newsStr; break;
    case A_MARS:    buildMarsStr(); s = marsStr; break;
    case A_AIR:     s = airStr; break;
    case A_CLOCK:   updateClockStr(); s = clockStr; break;
    case A_TEMP:    s = tempStr; break;
    case A_BUZZER:  return millis() - actStart >= 400;   // brief hold after the beep
  }
  scrollText(s);
  if (a.sec) return millis() - actStart >= (unsigned long)a.value * 1000UL;
  return (scrollCycle - actStartCycle) >= a.value;
}

void enterStep() {
  Action &a = events[evActive].act[evStep];
  actStart = millis(); actStartCycle = scrollCycle; scrollX = matrix.width();
  if (a.type == A_WEATHER) fetchWeather();
  else if (a.type == A_NEWS) fetchNews();
  else if (a.type == A_AIR)  fetchAir();
  else if (a.type == A_TEMP) { float t = readTempC();
    if (isnan(t)) strcpy(tempStr, "TEMP?"); else snprintf(tempStr, sizeof(tempStr), "%.1f\xB0""C", t); }
  else if (a.type == A_BUZZER) {
    if (a.param == 1)      buzzer.playMelody(RocketMelodies::HEDWIG, RocketBuzzer::VOL_MAX);
    else if (a.param == 2) buzzer.playMelody(RocketMelodies::CLOSE_ENCOUNTERS, RocketBuzzer::VOL_MAX);
    else                   buzzer.playMelody(RocketMelodies::SIREN, RocketBuzzer::VOL_MAX);
  }
}

void runSequencer() {
  if (renderAction(events[evActive].act[evStep])) {
    if (++evStep >= events[evActive].nAct) {
      logln("EVENT '%s' done", events[evActive].name);
      evActive = -1; scrollX = matrix.width();
    } else enterStep();
  }
}

void evalTriggers() {
  if (evActive >= 0 || millis() - lastTrigTick < 3000) return;
  lastTrigTick = millis();
  tempVal = readTempC();
  // Keep AQI fresh for threshold/delta events (every 30 min).
  bool needAqi = false;
  for (int i = 0; i < nEvents; i++)
    if (events[i].en && events[i].src == 1 && (events[i].trig == T_THR || events[i].trig == T_DELTA)) needAqi = true;
  if (needAqi && (lastAqiSample == 0 || millis() - lastAqiSample > 30UL * 60 * 1000)) { lastAqiSample = millis(); fetchAir(); }

  time_t now = timeReady() ? time(nullptr) : 0;
  struct tm *t = now ? localtime(&now) : nullptr;
  for (int i = 0; i < nEvents; i++) {
    Event &e = events[i]; if (!e.en) continue;
    bool fire = false;
    if (e.trig == T_AT && t) {
      if (t->tm_hour == e.h && t->tm_min == e.m && evLastMin[i] != t->tm_min) { evLastMin[i] = t->tm_min; fire = true; }
    } else if (e.trig == T_RANGE && t) {
      int c = t->tm_hour * 60 + t->tm_min, a = e.h * 60 + e.m, b = e.eh * 60 + e.em;
      if (a <= b ? (c >= a && c < b) : (c >= a || c < b)) fire = true;
    } else if (e.trig == T_THR) {
      float v = srcValue(e.src);
      if (!isnan(v)) { bool cond = e.above ? v > e.x : v < e.x; if (cond && !evPrevCond[i]) fire = true; evPrevCond[i] = cond; }
    } else if (e.trig == T_DELTA) {
      float v = srcValue(e.src);
      if (!isnan(v)) { if (evValInit[i] && fabs(v - evLastVal[i]) >= e.x) fire = true; evLastVal[i] = v; evValInit[i] = true; }
    }
    if (fire) { evActive = i; evStep = 0; logln("EVENT '%s' start", e.name); enterStep(); break; }
  }
}

void handleEventsGet() {
  JsonDocument d; eventsToJson(d["events"].to<JsonArray>());
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}
void handleEventsPost() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) { server.send(400, "application/json", "{\"err\":\"bad json\"}"); return; }
  eventsFromJson(d["events"].as<JsonArray>());
  markDirty(); logln("EVENTS updated (%u)", nEvents);
  handleEventsGet();
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

void playTimerSound() {
  if (cfg.timerSound == 1)      buzzer.playMelody(RocketMelodies::HEDWIG, RocketBuzzer::VOL_MAX);
  else if (cfg.timerSound == 2) buzzer.playMelody(RocketMelodies::CLOSE_ENCOUNTERS, RocketBuzzer::VOL_MAX);
  else                          buzzer.playMelody(RocketMelodies::SIREN, RocketBuzzer::VOL_MAX);
}

// Map a linear fill index -> canvas (x,y) in the configured propagation order.
// setPixel() then maps (x,y) to the right physical panel, so this is purely the
// visual order the fill sweeps the whole COLS*8 x ROWS*8 canvas.
void fillCoord(int idx, int W, int H, int &x, int &y) {
  switch (cfg.fillOrder) {
    default:
    case 0: x = idx % W; y = idx / W; break;                               // row-major
    case 1: y = idx % H; x = idx / H; break;                               // column-major
    case 2: { int r = idx / W, c = idx % W; y = r; x = (r & 1) ? W-1-c : c; } break;  // snake rows
    case 3: { int c = idx / H, r = idx % H; x = c; y = (c & 1) ? H-1-r : r; } break;  // snake cols
    case 4: { int p = idx / 64, l = idx % 64;                              // panel-by-panel (chain order)
              x = (p % cfg.cols) * 8 + l % 8; y = (p / cfg.cols) * 8 + l / 8; } break;
  }
}

// Start: clear/fill the whole display up front so the sweep is visible.
// forward = start empty then light; reverse = start full then extinguish.
void startTimer() {
  timerStart = millis(); timerLast = -1; timerDone = false;
  matrix.fill(cfg.timerReverse);        // reverse -> full, forward -> empty
}

void runTimer() {
  int total = matrix.width() * matrix.height();   // all LEDs across all panels
  unsigned long span = cfg.timerSecs * 1000UL;
  unsigned long elapsed = millis() - timerStart;
  if (elapsed >= span) {
    if (!timerDone) {                    // one-shot finale, then hold (non-blocking)
      matrix.fill(!cfg.timerReverse);    // forward -> all on, reverse -> all off
      timerDone = true;
      logln("TIMER done");
      playTimerSound();                  // plays once; switching mode exits/stops
    }
    return;
  }
  int lit = (int)((uint64_t)elapsed * total / span);   // 0..total
  if (lit == timerLast) return;
  for (int i = timerLast + 1; i <= lit && i < total; i++) {
    int x, y; fillCoord(i, matrix.width(), matrix.height(), x, y);
    matrix.setPixel(x, y, !cfg.timerReverse);          // forward on, reverse off
  }
  timerLast = lit;
}

// ---- API -------------------------------------------------------------------
void sendStatus() {
  const char *m = cfg.mode == MODE_TEXT ? "text" : cfg.mode == MODE_TIMER ? "timer"
                : cfg.mode == MODE_TEMP ? "temp" : cfg.mode == MODE_CLOCK ? "clock"
                : cfg.mode == MODE_WEATHER ? "weather" : cfg.mode == MODE_LAUNCH ? "launch"
                : cfg.mode == MODE_MARS ? "mars" : cfg.mode == MODE_AQI ? "air"
                : cfg.mode == MODE_NEWS ? "news" : "off";
  char buf[680];
  snprintf(buf, sizeof(buf),
    "{\"mode\":\"%s\",\"brightness\":%u,\"scrollMs\":%u,\"text\":\"%s\",\"timerSecs\":%lu,"
    "\"cols\":%u,\"rows\":%u,\"serpentine\":%s,\"flip\":%s,\"flip180\":%s,"
    "\"tRev\":%s,\"tSnd\":%u,\"fill\":%u,"
    "\"alarm\":\"%02u:%02u\",\"alarmOn\":%s,\"city\":\"%s\",\"time\":\"%s\",\"launch\":\"%s\"}",
    m, cfg.brightness, cfg.scrollMs, cfg.text, (unsigned long)cfg.timerSecs,
    cfg.cols, cfg.rows, cfg.serpentine ? "true" : "false", cfg.flip ? "true" : "false",
    cfg.flip180 ? "true" : "false", cfg.timerReverse ? "true" : "false", cfg.timerSound, cfg.fillOrder,
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
  else if (m == "news")    { cfg.mode = MODE_NEWS; lastNews = 0; }
  else if (m == "off")  { cfg.mode = MODE_OFF; matrix.clear(); }
  scrollX = matrix.width();
  logln("MODE -> %s", m.c_str());
  markDirty();
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
  markDirty();
  sendStatus();
}

void handleWeather() {                       // set city + switch to weather mode
  if (server.hasArg("city")) server.arg("city").toCharArray(cfg.city, sizeof(cfg.city));
  cfg.mode = MODE_WEATHER; lastWeather = 0; scrollX = matrix.width();
  markDirty();
  sendStatus();
}

void handlePanels() {
  if (server.hasArg("cols")) cfg.cols = constrain(server.arg("cols").toInt(), 1, 16);
  if (server.hasArg("rows")) cfg.rows = constrain(server.arg("rows").toInt(), 1, 16);
  if (cfg.cols * cfg.rows > RocketMatrix::MAX_PANELS) cfg.rows = RocketMatrix::MAX_PANELS / cfg.cols;
  if (server.hasArg("serpentine")) cfg.serpentine = server.arg("serpentine") == "1";
  if (server.hasArg("flip"))       cfg.flip       = server.arg("flip") == "1";
  if (server.hasArg("flip180"))    cfg.flip180    = server.arg("flip180") == "1";
  applyLayout();
  logln("PANELS %ux%u serp=%d flip=%d rot180=%d", cfg.cols, cfg.rows, cfg.serpentine, cfg.flip, cfg.flip180);
  markDirty();
  sendStatus();
}

void handleConfig() {
  if (server.hasArg("brightness")) {
    cfg.brightness = constrain(server.arg("brightness").toInt(), 0, 15);
    matrix.setBrightness(cfg.brightness);
  }
  if (server.hasArg("scrollMs"))
    cfg.scrollMs = constrain(server.arg("scrollMs").toInt(), 5, 1000);
  markDirty();
  sendStatus();
}

void handleText() {
  if (server.hasArg("message")) {
    server.arg("message").toCharArray(cfg.text, sizeof(cfg.text));
    scrollX = 8;
  }
  cfg.mode = MODE_TEXT;
  markDirty();
  sendStatus();
}

void handleTimer() {
  if (server.hasArg("seconds")) cfg.timerSecs = max(1L, server.arg("seconds").toInt());
  if (server.hasArg("reverse")) cfg.timerReverse = server.arg("reverse") == "1";
  if (server.hasArg("sound"))   cfg.timerSound = constrain(server.arg("sound").toInt(), 0, 2);
  if (server.hasArg("fill"))    cfg.fillOrder = constrain(server.arg("fill").toInt(), 0, 4);
  cfg.mode = MODE_TIMER;
  startTimer();
  markDirty();
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

// Stream log lines newer than ?since=<seq>. Returns {seq, lines:[...]}.
void handleLogs() {
  uint32_t since = strtoul(server.arg("since").c_str(), nullptr, 10);
  uint32_t start = (since > logSeq) ? logSeq : since;               // clamp
  if (logSeq - start > LOG_LINES) start = logSeq - LOG_LINES;       // ring cap
  String out; out.reserve(2800);
  out = "{\"seq\":" + String(logSeq) + ",\"lines\":[";
  for (uint32_t s = start; s < logSeq; s++) {
    if (s != start) out += ',';
    out += '"';
    for (const char *p = logRing[s % LOG_LINES]; *p; p++) {
      if (*p == '"' || *p == '\\') out += '\\';
      out += *p;
    }
    out += '"';
  }
  out += "]}";
  server.send(200, "application/json", out);
}

// Dashboard SPA (served from PROGMEM).
const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RocketClock Mission Control</title><style>
:root{--bg:#05080d;--panel:#0b121c;--edge:#1c2a3a;--grn:#39ff9e;--amb:#ffb347;--red:#ff5252;--txt:#cfe3f0;--dim:#5a7089}
*{box-sizing:border-box}
body{font-family:ui-monospace,'SF Mono',Menlo,Consolas,monospace;margin:0;color:var(--txt);min-height:100vh;background:radial-gradient(circle at 50% -10%,#0a1420,#05080d)}
header{display:flex;align-items:center;justify-content:space-between;padding:.7rem 1rem;border-bottom:1px solid var(--edge);background:#070c14}
header h1{font-size:.95rem;letter-spacing:.18em;margin:0;color:var(--grn);text-transform:uppercase}
#conn{font-size:.72rem;letter-spacing:.12em;display:flex;align-items:center;gap:.45rem;color:var(--red)}
#dot{width:10px;height:10px;border-radius:50%;background:var(--red);box-shadow:0 0 8px var(--red)}
#conn.ok{color:var(--grn)}#conn.ok #dot{background:var(--grn);box-shadow:0 0 9px var(--grn);animation:pulse 1.6s infinite}
@keyframes pulse{50%{opacity:.35}}
nav{display:flex;border-bottom:1px solid var(--edge);background:#070c14}
nav button{flex:1;background:none;border:0;color:var(--dim);padding:.65rem;font:inherit;letter-spacing:.12em;cursor:pointer;border-bottom:2px solid transparent}
nav button.on{color:var(--grn);border-bottom-color:var(--grn)}
main{max-width:660px;margin:0 auto;padding:.6rem 1rem 2rem}
.panel{border:1px solid var(--edge);border-radius:6px;margin:1rem 0;background:var(--panel);position:relative}
.panel>.lbl{position:absolute;top:-.55rem;left:.7rem;background:var(--panel);padding:0 .4rem;font-size:.62rem;letter-spacing:.16em;color:var(--amb)}
.panel>.body{padding:.9rem .8rem .75rem}
.row{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center}
button.act{background:#10202e;border:1px solid var(--edge);color:var(--txt);border-radius:4px;padding:.5rem .7rem;font:inherit;cursor:pointer}
button.act:hover{border-color:var(--grn);color:var(--grn)}
button.act.active{background:#0d3a2a;border-color:var(--grn);color:var(--grn);box-shadow:0 0 6px rgba(57,255,158,.4)}
input,select{background:#060b12;border:1px solid var(--edge);color:var(--txt);border-radius:4px;padding:.45rem;font:inherit}
input[type=range]{width:100%;accent-color:var(--grn);padding:0}
label{display:block;margin:.5rem 0 .2rem;font-size:.72rem;color:var(--dim);letter-spacing:.05em}
#met{font-size:.78rem;color:var(--dim);line-height:1.7}#met b{color:var(--grn)}
#console{background:#02050a;border:1px solid var(--edge);border-radius:6px;height:62vh;overflow:auto;padding:.6rem;font-size:.72rem;line-height:1.55;color:var(--grn);white-space:pre-wrap;word-break:break-word}
.hidden{display:none}
</style></head><body>
<header><h1>&#9650; RocketClock &middot; Mission Control</h1>
<div id="conn"><span id="dot"></span><span id="cst">OFFLINE</span></div></header>
<nav><button class="on" data-tab="control">CONTROL</button><button data-tab="events">EVENTS</button><button data-tab="telemetry">TELEMETRY</button></nav>
<main>
<section id="control">
 <div class="panel"><span class="lbl">STATUS</span><div class="body"><div id="met">awaiting telemetry&hellip;</div></div></div>
 <div class="panel"><span class="lbl">MODE</span><div class="body"><div class="row" id="modes">
  <button class="act" data-mode="text" onclick="mode('text')">TEXT</button>
  <button class="act" data-mode="timer" onclick="mode('timer')">TIMER</button>
  <button class="act" data-mode="clock" onclick="mode('clock')">CLOCK</button>
  <button class="act" data-mode="temp" onclick="mode('temp')">TEMP</button>
  <button class="act" data-mode="weather" onclick="mode('weather')">WEATHER</button>
  <button class="act" data-mode="launch" onclick="mode('launch')">&#128640; LAUNCH</button>
  <button class="act" data-mode="mars" onclick="mode('mars')">&#128308; MARS</button>
  <button class="act" data-mode="air" onclick="mode('air')">&#127787; AIR</button>
  <button class="act" data-mode="news" onclick="mode('news')">&#128240; NEWS</button>
  <button class="act" data-mode="off" onclick="mode('off')">OFF</button></div></div></div>
 <div class="panel"><span class="lbl">SCROLLING TEXT</span><div class="body row">
  <input id="msg" value="ROCKETCLOCK" style="flex:1"><button class="act" onclick="setText()">SET</button></div></div>
 <div class="panel"><span class="lbl">TIMER</span><div class="body">
  <div class="row"><input id="mins" type="number" value="20" step="0.5" style="width:5rem"><span style="color:var(--dim)">min</span>
   <select id="tdir"><option value="0">FILL ON</option><option value="1">EMPTY OFF</option></select>
   <button class="act" onclick="setTimer()">START</button></div>
  <label>FILL ORDER</label><select id="tfill"><option value="0">Rows</option><option value="1">Columns</option><option value="2">Snake rows</option><option value="3">Snake cols</option><option value="4">Panel by panel</option></select>
  <label>END SOUND</label><select id="tsnd"><option value="0">Siren</option><option value="1">Hedwig</option><option value="2">Close Encounters</option></select></div></div>
 <div class="panel"><span class="lbl">DISPLAY</span><div class="body">
  <label>BRIGHTNESS <span id="bv">5</span></label><input id="br" type="range" min="0" max="15" value="5" oninput="bv.textContent=this.value" onchange="cfg()">
  <label>SCROLL SPEED <span id="sv">60</span> ms</label><input id="sp" type="range" min="5" max="300" value="60" oninput="sv.textContent=this.value" onchange="cfg()"></div></div>
 <div class="panel"><span class="lbl">ALARM</span><div class="body row">
  <input id="atime" type="time" value="07:30"><label style="margin:0"><input type="checkbox" id="aon"> ARM</label>
  <button class="act" onclick="setAlarm()">SET</button></div></div>
 <div class="panel"><span class="lbl">WEATHER / AIR LOCATION</span><div class="body row">
  <input id="city" placeholder="city (blank = auto by IP)" style="flex:1"><button class="act" onclick="setWeather()">SHOW</button></div></div>
 <div class="panel"><span class="lbl">BUZZER</span><div class="body row">
  <button class="act" onclick="buzz('hedwig')">HEDWIG</button><button class="act" onclick="buzz('close_encounters')">CLOSE ENCOUNTERS</button></div></div>
 <div class="panel"><span class="lbl">PANEL ARRAY</span><div class="body">
  <div class="row"><label style="margin:0">COLS <input id="cols" type="number" min="1" max="16" value="1" style="width:4rem"></label>
  <label style="margin:0">ROWS <input id="rows" type="number" min="1" max="16" value="1" style="width:4rem"></label></div>
  <label style="margin-top:.5rem"><input type="checkbox" id="snake" checked> SERPENTINE (SNAKE WIRING)</label>
  <label><input type="checkbox" id="flip" checked> FLIP REVERSE ROWS 180&deg;</label>
  <label><input type="checkbox" id="rot180"> ROTATE WHOLE DISPLAY 180&deg; (USB DOWN)</label>
  <button class="act" onclick="setPanels()">APPLY</button></div></div>
</section>
<section id="events" class="hidden">
 <div class="panel"><span class="lbl">EVENTS</span><div class="body"><div id="evlist">none</div></div></div>
 <div class="panel"><span class="lbl">NEW EVENT</span><div class="body">
  <label>NAME</label><input id="evname" value="Morning" style="width:60%">
  <label>TRIGGER</label>
  <select id="evtrig" onchange="trigUI()"><option value="0">At time</option><option value="1">Time range</option><option value="2">Threshold</option><option value="3">Delta (change by)</option></select>
  <div id="trigAt" class="row" style="margin-top:.4rem">TIME <input id="tgh" type="number" min="0" max="23" value="9" style="width:3.5rem">:<input id="tgm" type="number" min="0" max="59" value="0" style="width:3.5rem"></div>
  <div id="trigEnd" class="row hidden">to <input id="tgeh" type="number" min="0" max="23" value="17" style="width:3.5rem">:<input id="tgem" type="number" min="0" max="59" value="0" style="width:3.5rem"></div>
  <div id="trigThr" class="row hidden"><select id="tgsrc"><option value="0">Temp &deg;C</option><option value="1">AQI</option></select>
   <select id="tgop"><option value="1">above</option><option value="0">below</option></select>
   <input id="tgx" type="number" value="28" style="width:4.5rem"></div>
  <label>ACTIONS</label><div id="acts"></div>
  <div class="row" style="margin-top:.4rem"><button class="act" onclick="addAct()">+ ACTION</button><button class="act" onclick="saveEvent()">SAVE EVENT</button></div>
 </div></div>
</section>
<section id="telemetry" class="hidden">
 <div class="panel"><span class="lbl">SERIAL TELEMETRY &middot; LIVE</span><div class="body"><div id="console"></div></div></div>
</section></main>
<script>
const $=id=>document.getElementById(id);
function setConn(ok){$('conn').className=ok?'ok':'';$('cst').textContent=ok?'CONNECTED':'OFFLINE';}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
 document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('on',x===b));
 ['control','events','telemetry'].forEach(id=>$(id).classList.toggle('hidden',b.dataset.tab!==id));
 if(b.dataset.tab==='events')loadEvents();});
let formInit=false;
const show=s=>{setConn(true);
 $('met').innerHTML='MODE <b>'+(s.mode||'?').toUpperCase()+'</b> &middot; CLK <b>'+(s.time||'--:--')+'</b> &middot; ARRAY <b>'+s.cols+'&times;'+s.rows+'</b> &middot; BRT <b>'+s.brightness+'</b>';
 document.querySelectorAll('#modes button').forEach(b=>b.classList.toggle('active',b.dataset.mode===s.mode));
 if(!formInit){formInit=true;  // populate inputs once so polls don't clobber typing
  if(s.cols){cols.value=s.cols;rows.value=s.rows;snake.checked=s.serpentine;flip.checked=s.flip;rot180.checked=s.flip180;}
  if(s.alarm){atime.value=s.alarm;aon.checked=s.alarmOn;}if(s.city!==undefined)city.value=s.city;
  if(s.brightness!==undefined){br.value=s.brightness;bv.textContent=s.brightness;}
  if(s.scrollMs){sp.value=s.scrollMs;sv.textContent=s.scrollMs;}
  if(s.timerSecs)mins.value=(s.timerSecs/60);
  if(s.tRev!==undefined)tdir.value=s.tRev?1:0;if(s.fill!==undefined)tfill.value=s.fill;if(s.tSnd!==undefined)tsnd.value=s.tSnd;}};
const req=(p,o,post)=>fetch(p+(o?'?'+new URLSearchParams(o):''),post?{method:'POST'}:{}).then(r=>r.json());
const q=(p,o)=>req(p,o,true).then(show).catch(()=>setConn(false));
const mode=m=>q('/api/mode',{mode:m});
const setText=()=>q('/api/text',{message:msg.value});
const setTimer=()=>q('/api/timer',{seconds:Math.max(1,Math.round(mins.value*60)),reverse:tdir.value,fill:tfill.value,sound:tsnd.value});
const setPanels=()=>q('/api/panels',{cols:cols.value,rows:rows.value,serpentine:snake.checked?1:0,flip:flip.checked?1:0,flip180:rot180.checked?1:0});
const setAlarm=()=>q('/api/alarm',{time:atime.value,enabled:aon.checked?1:0});
const setWeather=()=>q('/api/weather',{city:city.value});
const cfg=()=>q('/api/config',{brightness:br.value,scrollMs:sp.value});
const buzz=t=>fetch('/api/buzzer?tune='+t,{method:'POST'}).then(()=>setConn(true)).catch(()=>setConn(false));
function poll(){req('/api/status').then(show).catch(()=>setConn(false));}
setInterval(poll,2000);poll();
let logSeq=0;
function logs(){req('/api/logs',{since:logSeq}).then(d=>{setConn(true);logSeq=d.seq;
 if(d.lines&&d.lines.length){const c=$('console');d.lines.forEach(l=>c.textContent+=l+'\n');
  if(c.textContent.length>9000)c.textContent=c.textContent.slice(-9000);c.scrollTop=c.scrollHeight;}
 }).catch(()=>setConn(false));}
setInterval(logs,1500);logs();
// ---- events builder ----
let EV=[],NA=[];const ATN=['Text','Weather','News','Mars','Air','Clock','Temp','Buzzer'];
const p2=n=>String(n).padStart(2,'0');
function trigUI(){const v=+evtrig.value;$('trigAt').classList.toggle('hidden',v>1);$('trigEnd').classList.toggle('hidden',v!=1);$('trigThr').classList.toggle('hidden',v<2);}
function trigDesc(e){return [`@${p2(e.h)}:${p2(e.m)}`,`${p2(e.h)}:${p2(e.m)}-${p2(e.eh)}:${p2(e.em)}`,`${e.src?'AQI':'temp'}${e.above?'>':'<'}${e.x}`,`${e.src?'AQI':'temp'} Δ${e.x}`][e.trig];}
function loadEvents(){req('/api/events').then(d=>{EV=d.events||[];renderEvents();setConn(true);}).catch(()=>setConn(false));}
function renderEvents(){const l=$('evlist');l.innerHTML=EV.length?'':'none';EV.forEach((e,i)=>{const d=document.createElement('div');d.style.margin='.35rem 0';d.innerHTML=`<button class=act onclick=delEv(${i})>&#10005;</button> <b>${e.name}</b> — ${trigDesc(e)} → ${(e.act||[]).length} action(s)`;l.appendChild(d);});}
function addAct(){NA.push({t:0,p:0,v:3,s:false,x:'HELLO'});renderActs();}
function renderActs(){const w=$('acts');w.innerHTML='';NA.forEach((a,i)=>{const d=document.createElement('div');d.className='row';d.style.margin='.2rem 0';
 d.innerHTML=`<select onchange="NA[${i}].t=+this.value;renderActs()">${ATN.map((n,j)=>`<option value=${j} ${a.t==j?'selected':''}>${n}</option>`).join('')}</select>`
 +(a.t==0?`<input value="${a.x}" oninput="NA[${i}].x=this.value" style="flex:1">`:'')
 +(a.t==7?`<select onchange="NA[${i}].p=+this.value">${['Siren','Hedwig','CloseEnc'].map((n,j)=>`<option value=${j} ${a.p==j?'selected':''}>${n}</option>`).join('')}</select>`:'')
 +`<input type=number value=${a.v} style="width:3.5rem" oninput="NA[${i}].v=+this.value"><select onchange="NA[${i}].s=this.value=='1'"><option value=0 ${!a.s?'selected':''}>cyc</option><option value=1 ${a.s?'selected':''}>sec</option></select><button class=act onclick="NA.splice(${i},1);renderActs()">&#10005;</button>`;
 w.appendChild(d);});}
function saveEvent(){const v=+evtrig.value;EV.push({name:evname.value||'event',en:true,trig:v,h:+tgh.value,m:+tgm.value,eh:+tgeh.value,em:+tgem.value,src:+tgsrc.value,above:tgop.value=='1',x:+tgx.value,act:NA.map(a=>({t:a.t,p:a.p,v:a.v,s:a.s,x:a.x}))});NA=[];renderActs();saveEvents();}
function delEv(i){EV.splice(i,1);saveEvents();}
function saveEvents(){fetch('/api/events',{method:'POST',body:JSON.stringify({events:EV})}).then(r=>r.json()).then(d=>{EV=d.events||[];renderEvents();setConn(true);}).catch(()=>setConn(false));}
</script></body></html>)HTML";

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void setup() {
  Serial.begin(115200);
  delay(150);
  logln("BOOT RocketClock server");
  bool fs = LittleFS.begin();
  if (!fs) { LittleFS.format(); fs = LittleFS.begin(); }   // first boot: format FS
  logln("FS %s", fs ? "mounted" : "UNAVAILABLE");
  if (fs) loadConfig();
  matrix.setLayout(cfg.cols, cfg.rows, cfg.serpentine, cfg.flip);
  matrix.setFlip180(cfg.flip180);
  matrix.begin(cfg.brightness);
  buzzer.begin();               // Wire.begin() (SDA=4, SCL=5)
  logln("MATRIX %ux%u  BUZZER %s", cfg.cols, cfg.rows, buzzer.present() ? "OK" : "absent");

  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASS);
  logln("WIFI connecting to %s", STA_SSID);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    logln("WIFI up  IP %s", WiFi.localIP().toString().c_str());
    configTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");   // NTP for clock/alarm
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    logln("WIFI failed - SoftAP %s  IP %s", AP_SSID, WiFi.softAPIP().toString().c_str());
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
  server.on("/api/events", HTTP_GET,  handleEventsGet);
  server.on("/api/events", HTTP_POST, handleEventsPost);
  server.on("/api/temp",   handleTemp);
  server.on("/api/logs",   handleLogs);
  server.begin();
  logln("HTTP server started");
}

void handleStatus() { sendStatus(); }

void loop() {
  server.handleClient();
  checkAlarm();                       // fires regardless of active mode
  if (cfgDirty && millis() - cfgTouched > 2500) { cfgDirty = false; saveConfig(); }

  evalTriggers();                     // event triggers (time / threshold / delta)
  if (evActive >= 0) { runSequencer(); return; }   // an event sequence takes over

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
    case MODE_NEWS:
      if (lastNews == 0 || millis() - lastNews > 10UL * 60 * 1000) { lastNews = millis(); fetchNews(); }
      scrollText(newsStr);
      break;
    case MODE_OFF: break;
  }
}

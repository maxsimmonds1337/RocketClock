// RocketClock air-quality trimming proxy (Cloudflare Worker).
//
// Open-Meteo's air-quality API is free/no-key, but a DIRECT HTTPS call from the
// ESP8266 is unreliable (its TLS handshake needs more heap than the ESP can
// spare once persistence is loaded). Routing through Cloudflare - whose TLS is
// ESP-friendly and which we already use for mars/launch - fixes that, and trims
// the response to {aqi, pm25, pm10}. Location is Tallinn; edit LAT/LON below.
//
// Deploy: npx wrangler deploy air-worker.js --name rocketclock-air \
//           --compatibility-date 2024-11-01
// Then set AIR_PROXY_URL in secrets.h.

const LAT = "59.437", LON = "24.754";
const SRC = `https://air-quality-api.open-meteo.com/v1/air-quality?latitude=${LAT}&longitude=${LON}&current=european_aqi,pm2_5,pm10`;

export default {
  async fetch() {
    try {
      const r = await fetch(SRC, { cf: { cacheTtl: 1800, cacheEverything: true } });
      const c = (await r.json()).current || {};
      return json({ aqi: c.european_aqi ?? null, pm25: c.pm2_5 ?? null, pm10: c.pm10 ?? null });
    } catch (e) {
      return json({ error: String(e) }, 502);
    }
  },
};

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: {
      "content-type": "application/json",
      "cache-control": "public, max-age=1800",
      "access-control-allow-origin": "*",
    },
  });
}

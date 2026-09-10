// RocketClock Mars-weather trimming proxy (Cloudflare Worker).
//
// Why: NASA's Curiosity (MSL) REMS weather feed is live and accurate, but it
// ships ALL sols (~1.7 MB) - far too big for the ESP8266. This Worker fetches
// it, keeps only the latest sol, and returns ~150 bytes the board can pull.
//
// Deploy (2 min, free):
//   1. dash.cloudflare.com -> Workers & Pages -> Create -> paste this in.
//   2. Deploy. You get a URL like https://mars-weather.<you>.workers.dev
//   3. Put that URL in the firmware (secrets.h: MARS_PROXY_URL) and RocketClock
//      will show live Curiosity temps in Mars mode.
//
// Cloudflare caches the big upstream fetch for an hour, so NASA is hit at most
// once/hour no matter how often the board polls.

const SRC =
  "https://mars.nasa.gov/rss/api/?feed=weather&category=msl&feedtype=json&ver=1.0";

export default {
  async fetch() {
    try {
      const r = await fetch(SRC, { cf: { cacheTtl: 3600, cacheEverything: true } });
      const d = await r.json();
      const s = (d.soles || d.sols || [])[0] || {};
      const out = {
        sol: s.sol ?? null,
        date: s.terrestrial_date ?? null,
        min_c: s.min_temp ?? null,
        max_c: s.max_temp ?? null,
        pressure_pa: s.pressure ?? null,
        sunrise: s.sunrise ?? null,
        sunset: s.sunset ?? null,
        sky: s.atmo_opacity ?? null,
        season: s.season ?? null,
      };
      return json(out);
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
      "cache-control": "public, max-age=3600",
      "access-control-allow-origin": "*",
    },
  });
}

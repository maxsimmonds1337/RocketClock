// RocketClock launch trimming proxy (Cloudflare Worker).
//
// Why: Launch Library 2 is HTTPS-only and rate-limited (~15 req/hour anon), and
// even in list mode the response is a few KB. This Worker fetches the next
// launch, returns ~120 bytes ({name, net, pad, status}), and Cloudflare caches
// the upstream for 15 min - so the board can poll freely and stays well under
// LL2's limit. Smaller payload also eases BearSSL heap use on the ESP8266.
//
// Deploy: dash.cloudflare.com -> Workers -> paste -> Deploy. Then set
//   secrets.h: #define LAUNCH_PROXY_URL "https://launch.<you>.workers.dev"
// The firmware falls back to hitting LL2 directly if this is left blank.
//
// Keys match the firmware parser (name, net) so no code change is needed.

// Use the DEV endpoint: LL2's main API rate-limits per-IP (15/hour), and
// Cloudflare Workers share egress IPs, so main gets throttled. lldev has
// relaxed limits with ~1h-cached data - fine here, since the on-device
// countdown is driven by `net`, not by second-to-second freshness.
const SRC =
  "https://lldev.thespacedevs.com/2.2.0/launch/upcoming/?limit=1&hide_recent_previous=true&mode=list";

export default {
  async fetch() {
    try {
      const r = await fetch(SRC, {
        cf: { cacheTtl: 900, cacheEverything: true },
        headers: { "User-Agent": "RocketClock" },
      });
      const d = await r.json();
      const l = (d.results || [])[0] || {};
      return json({
        name: l.name ?? null,
        net: l.net ?? null,               // ISO-8601 UTC, e.g. 2026-09-13T18:49:00Z
        pad: l.pad ?? null,
        status: (l.status && l.status.abbrev) ?? null,
      });
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
      "cache-control": "public, max-age=900",
      "access-control-allow-origin": "*",
    },
  });
}

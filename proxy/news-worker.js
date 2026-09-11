// RocketClock news trimming proxy (Cloudflare Worker).
//
// BBC News RSS is ~26 KB of XML - far too big for the ESP8266. This Worker
// fetches it, extracts the top headline, and returns ~80 bytes {headline}.
// Cloudflare caches the upstream 10 min. Set NEWS_PROXY_URL in secrets.h.
//
// (A future "breaking news" trigger can diff this headline between polls.)

const SRC = "https://feeds.bbci.co.uk/news/rss.xml";

export default {
  async fetch() {
    try {
      const r = await fetch(SRC, { headers: { "User-Agent": "RocketClock/1.0" }, redirect: "follow" });
      const xml = await r.text();
      const item = xml.split("<item>")[1] || "";
      const m = item.match(/<title>(?:<!\[CDATA\[)?([\s\S]*?)(?:\]\]>)?<\/title>/);
      let h = m ? m[1] : "NO NEWS";
      h = h.replace(/&amp;/g, "&").replace(/&#39;/g, "'")
           .replace(/&quot;/g, '"').replace(/&lt;/g, "<").replace(/&gt;/g, ">").trim();
      return json({ headline: h });
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
      "cache-control": "public, max-age=600",
      "access-control-allow-origin": "*",
    },
  });
}

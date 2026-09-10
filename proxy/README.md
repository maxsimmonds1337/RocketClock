# RocketClock proxies

Tiny "trimming proxies" that fetch a big/awkward upstream feed and return a
small JSON the ESP8266 can actually consume. See `docs/CONTROL_SYSTEM.md` §5.

## mars-weather-worker.js — live Curiosity temperatures

NASA's MSL REMS feed is live and accurate but ~1.7 MB (every sol). This trims it
to the latest sol (~150 bytes: temp, pressure, sunrise/sunset, sky, season).

**Deploy options (pick one):**

### A. Cloudflare Worker (recommended — free, fast, HTTPS)
1. https://dash.cloudflare.com → Workers & Pages → Create Worker → paste
   `mars-weather-worker.js` → Deploy.
2. Copy the `*.workers.dev` URL.
3. Add to firmware `secrets.h`: `#define MARS_PROXY_URL "https://.../"`.

Test:
```
curl https://mars-weather.<you>.workers.dev
```

### B. GitHub Action → static JSON (zero always-on infra)
A scheduled job (e.g. every 6 h) curls the feed, extracts the latest sol with
`jq`/python, and commits `mars.json` to a repo served over HTTPS (GitHub Pages
or raw). The board pulls the static file. Ask and I'll add the workflow yaml.

## Notes
- Same pattern can front the rocket-launch (LL2) feed to sidestep BearSSL RAM on
  the ESP — return a trimmed `{name, net}` over the proxy instead of raw HTTPS.

## launch-worker.js — trimmed next rocket launch

Fronts Launch Library 2: returns `{name, net, pad, status}` (~120 B) and caches
the upstream 15 min (LL2 is rate-limited ~15/hour). Keys match the firmware
parser. Set `LAUNCH_PROXY_URL` in secrets.h; firmware falls back to direct LL2
if blank.

## Deploy via CLI (wrangler)

No install needed (uses npx). One command per worker, no wrangler.toml:

```sh
# 1. Authenticate once (opens a browser on this Mac):
npx wrangler login

# 2. Deploy both:
npx -y wrangler deploy mars-weather-worker.js \
    --name rocketclock-mars   --compatibility-date 2024-11-01
npx -y wrangler deploy launch-worker.js \
    --name rocketclock-launch --compatibility-date 2024-11-01
```

Each prints a `https://<name>.<your-subdomain>.workers.dev` URL → put those in
`secrets.h` (`MARS_PROXY_URL`, `LAUNCH_PROXY_URL`) and reflash.

Alternative auth for headless/CI: `CLOUDFLARE_API_TOKEN` (scope *Workers
Scripts:Edit*) as an env var instead of `wrangler login`.

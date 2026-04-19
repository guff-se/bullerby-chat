# Bullerby Chat — Cloudflare Worker

Ephemeral voice relay: **HTTP** upload, **WebSocket** push, **short-lived** audio fetch, **git-bundled** config (`config/bullerby.json`). No long-term mailbox — see `docs/project-plan.md`.

## What you need

1. **Cloudflare** account (free tier: use `new_sqlite_classes` migration — already in `wrangler.toml`).
2. **Wrangler** authenticated: `npx wrangler login` (OAuth) once per machine.
3. **Node.js** 18+ and npm.
4. **Shared device secret** (required for all authenticated API calls and for signing download URLs):

```bash
npx wrangler secret put BULLERBY_DEVICE_SECRET
```

Use a long random string (e.g. `openssl rand -hex 32`). Every device sends it as `Authorization: Bearer <same-secret>`. **Cloudflare never shows the value again** after you set it; store it in a password manager or in **`server/.dev.vars`** (gitignored) for local use.

**Non-interactive / CI:**

```bash
printf '%s' "$BULLERBY_DEVICE_SECRET" | npx wrangler secret put BULLERBY_DEVICE_SECRET
```

After updating the secret in the dashboard, wait a few seconds before calling authenticated routes (propagation).

**Local dev:** copy `.dev.vars.example` to **`.dev.vars`** and set `BULLERBY_DEVICE_SECRET=...`. Wrangler loads `.dev.vars` for `wrangler dev` only; it does not deploy that file.

## Config

Edit **`config/bullerby.json`** (families + one device per family), commit, then `npm run deploy`. Devices see updates on the next `GET /api/devices/{id}/config` or after a WebSocket reconnect (you can add `config_updated` broadcasts later). Each **`devices[].id`** must match the firmware **`X-Device-Id`** (Kconfig / NVS, or `esp-xxxxxxxxxxxx` when **Bullerby Chat → Derive device id from chip WiFi MAC** is enabled in menuconfig).

## Testing (required before deploy)

Automated tests use **Vitest** + **`@cloudflare/vitest-pool-workers`** (runs against `workerd` with your `wrangler.toml`).

```bash
cd server
npm install
npm test              # run once (CI / pre-deploy)
npm run test:watch    # during development
```

- **`test/auth.test.ts`** — Bearer allowlist, HMAC download URL roundtrip.
- **`test/worker.test.ts`** — HTTP integration: `/health`, `/api/devices/.../config`, `/api/devices/register`.

**`npm run deploy`** runs **`npm test` first** (`predeploy`). To deploy without tests (emergency only), use `npx wrangler deploy` directly — do not make that the default workflow.

Test-only secret: **`test/constants.ts`** (injected via `vitest.config.mts`); production uses **`BULLERBY_DEVICE_SECRET`** from Wrangler.

### End-to-end (WebSocket + upload + signed download)

**`scripts/e2e-full.mjs`** exercises the full relay path: **`wss://…/api/ws`** as **`device-uuid-002`** (with `Authorization` + `X-Device-Id`), **`POST /api/messages`** as **`device-uuid-001`** to **`family-b`**, waits for **`new_message`**, then **`GET`**s the signed **`download_url`** and compares bytes to the upload. Uses the **`ws`** package so WebSocket upgrades can send the same headers as HTTP.

**Local (starts `wrangler dev` on port 8787):**

```bash
cd server
npm run test:e2e
```

This writes a temporary **`.dev.vars`** with a random secret (existing **`.dev.vars`** is backed up and restored). Alternatively keep **`wrangler dev`** running and point the script at it:

```bash
export BULLERBY_DEVICE_SECRET='…'   # must match .dev.vars / Worker
export E2E_SKIP_SERVER=1
export E2E_BASE_URL=http://127.0.0.1:8787
npm run test:e2e
```

**Deployed Worker (same script; HTTPS / WSS):** set the secret to match **`wrangler secret put`**, then:

```bash
export BULLERBY_DEVICE_SECRET='…'
export E2E_SKIP_SERVER=1
export E2E_BASE_URL='https://<your-worker>.<subdomain>.workers.dev'   # URL from wrangler deploy output
node scripts/e2e-full.mjs
```

Or use **`npm run test:e2e:only`** with the same env vars (runs **`e2e-full.mjs`** only). Confirm **`GET /api/devices/{id}/config`** returns **200** with the same Bearer before running the full E2E if you just rotated the secret.

This is a **Node** client check, not firmware; it validates the live Cloudflare Worker + Durable Object contract.

## API (summary)

| Method | Path | Headers |
|--------|------|---------|
| GET | `/health` | — |
| GET | `/api/devices/{device_id}/config` | `Authorization: Bearer`, `X-Device-Id` |
| POST | `/api/devices/register` | same |
| POST | `/api/messages` | same; `Content-Type: multipart/form-data` |
| GET | `/api/messages/{id}/audio?exp=&sig=` | signed URL from `new_message` (no Bearer) |
| GET | `/api/ws` → WebSocket | same headers as HTTP |

**Multipart** (`POST /api/messages`):

- `audio` — file field (audio blob, max **128 KiB**). Codec is up to the sender — today firmware uploads **raw 16-bit mono PCM**; swapping in Opus later is transparent to the server.
- `metadata` — JSON string:
  - `to_family_id` *(optional)* — omit or `"ALL"` / `"broadcast"` for broadcast.
  - `duration_s` *(optional)* — float; echoed in `new_message`.
  - `sample_rate_hz` *(optional)* — integer; defaults to **16000** when missing. Echoed in `new_message` so the receiver can retune its I2S TX clock. Firmware today uploads **24000**.

  Example: `{ "to_family_id": "family-b", "duration_s": 2.5, "sample_rate_hz": 24000 }`.

**WebSocket** (after `connected`):

- Send `{ "type": "heartbeat" }` → `{ "type": "heartbeat_ack", ... }`.
- Receive `{ "type": "new_message", "message_id", "from_family_id", "duration_s", "sample_rate_hz", "download_url" }` — `GET` the URL once (signed, expires with relay TTL).

## Commands

```bash
cd server
npm install
npm test             # required before ship; also runs automatically before npm run deploy
npm run test:e2e     # local wrangler dev + full WS/HTTP E2E (see above)
npm run test:e2e:only # run scripts/e2e-full.mjs only (needs E2E_BASE_URL + secret)
npm run dev          # local
npm run deploy       # runs tests, then wrangler deploy
npm run types        # after wrangler.toml changes
```

## Regenerate TypeScript `Env`

```bash
npm run types
```

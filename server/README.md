# Bullerby Chat — Cloudflare Worker

Ephemeral voice relay: **HTTP** upload, **WebSocket** push, **short-lived** audio fetch, **git-bundled** config (`config/bullerby.json`). No long-term mailbox — see `docs/project-plan.md`.

## Auth

Identification-only: every request sends `X-Device-Id: <id>` (or `?device_id=<id>` on the WS URL) and the Worker accepts it if the id is listed in `config/bullerby.json`. Audio download URLs are unsigned. No shared secret, no bearer. Security is not a goal — this is a family toy.

## What you need

1. **Cloudflare** account (free tier: use `new_sqlite_classes` migration — already in `wrangler.toml`).
2. **Wrangler** authenticated: `npx wrangler login` (OAuth) once per machine.
3. **Node.js** 18+ and npm.

## Config

Edit **`config/bullerby.json`** (families + one device per family), commit, then `npm run deploy`. Devices see updates on the next `GET /api/devices/{id}/config` or after a WebSocket reconnect. Each **`devices[].id`** must match the firmware **`X-Device-Id`** (Kconfig / NVS, or `esp-xxxxxxxxxxxx` when **Bullerby Chat → Derive device id from chip WiFi MAC** is enabled in menuconfig).

## Testing (required before deploy)

Automated tests use **Vitest** + **`@cloudflare/vitest-pool-workers`** (runs against `workerd` with your `wrangler.toml`).

```bash
cd server
npm install
npm test              # run once (CI / pre-deploy)
npm run test:watch    # during development
```

- **`test/auth.test.ts`** — device-id allowlist.
- **`test/worker.test.ts`** — HTTP integration: `/health`, `/api/devices/.../config`, `/api/devices/register`.

**`npm run deploy`** runs **`npm test` first** (`predeploy`). To deploy without tests (emergency only), use `npx wrangler deploy` directly — do not make that the default workflow.

**Deployed `workers.dev` URL:** Wrangler prints it at the end of `deploy`. It matches **`https://<name>.<subdomain>.workers.dev`** where **`name`** is `wrangler.toml`’s `name` field and **`<subdomain>`** is account-specific (not in git). Confirm with `curl -sI "https://<name>.<subdomain>.workers.dev/health"` (expect `HTTP/2 200`). Firmware **Server base URL** (or NVS `bullerby` / `server_url`) must use that exact origin — no trailing slash.

### End-to-end (WebSocket + upload + download)

**`scripts/e2e-full.mjs`** exercises the full relay path: **`wss://…/api/ws`** as **`device-uuid-002`** (with `X-Device-Id`), **`POST /api/messages`** as **`device-uuid-001`** to **`family-b`**, waits for **`new_message`**, then **`GET`**s the **`download_url`** and compares bytes to the upload.

**Local (starts `wrangler dev` on port 8787):**

```bash
cd server
npm run test:e2e
```

**Against an already-running server or deployed Worker:**

```bash
export E2E_SKIP_SERVER=1
export E2E_BASE_URL='https://<your-worker>.<subdomain>.workers.dev'
npm run test:e2e:only
```

## API (summary)

| Method | Path | Headers |
|--------|------|---------|
| GET | `/health` | — |
| GET | `/api/devices/{device_id}/config` | `X-Device-Id` |
| POST | `/api/devices/register` | `X-Device-Id` |
| POST | `/api/messages` | `X-Device-Id`; `Content-Type: multipart/form-data` |
| GET | `/api/messages/{id}/audio` | — (URL from `new_message`) |
| GET | `/api/ws` → WebSocket | `X-Device-Id` header or `?device_id=` query |

**Multipart** (`POST /api/messages`):

- `audio` — file field (audio blob, max **128 KiB**). Codec is up to the sender — today firmware uploads **raw 16-bit mono PCM**.
- `metadata` — JSON string:
  - `to_family_id` *(optional)* — omit or `"ALL"` / `"broadcast"` for broadcast.
  - `duration_s` *(optional)* — float; echoed in `new_message`.
  - `sample_rate_hz` *(optional)* — integer; defaults to **16000**. Echoed in `new_message` so the receiver can retune its I2S TX clock. Firmware today uploads **24000**.

**WebSocket** (after `connected`):

- Send `{ "type": "heartbeat" }` → `{ "type": "heartbeat_ack", ... }`.
- Receive `{ "type": "new_message", "message_id", "from_family_id", "duration_s", "sample_rate_hz", "download_url" }` — `GET` the URL once (expires with relay TTL).

## Commands

```bash
cd server
npm install
npm test             # required before ship; also runs automatically before npm run deploy
npm run test:e2e     # local wrangler dev + full WS/HTTP E2E (see above)
npm run test:e2e:only # run scripts/e2e-full.mjs only (needs E2E_BASE_URL)
npm run dev          # local
npm run deploy       # runs tests, then wrangler deploy
npm run types        # after wrangler.toml changes
```

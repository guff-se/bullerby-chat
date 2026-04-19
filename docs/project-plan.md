# Bullerby Chat — Project Plan

> A kid-friendly intercom system using ESP32-S3 voice chat robots.
> Families send voice messages to each other by tapping icons on a tiny round screen.

## Overview

Bullerby Chat turns cheap ESP32-S3 "AI robot" devices into a neighborhood intercom
for kids. Each device shows icons for every family (plus an "all" broadcast icon).
Tap a family → record a message → tap again to send. The recipient's device beeps
and shows a notification. Tap to play. No phones, no screens, no accounts for kids.

### Development strategy (current)

**Firmware first, fully offline.** We implement graphic design, LVGL screens, touch
flows, recording/playback, inbox UX, and battery/status UI using **dummy family
data** baked into firmware (names/icons that stand in for real families). No WiFi
or internet is required for this phase—networking and the Bullerby server are
**deferred** until the on-device experience is complete and stable.

Later we add provisioning, HTTP/WebSocket, and a server without redesigning the core UI.

**Interface design:** [ui-spec.md](ui-spec.md) — family carousel, touch targets, embedded UX practices.

---

## 1. System Architecture

```
┌──────────────┐         HTTPS / WSS          ┌──────────────────┐
│  ESP32-S3    │◄────────────────────────────►│   Bullerby       │
│  Device      │   (upload, relay push,       │   Server         │
│              │    config)                   │                  │
│  ┌────────┐  │                              │  ┌────────────┐  │
│  │ Round  │  │                              │  │  REST +    │  │
│  │ LCD    │  │                              │  │  WSS       │  │
│  └────────┘  │                              │  ├────────────┤  │
│  ┌────────┐  │                              │  │  Ephemeral │  │
│  │ Mic +  │  │                              │  │  relay     │  │
│  │Speaker │  │                              │  │  (~minutes)│  │
│  └────────┘  │                              │  ├────────────┤  │
│  ┌────────┐  │                              │  │  Config    │  │
│  │ Touch  │  │                              │  │  from repo │  │
│  └────────┘  │                              │  │  deploy    │  │
└──────────────┘                              └──────────────────┘
      x12 devices                         Cloudflare Workers (+ DOs)
```

*End state.* During offline firmware development, the right-hand side is unused:
the device loops recordings into local buffers or flash, simulates “received”
messages, and uses a fixed dummy family list.

### Components

| Component | Tech | Description |
|-----------|------|-------------|
| **Firmware** | C / ESP-IDF 5.5 | Runs on each ESP32-S3 device |
| **Server** | **Cloudflare Workers** + **Durable Objects** (TypeScript/JavaScript) | HTTPS + WSS, ephemeral relay; **no long-term message archive** |
| **Configuration** | **`server/config/bullerby.json`** in **this repo** | Families + devices (allowlist); edit in git, `npm run deploy` in `server/` (no admin web app) |
| **Relay** | In-memory in **`RelayRoom` Durable Object** | Hold in-flight Opus only until delivered or dropped; **~10 min** TTL, **3 delivery waves**, **~2 min** between retries (see §3.5) |

---

## 2. Device Firmware

**Detailed firmware roadmap:** [firmware-plan.md](firmware-plan.md) (modules, phases, storage, audio, touch/swipe evaluation, testing).

**UI specification (v2 ring layout, touch targets, embedded UX):** [ui-spec.md](ui-spec.md).

**Language:** On-device **user-visible** copy is **Swedish** (see [ui-spec.md](ui-spec.md), Language). Server/API field names and JSON tokens may remain English (e.g. `ALL` / `broadcast` in metadata); the device shows **ALLA** and other Swedish strings in the UI.

### 2.1 UI (LVGL on 240x240 round LCD)

Canonical layout: **[ui-spec.md](ui-spec.md) v2 (ring)** — implemented in firmware today.

- **Home screen:** Ring of **tappable family circles** (neon fill + emoji) around the disc; **ALLA** as broadcast row; **center message bubble** when the dummy inbox has items (play / count / replay affordance)
- **Recording screen:** Family colour flood, **name in top bar**, large record/stop control, back; after send, **random Swedish toast** then return home
- **Inbox:** Count and entry via **center bubble** on home; **dedicated inbox list screen** still roadmap (playback UX)
- **Optional later:** CST816D **gesture** evaluation for swipe-between-families pager — [firmware-plan.md](firmware-plan.md)

### 2.2 Audio Pipeline

- **Recording:** Mic → I2S → 16kHz 16-bit PCM → Opus encode (16kbps VBR) → buffer
- **Playback:** Opus decode → I2S → Speaker (via ES8311 + PA)
- **Max message:** 30 seconds ≈ ~60KB Opus at 16kbps
- **Storage:** Buffer in PSRAM during record, upload immediately after

### 2.3 Connectivity

**Phase A — offline / dummy (now):** WiFi optional; no server. Family list and
messages can be stubbed (e.g. static structs, local NVS or FAT partition for
recorded clips during development).

**Phase B — networked (later):**

- **WiFi:** Connect to configured home network (credentials via provisioning)
- **Protocol:** WebSocket to **`GET /api/ws`** (same `Authorization` + `X-Device-Id` as HTTP)
  - JSON `{ "type": "heartbeat" }` / `{ "type": "heartbeat_ack" }` (firmware may still use ~30s cadence for UX)
  - Auto-reconnect with exponential backoff
- **Message upload:** `POST /api/messages` — `multipart/form-data`: field **`audio`** (file), **`metadata`** (JSON string: `to_family_id`, `duration_s`, **`sample_rate_hz`** — defaults to `16000` if omitted; omit or `"ALL"`/`broadcast` for broadcast)
- **Message download:** `GET` the **`download_url`** from the `new_message` WebSocket event — signed query **`exp`** + **`sig`**, **not** a server inbox to poll
- **Config sync:** After WiFi, **`GET /api/devices/{id}/config`** (authenticated) returns `family_id`, `families[]` (`id`, `name`, `icon`) — firmware applies this to the in-memory family model and refreshes the home ring (see `model_apply_server_config_json` in `firmware/main/model/model_families.c`). Until then, a static table matches the bundled JSON for offline dev.

### 2.4 Provisioning (First-Time Setup)

Deferred until server integration. **Later:** device boots into AP mode (captive
portal), user enters home WiFi + server URL, credentials in NVS, reboot → WiFi →
register with server.

### 2.5 Key Interactions

```
[Home Screen]
  │
  ├─ Tap family icon ──► [Recording Screen] ──► Tap again ──► Upload ──► [Home]
  │
  ├─ Tap inbox badge ──► [Playback Screen] ──► Auto-return ──► [Home]
  │
  └─ Tap **ALLA** icon ──► [Recording Screen] ──► Tap again ──► Upload to all ──► [Home]
```

---

## 3. Server

**Implemented** in **`server/`** (Cloudflare Workers + one global **`RelayRoom`** Durable Object). **Canonical API and ops:** [server/README.md](../server/README.md). Firmware still uses dummy data until Phase 4 integration.

### 3.0 Product semantics (instant voice, not voicemail)

- **One device per family** — routing is “which family” → **at most one** online recipient; no fan-out within a family.
- **No mailbox on the server** — the server is a **relay**: accept upload, push to the recipient over WebSocket, optionally hold a **short-lived** buffer (e.g. **~10 minutes** ceiling) only while attempting delivery.
- **Offline recipient** — the implementation retries on a **~2 minute** alarm schedule up to **3 delivery waves** total, then **drops** undelivered clips (see §3.3). They missed it; the product is **live** intercom, not guaranteed async delivery.
- **Device inbox** — **short FIFO** on the device; **delete after listen** (or equivalent: not an archival history). Unheard clips may still expire under a small cap (time/count/MB) so flash does not fill.

Configuration changes (families, which device belongs to which family) are **not** done through a browser: **edit files in git** and redeploy — see §3.5.

### 3.1 REST API (as implemented)

Admin CRUD endpoints are **omitted**; families/devices come from **`server/config/bullerby.json`** at deploy time.

**Authentication:** identification-only. Every HTTP call and the WebSocket upgrade send `X-Device-Id: <device id>` (or `?device_id=` on the WS URL); the Worker accepts it if the id is listed in `config/bullerby.json`. No bearer token. Audio download URLs are unsigned. Security is not a goal for this project — see [server/README.md](../server/README.md).

| Endpoint | Method | Description |
|----------|--------|-------------|
| `GET /health` | GET | Liveness JSON (no auth) |
| `POST /api/devices/register` | POST | Validates device against allowlist; returns `family_id` |
| `GET /api/devices/{id}/config` | GET | `device_id`, `family_id`, `families[]` — path **`id`** must match authenticated device |
| `POST /api/messages` | POST | Multipart **`audio`** + **`metadata`** (JSON); broadcast if `to_family_id` omitted / `ALL` / `broadcast` |
| `GET /api/messages/{id}/audio` | GET | Download while relay still holds the blob until TTL |

There is **no** “list unread messages on server” or “mark read on server” — persistence of “heard / not heard” is **on the device** only.

### 3.2 WebSocket

- **URL:** `GET /api/ws` (TLS **`wss://`** in production).
- **One persistent connection per device** (12 devices ≈ trivial).
- Server → device:
  - `connected` — after upgrade
  - `new_message` — `{ message_id, from_family_id, duration_s, sample_rate_hz, download_url }` — fetch audio before relay TTL; `sample_rate_hz` lets the receiver retune I2S TX
  - **`config_updated`** — *not implemented yet*; devices pick up new config via **`GET .../config`** on reconnect or periodic poll
- Device → server:
  - `{ "type": "heartbeat" }` → `heartbeat_ack`

### 3.3 Implementation constants (relay)

| Parameter | Value (in code) |
|-----------|-----------------|
| Relay TTL | **10 minutes** (`TTL_MS`) |
| Time between retry waves | **~2 minutes** (`RETRY_MS`, Durable Object alarm) |
| Max delivery waves | **3** (`MAX_WAVES`) — initial send + two alarm-driven retries, then drop if still undelivered |
| Max upload size | **128 KiB** audio per message |
| Topology | Single Durable Object id **`global`** — all WebSockets and pending clips in one `RelayRoom` |

### 3.4 Data model (conceptual)

**Authoritative** families and device→family mapping live in **config files** in the repo, not as rows users edit through an API.

```
Family (in config)
  id          stable string or UUID
  name        string
  icon        string (emoji or letter)

Device (in config + NVS)
  id          UUID (or MAC-based) — must match allowlist in config
  family_id   exactly one family  (one device per family)

In-flight message (server — ephemeral only)
  id          UUID
  from_family id
  to_family   id (or broadcast rule)
  blob or path  temporary until delivered or dropped
  expires_at    tied to relay TTL / retry policy
```

**Secrets:** none. Auth is an `X-Device-Id` allowlist check against bundled config. **Relay** audio is held in the **`RelayRoom` Durable Object** (in-memory `Map`); the DO class uses Cloudflare’s **SQLite-backed** Durable Object product on the free plan — SQLite storage API is **not** used for message blobs; alarms drive retries.

### 3.5 Configuration (repository, not a web UI)

- **Source of truth:** **`server/config/bullerby.json`** — `families[]` (`id`, `name`, `icon`) and `devices[]` (`id`, `family_id`).
- **Workflow:** edit in git → commit → **`cd server && npm run deploy`**; devices refresh via **`GET /api/devices/{id}/config`** (and reconnect WebSocket).
- **No** dedicated admin SPA — operator tooling is **git + deploy**, not in-product CRUD.

---

## 4. Infrastructure To Set Up

**Firmware-only work does not require any of this.** Use when starting server integration.

**Stack:** **Cloudflare Workers** (public `https` / `wss` on `*.workers.dev` or a **custom domain** on Cloudflare). TLS is **terminated by Cloudflare**; no separate reverse proxy or VM required.

### 4.1 What you will need

| Need | Notes |
|------|--------|
| **Cloudflare account** | Free tier is sufficient to start for this scale; confirm current [Workers pricing](https://developers.cloudflare.com/workers/platform/pricing/) |
| **Wrangler CLI** | Develop, test, and deploy Workers + Durable Objects |
| **Config in repo** | Bundled at deploy (e.g. `wrangler.toml` + config files) or synced via CI — **no** runtime admin UI |
| **R2** | *Not used* in current build (blobs stay in DO memory) |

### 4.2 Server setup checklist

- [x] Worker project under **`server/`** ([repo-structure.md](repo-structure.md))
- [x] **`RelayRoom`** Durable Object (`new_sqlite_classes` migration for free plan)
- [ ] Attach **custom domain** (optional) in Cloudflare DNS for a stable device URL
- [x] Local dev: **`cd server && npm run dev`**; deploy: **`npm run deploy`**
- [x] **End-to-end relay script** — **`npm run test:e2e`** / **`scripts/e2e-full.mjs`** (WebSocket + upload + GET); optional check against deployed `*.workers.dev` — see [server/README.md](../server/README.md)

### 4.3 Development Environment

- [ ] Install ESP-IDF v5.5.2+ (via espressif/vscode-esp-idf-extension or CLI)
- [ ] Clone this repo
- [ ] Connect ESP32-S3 device via USB
- [ ] `idf.py set-target esp32s3 && idf.py build && idf.py flash monitor`
- [ ] Server: **Node.js** + **`cd server && npm run dev`**; before deploy run **`cd server && npm test`** (also runs automatically via **`npm run deploy`**); optional **`cd server && npm run test:e2e`** for WebSocket + upload + signed download — see [server/README.md](../server/README.md))

### 4.4 Network Considerations

- Devices need **internet** path to **Cloudflare’s edge** (not a LAN-only Pi unless you add tunneling — out of scope unless you decide otherwise)
- WebSocket connections are lightweight — 12 devices ≈ negligible load

---

## 5. Implementation Phases

### Phase 1: Skeleton (hardware bring-up)
- [x] Set up ESP-IDF project targeting the ESP32-S3 round display board
- [x] Display + LVGL running on 240×240 GC9A01
- [x] Touch input
- [ ] Audio: record from mic and play back through speaker (codec path)
- [ ] WiFi: connect to a configured network *(can stay optional until Phase 4)*

### Phase 2: Offline firmware — UI, dummy data, local audio (current focus)

**Goal:** A complete on-device experience with no network: looks and feels like the
final product for families, using placeholder data.

**Interface checkpoint (Apr 2026):** **v2 ring** home + recording shell + Swedish copy + emoji assets are **done for now** (see [ui-spec.md](ui-spec.md) status). Next focus: **codec from record UI**, **persistence**, **inbox list / playback** product path — not more layout churn on the current screens.

- [x] **Dummy families:** Static table (names, ids) + **ALLA**; replaceable with server config; device family id in **NVS** (`CONFIG_BULLERBY_DEFAULT_FAMILY_ID` default)
- [x] **Graphic design & LVGL (v2):** Ring of circles, emoji assets, neon palette, center message bubble, record screen typography and controls per ui-spec
- [ ] **Touch / swipe (optional):** Log / use **CST816D** gesture codes for a future pager or extras — not required for tap-only ring; see [firmware-plan.md](firmware-plan.md)
- [x] **Recording screen (UI):** Tap ring → full-screen record/stop, **30 s UI cap**, Swedish send toast → home (**I2S from record button** still Phase D — **BOOT** PCM path today)
- [ ] **Playback product path:** Opus/PCM through speaker from chosen message; **scrollable inbox list** screen TBD (home bubble + dummy model exercise counts / tap)
- [x] **Status:** Battery % on home; inbox **count** on center bubble (dummy `model_messages`)
- [ ] **Optional:** Simulate “new message” with a button or timer to test sounds/LEDs

No HTTP, WebSocket, or provisioning in this phase.

### Phase 3: Server MVP

- [x] **Cloudflare Workers** — `server/`: Wrangler, **`RelayRoom`**, REST + **`/api/ws`**, bundled **`config/bullerby.json`**, `X-Device-Id` allowlist auth
- [x] Load **families/devices** from JSON at deploy; **`register`** + **`GET .../config`** enforce allowlist
- [x] **`POST /api/messages`** + relay TTL + WebSocket **`new_message`** + **`GET .../audio`** + alarm retries then drop
- [x] **`npm run deploy`** (runs **`npm test`** first); docs in [server/README.md](../server/README.md)
- [x] **Automated tests** — Vitest + `@cloudflare/vitest-pool-workers` in `server/test/` (see [server/test/README.md](../server/test/README.md))
- [ ] **Firmware integration** — HTTP client + WebSocket client against deployed Worker (Phase 4)

### Phase 4: Firmware + server integration

**Transport landed Apr 2026** (behind `CONFIG_BULLERBY_ENABLE_NET=y`): WiFi STA, `api_register` + `api_fetch_config`, `wss://…/api/ws` with 30 s heartbeat, multipart upload, signed GET, I2S playback at sender's `sample_rate_hz`. See [firmware-plan.md §Phase G](firmware-plan.md).

- [x] **Server schema aligned** — firmware family table (`ANSUND`…`TADAA`) + `server/config/bullerby.json` now carry matching `server_id`s (`family-a`…`family-h`) and 8 devices (`device-uuid-001`…`008`).
- [x] **`sample_rate_hz` round-trip** — device uploads mono 24 kHz PCM with the field in `metadata`; server stores + forwards it on `new_message`; receiving device reclocks I2S TX so the played-back audio sounds correct regardless of sender rate.
- [x] **HTTP + WebSocket client** (`firmware/main/net/`) — HTTPS via `esp_http_client` + mbedTLS cert bundle, WSS via `esp_websocket_client`.
- [x] **BOOT-hold capture uploads** to the server (broadcast), clipped to the 128 KiB server cap.
- [x] **Remote audio playback** on the device (worker task + 128 KiB PSRAM download buffer).
- [x] Replace dummy family list with **server-fetched** `families[]` after a successful `GET …/config` (static table remains the offline / pre-config fallback).
- [ ] **Route record-screen sends** from UI to `net_send_pcm(family->server_id, …)` instead of broadcast-only (wire up via `model_family_by_server_id`).
- [ ] Opus encode (today: **raw PCM** at 24 kHz mono — fits the 128 KiB cap for ≤ ~2.7 s clips).
- [ ] Captive portal WiFi provisioning + server URL in NVS (today: `Kconfig` / NVS pre-seeded).
- [ ] OTA firmware updates via server.
- [ ] LED feedback (recording, new message, connected/disconnected) — only `hal_led_set(true)` during capture today.
- [x] Low battery warning on screen.
- [ ] Edge cases: WiFi dropout retry UX, server unreachable toast, full storage.

### Phase 5: Scale to 12 Devices

- [ ] Test with all 12 devices simultaneously
- [ ] Confirm relay + retry policy under load (still tiny — no message “archive” to tune)
- [x] **`GET /health`** on Worker (optional deeper monitoring later)

---

## 6. Tech Decisions & Rationale

| Decision | Choice | Why |
|----------|--------|-----|
| Audio codec | Opus 16kbps | Excellent quality at tiny size; ESP-IDF has native support |
| Server protocol | HTTP + WebSocket | HTTP for bulk transfer, WS for real-time push — simple and proven |
| Server retention | **Ephemeral relay** (~minutes), retry then drop | Intercom semantics — not voicemail; tiny storage footprint |
| Configuration | **Git repo** + deploy | No admin UI; operator edits config files |
| Server runtime | **Cloudflare Workers** (TypeScript) | Edge; one **SQLite-backed** `RelayRoom` DO for WS + alarms; blobs in memory |
| Optional storage | **KV / R2** | *Unused* in v1 — relay clips are not written to R2/KV |
| Display framework | LVGL 9.x | De facto standard for ESP32 displays, great touch support |
| Provisioning | Captive portal AP | No app needed, works from any phone |
| **Hosting / TLS** | **Cloudflare** | Public HTTPS + WSS; custom domain optional |
| **Server testing** | **Vitest** + `@cloudflare/vitest-pool-workers` | Runs in `workerd`; **`npm run deploy`** runs **`npm test`** first |

---

## 7. Open Questions

- [ ] **`config_updated` over WebSocket** — broadcast after deploy so devices refetch without reconnect-only
- [ ] **Icon system:** Emoji-based? Or simple letter avatars? Custom bitmaps?
- [ ] **Device inbox cap:** Max **FIFO depth**, max **MB**, or max **age** for unheard clips before eviction (firmware)
- [ ] **Security:** Today auth is an `X-Device-Id` allowlist only (deliberately minimal for a family toy). If it ever matters, add per-device tokens or certs.
- [ ] **Outbox on device:** If WiFi/server is down when sending, **queue and retry** outgoing only (separate from “missed incoming” semantics)

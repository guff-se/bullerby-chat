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

**UI specification (carousel, scale, embedded UX):** [ui-spec.md](ui-spec.md).

### 2.1 UI (LVGL on 240x240 round LCD)

- **Home screen:** Grid of circular family icons filling the round display
  - Each icon: a letter or simple pictogram + family name
  - Last slot: "ALL" icon (broadcast)
  - Scrollable if >6 families (but targeting ~5-6 families initially)
  - **Swipe evaluation:** Test hardware-reported swipes (CST816D gesture register) to see if **horizontal swipes** can switch the active family (e.g. carousel); details in [firmware-plan.md](firmware-plan.md).
- **Recording screen:** Pulsing red circle + timer (0–30s), tap to stop
- **Inbox indicator:** Badge count on status area + LED blink
- **Playback screen:** Speaker icon + progress bar, auto-returns to home

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
- **Message upload:** `POST /api/messages` — `multipart/form-data`: field **`audio`** (file), **`metadata`** (JSON string: `to_family_id`, `duration_s`; omit or `"ALL"`/`broadcast` for broadcast)
- **Message download:** `GET` the **`download_url`** from the `new_message` WebSocket event — signed query **`exp`** + **`sig`**, **not** a server inbox to poll
- **Config sync:** On boot, **`GET /api/devices/{id}/config`** (authenticated); same JSON shape as bundled `bullerby.json` families + device assignment

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
  └─ Tap "ALL" icon ──► [Recording Screen] ──► Tap again ──► Upload to all ──► [Home]
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

**Authentication (HTTP + WebSocket upgrade):** `Authorization: Bearer <BULLERBY_DEVICE_SECRET>` and `X-Device-Id: <device id>` for every call **except** signed audio download. Set the secret with `wrangler secret put BULLERBY_DEVICE_SECRET` (see [server/README.md](../server/README.md)).

| Endpoint | Method | Description |
|----------|--------|-------------|
| `GET /health` | GET | Liveness JSON (no auth) |
| `POST /api/devices/register` | POST | Validates device against allowlist; returns `family_id` |
| `GET /api/devices/{id}/config` | GET | `device_id`, `family_id`, `families[]` — path **`id`** must match authenticated device |
| `POST /api/messages` | POST | Multipart **`audio`** + **`metadata`** (JSON); broadcast if `to_family_id` omitted / `ALL` / `broadcast` |
| `GET /api/messages/{id}/audio?exp=&sig=` | GET | HMAC-signed download while relay still holds the blob until TTL (no Bearer) |

There is **no** “list unread messages on server” or “mark read on server” — persistence of “heard / not heard” is **on the device** only.

### 3.2 WebSocket

- **URL:** `GET /api/ws` (TLS **`wss://`** in production).
- **One persistent connection per device** (12 devices ≈ trivial).
- Server → device:
  - `connected` — after upgrade
  - `new_message` — `{ message_id, from_family_id, duration_s, download_url }` — fetch audio before relay TTL
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

**Secrets:** `BULLERBY_DEVICE_SECRET` via Workers Secrets (shared bearer for all devices in the current design). **Relay** audio is held in the **`RelayRoom` Durable Object** (in-memory `Map`); the DO class uses Cloudflare’s **SQLite-backed** Durable Object product on the free plan — SQLite storage API is **not** used for message blobs; alarms drive retries.

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
- [x] **`BULLERBY_DEVICE_SECRET`** — `wrangler secret put BULLERBY_DEVICE_SECRET` (required for authenticated routes; value not retrievable after set — keep in **`server/.dev.vars`** or a vault for local/E2E)
- [x] Local dev: **`cd server && npm run dev`**; deploy: **`npm run deploy`**
- [x] **End-to-end relay script** — **`npm run test:e2e`** / **`scripts/e2e-full.mjs`** (WebSocket + upload + signed GET); optional check against deployed `*.workers.dev` — see [server/README.md](../server/README.md)

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

- [ ] **Dummy families:** Static list (names, letters/icons) representing ~5–6
      families + “ALL”; easy to replace later with server-driven config
- [ ] **Graphic design & LVGL:** Home grid, typography, colors, icons on round screen
- [ ] **Touch / swipe:** Log and evaluate **CST816D hardware gesture** codes; if reliable, use **swipes** to move between families (carousel); see [firmware-plan.md](firmware-plan.md)
- [ ] **Recording flow:** Tap family → recording screen (timer, pulsing indicator) →
      stop; store PCM/Opus locally or in RAM for demo
- [ ] **Playback flow:** Inbox UI with fake or locally queued “messages”; tap to play
      through speaker
- [ ] **Status:** Battery %, recording state, simple inbox badge (dummy counts)
- [ ] **Optional:** Simulate “new message” with a button or timer to test sounds/LEDs

No HTTP, WebSocket, or provisioning in this phase.

### Phase 3: Server MVP

- [x] **Cloudflare Workers** — `server/`: Wrangler, **`RelayRoom`**, REST + **`/api/ws`**, bundled **`config/bullerby.json`**, `BULLERBY_DEVICE_SECRET` auth
- [x] Load **families/devices** from JSON at deploy; **`register`** + **`GET .../config`** enforce allowlist
- [x] **`POST /api/messages`** + relay TTL + WebSocket **`new_message`** + signed **`GET .../audio`** + alarm retries then drop
- [x] **`npm run deploy`** (runs **`npm test`** first); docs in [server/README.md](../server/README.md)
- [x] **Automated tests** — Vitest + `@cloudflare/vitest-pool-workers` in `server/test/` (see [server/test/README.md](../server/test/README.md))
- [ ] **Firmware integration** — HTTP client + WebSocket client against deployed Worker (Phase 4)

### Phase 4: Firmware + server integration

- [ ] Replace dummy family list with config from server (or cache + sync)
- [ ] Opus encode; upload message via HTTP; WebSocket for push
- [ ] Download and play received messages; real inbox counts
- [ ] Captive portal WiFi provisioning + server URL in NVS
- [ ] OTA firmware updates via server
- [ ] LED feedback (recording, new message, connected/disconnected)
- [ ] Low battery warning on screen
- [ ] Edge cases: WiFi dropout, server unreachable, full storage

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
- [ ] **Security:** Today **one shared** `BULLERBY_DEVICE_SECRET` for all devices; per-device tokens or certs later?
- [ ] **Outbox on device:** If WiFi/server is down when sending, **queue and retry** outgoing only (separate from “missed incoming” semantics)

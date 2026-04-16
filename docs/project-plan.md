# Bullerby Chat — Project Plan

> A kid-friendly intercom system using ESP32-S3 voice chat robots.
> Families send voice messages to each other by tapping icons on a tiny round screen.

## Overview

Bullerby Chat turns cheap ESP32-S3 "AI robot" devices into a neighborhood intercom
for kids. Each device shows icons for every family (plus an "all" broadcast icon).
Tap a family → record a message → tap again to send. The recipient's device beeps
and shows a notification. Tap to play. No phones, no screens, no accounts for kids.

---

## 1. System Architecture

```
┌──────────────┐         HTTPS / WSS          ┌──────────────────┐
│  ESP32-S3    │◄────────────────────────────►│   Bullerby       │
│  Device      │   (message upload/download,  │   Server         │
│              │    push notifications,       │                  │
│  ┌────────┐  │    device config sync)       │  ┌────────────┐  │
│  │ Round  │  │                              │  │  REST API   │  │
│  │ LCD    │  │                              │  │  + WSS      │  │
│  └────────┘  │                              │  ├────────────┤  │
│  ┌────────┐  │                              │  │  Message    │  │
│  │ Mic +  │  │                              │  │  Store      │  │
│  │Speaker │  │                              │  ├────────────┤  │
│  └────────┘  │                              │  │  SQLite DB  │  │
│  ┌────────┐  │                              │  ├────────────┤  │
│  │ Touch  │  │                              │  │  Admin Web  │  │
│  └────────┘  │                              │  │  UI         │  │
└──────────────┘                              └──────────────────┘
      x12 devices                                  1 server
```

### Components

| Component | Tech | Description |
|-----------|------|-------------|
| **Firmware** | C / ESP-IDF 5.5 | Runs on each ESP32-S3 device |
| **Server** | Go or Python (FastAPI) | Relays messages, stores state |
| **Admin Web UI** | Simple SPA (vanilla JS or Svelte) | Configure families & devices |
| **Message Store** | Filesystem + SQLite | Audio files on disk, metadata in DB |

---

## 2. Device Firmware

### 2.1 UI (LVGL on 240x240 round LCD)

- **Home screen:** Grid of circular family icons filling the round display
  - Each icon: a letter or simple pictogram + family name
  - Last slot: "ALL" icon (broadcast)
  - Scrollable if >6 families (but targeting ~5-6 families initially)
- **Recording screen:** Pulsing red circle + timer (0–30s), tap to stop
- **Inbox indicator:** Badge count on status area + LED blink
- **Playback screen:** Speaker icon + progress bar, auto-returns to home

### 2.2 Audio Pipeline

- **Recording:** Mic → I2S → 16kHz 16-bit PCM → Opus encode (16kbps VBR) → buffer
- **Playback:** Opus decode → I2S → Speaker (via ES8311 + PA)
- **Max message:** 30 seconds ≈ ~60KB Opus at 16kbps
- **Storage:** Buffer in PSRAM during record, upload immediately after

### 2.3 Connectivity

- **WiFi:** Connect to configured home network (credentials via provisioning)
- **Protocol:** WebSocket (persistent connection to server)
  - Heartbeat every 30s to detect disconnection
  - Auto-reconnect with exponential backoff
- **Message upload:** HTTP POST multipart (audio blob + metadata JSON)
- **Message download:** HTTP GET (triggered by WebSocket notification)
- **Config sync:** On boot, fetch family list + device assignment from server

### 2.4 Provisioning (First-Time Setup)

- Device boots into AP mode (captive portal)
- User connects phone to device's WiFi
- Web page: enter home WiFi credentials + server URL
- Credentials stored in NVS (non-volatile storage)
- Device reboots and connects to WiFi → registers with server

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

### 3.1 REST API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `POST /api/devices/register` | POST | Device self-registers on first boot |
| `GET /api/devices/{id}/config` | GET | Device fetches its config (family list, own family ID) |
| `POST /api/messages` | POST | Upload voice message (audio + recipient family ID) |
| `GET /api/messages/{id}/audio` | GET | Download audio file |
| `GET /api/families` | GET | List all families |
| `POST /api/families` | POST | Create family (admin) |
| `PUT /api/families/{id}` | PUT | Update family (admin) |
| `DELETE /api/families/{id}` | DELETE | Remove family (admin) |
| `GET /api/devices` | GET | List all devices (admin) |
| `PUT /api/devices/{id}` | PUT | Assign device to family, set name (admin) |
| `DELETE /api/devices/{id}` | DELETE | Remove device (admin) |
| `GET /api/messages?family={id}&unread=true` | GET | List pending messages for a family |
| `PUT /api/messages/{id}/read` | PUT | Mark message as read |

### 3.2 WebSocket

- Persistent connection per device
- Server pushes events:
  - `new_message` — a new voice message arrived for this device's family
  - `config_updated` — family list changed, device should re-fetch config
- Device sends:
  - `heartbeat` — keep-alive

### 3.3 Data Model

```
Family
  id          UUID
  name        string
  icon        string (emoji or letter)
  created_at  timestamp

Device
  id          UUID (burned into NVS at provisioning, or MAC-based)
  family_id   UUID (nullable — unassigned until admin configures)
  name        string
  last_seen   timestamp
  created_at  timestamp

Message
  id          UUID
  from_family UUID
  to_family   UUID (NULL = broadcast)
  audio_path  string (path to opus file on disk)
  duration_s  float
  created_at  timestamp
  read_at     timestamp (nullable)
```

### 3.4 Admin Web UI

Single-page app served by the same server process:

- **Families page:** Add/edit/remove families, set icon + name
- **Devices page:** See all registered devices, assign to family, rename
- **Messages page:** (optional) View message log, playback from browser

---

## 4. Infrastructure To Set Up

### 4.1 What You Need

| Item | Recommendation | Cost Estimate |
|------|---------------|---------------|
| **Server** | Any small VPS (Hetzner CX22, DigitalOcean $6/mo, or a Raspberry Pi at home) | ~$5–6/mo or one-time ~$60 |
| **Domain** | e.g. `bullerby.yourdomain.com` (optional but nice for HTTPS) | ~$10/yr |
| **TLS certificate** | Let's Encrypt (free, auto-renew via certbot or Caddy) | Free |
| **DNS** | Point domain to server IP (A record) | Included with domain |

### 4.2 Server Setup Checklist

- [ ] Provision a VPS or Raspberry Pi with a static IP / DDNS
- [ ] Install Docker (simplifies deployment)
- [ ] Set up reverse proxy with automatic HTTPS (Caddy recommended — zero config TLS)
- [ ] Deploy Bullerby Server as a Docker container
- [ ] Open ports: 443 (HTTPS + WSS)
- [ ] Set up a simple backup for SQLite DB + audio files (rsync / cron)

### 4.3 Development Environment

- [ ] Install ESP-IDF v5.5.2+ (via espressif/vscode-esp-idf-extension or CLI)
- [ ] Clone this repo
- [ ] Connect ESP32-S3 device via USB
- [ ] `idf.py set-target esp32s3 && idf.py build && idf.py flash monitor`
- [ ] Server: run locally for dev (`docker compose up` or direct run)

### 4.4 Network Considerations

- All devices need WiFi access to the server
- If server is on the local LAN (Raspberry Pi), no internet needed for core messaging
- If server is a VPS, devices need internet access
- WebSocket connections are lightweight — 12 devices ≈ negligible load

---

## 5. Implementation Phases

### Phase 1: Skeleton (get hardware working)
- [ ] Set up ESP-IDF project targeting the sp-esp32-s3-1.28-box board
- [ ] Get display working: show a static image on the round LCD
- [ ] Get audio working: record from mic and play back through speaker
- [ ] Get touch working: detect taps on screen regions
- [ ] Get WiFi working: connect to a configured network

### Phase 2: Server MVP
- [ ] Set up project (Go or Python + SQLite)
- [ ] Implement REST API for messages and families
- [ ] Implement WebSocket for real-time notifications
- [ ] Build minimal admin web UI (list families, add/remove, assign devices)
- [ ] Dockerize

### Phase 3: Firmware MVP
- [ ] LVGL UI: home screen with family icons grid
- [ ] Touch → start/stop recording flow
- [ ] Opus encode recorded audio
- [ ] Upload message to server via HTTP
- [ ] WebSocket connection for push notifications
- [ ] Download and play received messages
- [ ] Inbox badge / notification sound

### Phase 4: Provisioning & Polish
- [ ] Captive portal WiFi provisioning
- [ ] OTA firmware updates via server
- [ ] LED feedback (recording, new message, connected/disconnected)
- [ ] Low battery warning on screen
- [ ] Handle edge cases (WiFi dropout, server unreachable, full storage)

### Phase 5: Scale to 12 Devices
- [ ] Test with all 12 devices simultaneously
- [ ] Optimize server for concurrent WebSocket connections
- [ ] Message cleanup (auto-delete after 7 days)
- [ ] Monitoring / health dashboard (optional)

---

## 6. Tech Decisions & Rationale

| Decision | Choice | Why |
|----------|--------|-----|
| Audio codec | Opus 16kbps | Excellent quality at tiny size; ESP-IDF has native support |
| Server protocol | HTTP + WebSocket | HTTP for bulk transfer, WS for real-time push — simple and proven |
| Database | SQLite | Zero ops, single-file, plenty fast for 12 devices |
| Server language | TBD (Go or Python) | Both work; Go = single binary, Python = faster to prototype |
| Display framework | LVGL 9.x | De facto standard for ESP32 displays, great touch support |
| Provisioning | Captive portal AP | No app needed, works from any phone |
| Deployment | Docker + Caddy | Simple, auto-HTTPS, easy to back up |

---

## 7. Open Questions

- [ ] **Server language:** Go (simpler deployment) or Python/FastAPI (faster prototyping)?
- [ ] **Icon system:** Emoji-based? Or simple letter avatars? Custom bitmaps?
- [ ] **Multiple devices per family:** Should each device in a family get the message, or just one?
- [ ] **Message history:** Keep last N messages per family? Or just latest unread?
- [ ] **Security:** Simple shared secret per device, or proper device certificates?
- [ ] **Offline messages:** Queue on device if server unreachable and send later?

# Bullerby Chat — Firmware Development Plan

This document is the **detailed roadmap for ESP-IDF firmware** only. High-level product
and server planning live in [project-plan.md](project-plan.md).

**On-device UI design** (v2 **ring** layout, touch targets, embedded UX):
[ui-spec.md](ui-spec.md).

**Strategy:** Build the full on-device UX first using **dummy family data** and **no
network**. Add WiFi, provisioning, and server sync in a later milestone without
rewriting the UI core.

---

## 1. Goals and constraints

| Goal | Notes |
|------|--------|
| **Offline-first** | All screens, audio record/playback, and inbox simulation work without WiFi or a backend. |
| **Dummy data** | Families (names, icons, ids) are compile-time or small NVS blobs; replace with API later. |
| **Round UI** | 240×240 circular mask: content stays in the visible disc; avoid critical actions in corners. |
| **Stable HAL** | Keep `hal/` as thin drivers; app logic and LVGL live above. |
| **Future-proof** | Clear seams for “config source” (static → server) and “transport” (none → HTTP/WS). |

**Hardware (current codebase):** ESP32-S3, GC9A01 round LCD, CST816D touch, ES8311 codec + I2S, status LED, boot button, battery ADC + charge detect. Pins: `main/hal/hal.h`.

---

## 2. Repository layout (firmware)

| Path | Role |
|------|------|
| `firmware/main/main.c` | `app_main`, audio capture / playback, net wiring |
| `firmware/main/hal/` | Hardware abstraction: `display.c`, `touch.c`, `codec.c`, LED/battery helpers in `hal.h` |
| `firmware/main/app/` | LVGL screens + screen manager (`ui_app.c`) |
| `firmware/main/model/` | Families (+ server id), inbox messages |
| `firmware/main/net/` | `identity` (NVS/Kconfig), `wifi`, `api_client` (HTTPS), `ws_client` (wss), `net` (orchestrator) — gated by `CONFIG_BULLERBY_ENABLE_NET` |
| `firmware/main/CMakeLists.txt` | Sources and `REQUIRES` |
| `firmware/main/idf_component.yml` | LVGL, esp_lvgl_port, esp_lcd_gc9a01, esp_codec_dev (ES8311), esp_websocket_client |
| `firmware/partitions.csv` | Dual OTA apps + **8 MB `storage` (SPIFFS)** at `0x800000` — use for offline clips/metadata later |

**Direction:** Split `main.c` into modules as complexity grows, e.g. `ui/`, `app/`, `audio/`, `model/` (see §7).

---

## 3. Current state (baseline)

**UI language:** All **user-visible** strings on the device are **Swedish** (see [ui-spec.md](ui-spec.md), Language). Code identifiers and API tokens may stay English.

Already in place:

- GC9A01 + LVGL 9 + esp_lvgl_port (partial buffers, RGB565 byte swap, rotation)
- CST816D touch → LVGL pointer device; **hardware gesture** register is **read and logged** on change (not yet driving carousel logic)
- ES8311 + I2S at 24 kHz via the managed **`esp_codec_dev`** component (mirrors xiaozhi-esp32 sp-esp32-s3-1.28-box). Both I2S channels enabled once in `hal_codec_init` and left running; TX silently clocks zeros when idle. `hal_pa_enable` gates the speaker amp around playback. Current levels: `set_in_gain=36 dB` (SNR/level tradeoff), `set_out_vol=100`. `hw_gain={pa_voltage=5.0 V, codec_dac_voltage=3.3 V}` so the codec DAC is scaled correctly. **Boot-button** hold → record to PSRAM → release → playback (PCM loopback)
- Battery % + charging flag on screen; **low-battery** tint below `BATTERY_PCT_LOW_WARN` (15%); status LED
- **Networking** (Phase G — landed Apr 2026) behind **`CONFIG_BULLERBY_ENABLE_NET`** (off by default in `sdkconfig.defaults`; may be on in a checked-in `sdkconfig`): WiFi STA from **`CONFIG_BULLERBY_WIFI_SSID/PASS`** when SSID is non-empty, else NVS `bullerby` keys **`wifi_ssid`** / **`wifi_pass`** → `api_register` + `api_fetch_config` → `wss://…/api/ws` with 30 s heartbeat → on `new_message`, signed HTTPS GET → I2S playback at sender's `sample_rate_hz`. BOOT-hold capture also uploads mono PCM via multipart POST when online (clipped to the server's 128 KiB cap). All HTTPS/WSS verified against the **mbedTLS cert bundle** (`esp_crt_bundle_attach`)
- **Model:** `family_t` + **ALLA** (broadcast); **`message_t`** + in-memory **inbox** (`model_messages.c`); **`model_my_family_id`** from **NVS** then **`GET …/config`** `family_id` via **`model_apply_server_config_json()`** when net is up; static `family-a`…`h` table is the offline fallback; optional **`model_set_my_family_id()`** for provisioning
- **UI (`ui_app.c`):** **Home** = **ring of family circles** (even angular spacing, emoji inside each) + **status** (battery); **center message bubble** when the dummy inbox has items (tap → model mark-read / state); **Recording** = full-bleed family colour, title bar, **Record/Stop**, back, **random Swedish send toast**, **30 s max** UI timer; **Record/Stop** drives **real** I2S capture on `audio_task` (same pipeline as **BOOT** hold: PCM → loopback + optional upload). No horizontal strip / zoom overlay in current tree — superseded by v2 (see **ui-spec.md** implementation notes).

**Gaps for product UX:** Record/stop drives **I2S** from the UI; Opus + SPIFFS; **dedicated inbox list** + decode→speaker playback; optional **LED / sound** on send; optional CST816D **gesture** pager; richer **screen transitions** beyond fade.

---

## 4. Architecture overview

```
┌─────────────────────────────────────────────────────────┐
│  app/          State machine, screen navigation          │
│  ui/           LVGL screens (home, record, inbox, …)    │
│  model/        Families, messages (dummy → NVS/API)      │
│  audio/        Record/play pipeline, Opus, file I/O      │
├─────────────────────────────────────────────────────────┤
│  hal/          display, touch, codec, LED, battery       │
│  (future) net/ WiFi, HTTP client, WebSocket              │
└─────────────────────────────────────────────────────────┘
```

- **UI layer** must not talk to I2S directly; it calls **audio service** APIs (start/stop record, play clip id).
- **Model** exposes “list families”, “enqueue outgoing”, “list inbox” — backed by static arrays first, then NVS/SPIFFS, then server.

---

## 5. Phased roadmap

### Phase A — HAL polish (short)

Purpose: predictable building blocks for the app layer.

- [ ] **Codec contract:** Document sample rate (24 kHz vs 16 kHz target for Opus); align project-plan (16 kHz) with `AUDIO_SAMPLE_RATE` or resample.
- [ ] **Optional:** Expose `hal_audio` wrappers around I2S RX/TX + PA instead of raw handles from `main.c`. Candidate: migrate `play_mono_pcm` off raw `i2s_channel_write` to `esp_codec_dev_write` (drops the manual stereo-expansion + `i2s_channel_reconfig_std_clock` retune hack for off-rate remote playback; would need `esp_codec_dev_close`/`open` on sample-rate change, or server-side resample to 24 kHz).
- [x] **Power latch:** `hal_power_init` holds `POWER_HOLD_PIN` (GPIO3) high via `rtc_gpio_*`, keeping the regulator on for soft-power push-button boards. Harmless if the latch circuit is absent.
- [ ] **Touch:** Ensure no long I2C work on LVGL task; keep CST816D read in registered callback (already).
- [ ] **Swipe / gesture probe:** The CST816D exposes a **gesture** register (see `CST816D_REG_GESTURE` in `touch.c`). Build a small test mode or boot-time logging that prints **hardware-reported gesture codes** when the user swipes (up/down/left/right if supported). Goal: verify whether we get **reliable swipe direction** from the chip vs. having to infer swipes from raw coordinate streams in software. If hardware gestures are stable on this board, we can use **horizontal swipes to move between families** on the home screen (carousel / pager) instead of or in addition to a dense icon grid.
- [x] **Battery:** Low-battery threshold for UI warning — `BATTERY_PCT_LOW_WARN` (15%) in `hal.h`; home status bar turns red when not charging.
- [x] **Networking flag:** `CONFIG_BULLERBY_ENABLE_NET` in `sdkconfig` / `sdkconfig.defaults` disables WiFi + HTTPS + WSS for offline dev.

### Phase B — App model (dummy data)

Purpose: one place that defines “who are the families” and “what is a message”.

- [x] **Structs:** `family_t` in `model_families.h` (local `id`, Swedish `name`, `is_broadcast`, **`server_id`** for API round-trips); `message_t` in `model_messages.h` (id, from family, label, duration, unread — extend later with timestamp/storage ref).
- [x] **Dummy table:** families + ALLA in `model_families.c`; inbox rows in `model_messages.c`. Firmware table is aligned with `server/config/bullerby.json` (8 families; `family-a`…`family-h`).
- [x] **Device identity:** `model_my_family_id` in NVS namespace `bullerby`, key `family_id`; first boot uses `CONFIG_BULLERBY_DEFAULT_FAMILY_ID`; **`model_set_my_family_id()`** for later provisioning. With **`CONFIG_BULLERBY_ENABLE_NET`**, server config overwrites the local id from `family_id` and re-persists NVS. Optional **`CONFIG_BULLERBY_DEVICE_ID_FROM_MAC`** sets `device_id` to `esp-` + 12 hex WiFi MAC (add matching `devices[].id` in `server/config/bullerby.json`).
- [ ] **Outbox** demo + optional **SPIFFS** for persisted clips (namespaced paths).

### Phase C — UI shell and navigation

Purpose: replace the single test screen with a real navigation stack.

- [x] **Screen manager:** `lv_screen_load` / `lv_screen_load_anim` in `ui_app.c` (**home**, **recording**); center bubble on home covers inbox affordance until a list screen exists.
- [x] **Global chrome:** Battery on home; recording has back; message bubble shows count when non-empty.
- [x] **Round layout (v2):** Ring math + circular widgets + `no_scroll()` — content kept in the visible disc per ui-spec (no scrollable strip).
- [x] **Fonts:** **Montserrat 14 + 20** Latin-1 subsets in `firmware/main/fonts/` (headers in `fonts.h`); Nordic extras optional later.

**Screens (offline):**

| Screen | Purpose |
|--------|---------|
| **Home** | **Ring** of family circles + **ALLA**; tap → record; **center bubble** = inbox entry (dummy model) |
| **Recording** | Large record/stop, family name bar, back; send toast; fade back to home |
| **Sending (fake)** | Random Swedish one-liner toast (~3 s) — no network |
| **Inbox list** | **TODO** — scrollable list; today counts + tap path via **bubble** on home |
| **Playback** | **TODO** — full play UI; bubble + model hooks are placeholders |

- [x] **Transitions:** **Fade** between home ↔ record (`LV_SCR_LOAD_ANIM_FADE_ON`, ~180–200 ms).
- [ ] **Further UI polish:** Dedicated inbox list + playback chrome; optional **gesture** pager (Phase A); extra anims if FPS budget allows.

**Interface:** Treated **complete for now** (Apr 2026); see [ui-spec.md](ui-spec.md) status line. Audio wiring and inbox list are the next UX-moving work.

### Phase D — Audio product path (offline)

Purpose: match product requirements while staying disconnected.

- [x] **Recording from UI:** Start/stop on the record screen toggles **`app_audio_set_ui_recording`** → `main.c` `audio_task` I2S capture (30 s UI cap unchanged). **BOOT** hold remains for quick capture without opening the screen.
- [ ] **Buffer strategy:** PSRAM ring or fixed buffer; stop cleanly on max time or user stop.
- [ ] **Opus:** Encode pipeline (ESP-ADF or `libopus` component); store `.opus` blobs on SPIFFS or raw PCM first then convert.
- [ ] **Playback:** Decode Opus → I2S; enable PA only during play.
- [ ] **Simulated receive:** Button, menu action, or timer injects a “new message” into inbox pointing at a bundled sample or last recording — exercises notification + list + play.

### Phase E — Storage and persistence

The partition table already has **8 MB SPIFFS** (`storage`).

- [ ] **Mount SPIFFS** at boot (or FAT if you switch partition type later).
- [ ] **File naming:** e.g. `/storage/msg_<uuid>.opus` + small JSON sidecar or fixed binary index in NVS.
- [ ] **Retention policy:** **Short FIFO** for received clips — **delete after listen**; optional small cap (count/MB/age) so SPIFFS does not fill. Aligns with [project-plan.md](project-plan.md) (instant intercom, not archive).
- [ ] **Wear / errors:** Handle full SPIFFS gracefully in UI.

### Phase F — Feedback and edge cases

- [ ] **LED patterns:** Solid / blink for recording, new message, charging (define table).
- [ ] **Sounds:** Optional short beeps via short PCM buffers (codec init cost vs UX).
- [ ] **Power:** Screen blanking / brightness curve (backlight PWM already in `display.c` path).
- [ ] **Concurrency:** One audio operation at a time; mutex between UI and `audio_task`.

### Phase G — Networking (landed, Apr 2026)

Behind `CONFIG_BULLERBY_ENABLE_NET=y`. All files live under `firmware/main/net/`.

- [x] **Identity:** `net/identity.c` reads NVS namespace `bullerby` keys `device_id` / `server_url`; falls back to `CONFIG_BULLERBY_DEVICE_ID` / `SERVER_URL`. Trailing slash on `server_url` stripped. Auth is just `X-Device-Id` — no shared secret.
- [x] **WiFi manager:** `net/wifi.c` — `wifi_init_driver()` (STA+AP netifs), `wifi_sta_connect()` for first join (45 s timeout), then STA auto-reconnect on drop. **Credential order:** NVS `bullerby` keys `wifi_ssid` / `wifi_pass` first, then non-empty `CONFIG_BULLERBY_WIFI_SSID` / `PASS`. If no credentials or STA times out: open SoftAP **`Bullerby-` + MAC** (`wifi_portal.c`), DHCP captive-portal URI, DNS redirect (`dns_server.c` from ESP-IDF example), HTTP form POST `/save` → NVS → `esp_restart()`. **UI:** `ui_app_show_wifi_setup()` tells the user which SSID to join. `wifi_wait_connected()` in `net_worker` after successful bootstrap.
- [x] **HTTPS:** `net/api_client.c` — `POST /api/devices/register`, `GET /api/devices/{id}/config` (logged), `POST /api/messages` (multipart/form-data with boundary `----bullerby7f3c2e9a`, mono PCM + `X-Device-Id` header, metadata JSON with `sample_rate_hz`), `GET <download_url>` for audio. mbedTLS cert bundle via `esp_crt_bundle_attach`.
- [x] **WebSocket:** `net/ws_client.c` opens `wss://…/api/ws` via `esp_websocket_client`, 30 s heartbeat, reassembles fragmented text frames into a 4 KiB buffer, parses `new_message` (`message_id`, `from_family_id`, `sample_rate_hz`, `duration_s`, `download_url`), dispatches to a callback.
- [x] **Orchestrator:** `net/net.c` — worker task drains an inbox queue, downloads audio to a 128 KiB PSRAM buffer, hands it to `main.c` for playback. `net_send_pcm(to_family_server_id, pcm, len, sr, duration)` wraps the upload for the BOOT-hold capture path.
- [x] **Playback:** `main.c::play_mono_pcm` reclocks I2S TX via `i2s_channel_reconfig_std_clock` when the sender's sample rate ≠ 24 kHz; shared `s_audio_lock` mutex gates capture vs. remote playback.
- [ ] **Routing from UI:** Tapping a family circle currently only opens the record screen; wiring "record → upload to that family's `server_id`" (via `model_family_by_server_id`) is next.
- [ ] **OTA:** Second OTA slot already in partition table; add HTTPS OTA when packaging exists.

**Server reference:** Production API is implemented in **`server/`** (see **`docs/project-plan.md` §3**). Before changing server behaviour, run **`cd server && npm test`** (see **`server/README.md`**).

---

## 6. UI / UX specifics (round display)

Authoritative layout and interaction rules: **[ui-spec.md](ui-spec.md)**.

- **Safe area:** Treat center ~220 px diameter as primary; outer ring for subtle chrome only.
- **Touch targets:** Minimum ~44 px for kids; larger for main actions.
- **Family grid:** e.g. 2×3 or hex layout; **ALLA** visually distinct (color/icon).
- **Swipe between families (optional):** After validating CST816D-reported swipes (Phase A), consider a **pager** UI: one large family tile per screen, **swipe left/right** to change family, **tap** to record. Reduces clutter on a small round display; confirm gestures do not misfire when kids tap.
- **Recording:** Clear “recording” state (red dot, timer); block accidental navigation.
- **Vibe:** Playful, colorful, wacky—see [ui-spec.md](ui-spec.md) (neighborhood kids, not corporate a11y).

---

## 7. Suggested module split (refactor from skeleton)

When `main.c` grows, extract:

| Module | Contents |
|--------|----------|
| `app_state.c` | Current screen, inbox count, whether recording |
| `ui_home.c` | Home grid LVGL |
| `ui_record.c` | Recording screen |
| `ui_inbox.c` | List + playback |
| `model_families.c` | Dummy data + accessors |
| `audio_service.c` | Tasks, Opus, SPIFFS I/O |
| `storage_msgs.c` | SPIFFS index + delete |

Keep `hal_*` as-is; only extend when hardware changes.

---

## 8. Dependencies to add (when needed)

| Need | Component |
|------|-----------|
| Opus encode/decode | `libopus` / esp-opus or ADF patterns |
| SPIFFS | `esp_spiffs` / `spiffs` in IDF |
| JSON metadata | `cJSON` (IDF component) |
| TLS client | `esp_http_client` + mbedTLS (Phase G) |

Track in `idf_component.yml` or IDF component manager as you integrate.

---

## 9. Testing and quality

| Layer | Approach |
|-------|----------|
| **On device** | Serial logs, LVGL assert, stack watermark (`uxTaskGetStackHighWaterMark`) |
| **Audio** | Loopback first; then file play; finally Opus round-trip |
| **Long run** | Soak test: repeated record/play, SPIFFS fill, thermal check |
| **Regression** | Tag firmware versions when UI milestones land |
| **Touch / swipe** | Log gesture codes from CST816D; try slow vs fast swipes, edges of the round panel; decide software fallback (LVGL gesture on coordinates) if hardware is unreliable |

---

## 10. Build, flash, monitor

```bash
cd firmware
. ~/esp/esp-idf/export.sh   # or your IDF path
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Use **`idf.py menuconfig`** for PSRAM, CPU frequency, and optional WiFi disable.

---

## 11. Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Flash/PSRAM pressure with LVGL + Opus | Measure; tune LVGL buffer size; strip fonts |
| SPIFFS full | Quotas + UI message |
| Audio underruns | Correct buffer sizes; raise I2S task priority carefully |
| UI thread blocking | Never block LVGL task; use queues to audio task |
| Round clip bugs | Test scrollable content early |

---

## 12. Checklist summary (offline milestone)

- [x] Dummy families + message model
- [x] Navigation + **home / recording** + center **message bubble** (inbox list screen + dedicated **playback** screen still TODO)
- [x] **v2 ring UI shell** — **interface parked** (Apr 2026); see [ui-spec.md](ui-spec.md)
- [x] Record/stop from UI + max duration — **codec** path in `main.c` shared with BOOT hold
- [ ] Opus + SPIFFS persistence (or PCM interim)
- [ ] Simulated incoming message path
- [x] **Battery** warning tint on home; **LED** messaging patterns still TODO
- [x] Networking optional / off by default for dev (`CONFIG_BULLERBY_ENABLE_NET`); full Phase G transport landed
- [ ] CST816D swipe/gesture evaluation documented; optional pager UX if stable

When this list is done, you are ready to attach **Phase G** networking without changing the fundamental UI flow.

---

## 13. Related docs

- [project-plan.md](project-plan.md) — product scope, server API (§3), infrastructure
- [server/README.md](../server/README.md) — HTTP/WebSocket contract and Wrangler deploy
- [ui-spec.md](ui-spec.md) — v2 ring layout, touch targets, embedded UX

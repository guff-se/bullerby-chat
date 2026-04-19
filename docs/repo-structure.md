# Repository Structure

Map of the repo for agents and humans landing fresh. The **HTTP/WebSocket API** and Worker deploy steps live in **[server/README.md](../server/README.md)** (summary in [project-plan.md](project-plan.md) §3). High-level firmware roadmap is in [firmware-plan.md](firmware-plan.md).

```
bullerby-chat/
│
├── AGENTS.md                       # Read first — agent guide, conventions, workflow
├── bullerby-chat.code-workspace    # VS Code multi-root workspace
├── .vscode/                        # Editor settings (workspace-shared)
│
├── docs/                           # Product, UI, firmware, and structure docs
│   ├── product-description.md      # Hardware specs and pin assignments
│   ├── project-plan.md             # Master plan: scope, server API summary (§3)
│   ├── firmware-plan.md            # Detailed ESP-IDF firmware roadmap (phases A–G)
│   ├── ui-spec.md                  # On-device UI/UX (v2 ring layout)
│   └── repo-structure.md           # This file
│
├── scripts/                        # Build-time tooling (Python)
│   └── gen_family_emoji_assets.py  # Pillow → firmware/main/assets/family_emoji_assets.{c,h}
│
├── firmware/                       # ESP-IDF project for the device
│   ├── CMakeLists.txt
│   ├── partitions.csv              # Dual OTA + 8 MB SPIFFS at 0x800000
│   ├── sdkconfig.defaults          # Committed defaults (Kconfig baseline)
│   ├── sdkconfig                   # ⚠ Currently committed; per AGENTS.md it should be developer-local
│   ├── dependencies.lock           # Component Manager lock
│   ├── .vscode/settings.json       # Per-firmware editor settings
│   └── main/
│       ├── CMakeLists.txt          # SRC_DIRS + PRIV_REQUIRES (driver, esp_lcd, …)
│       ├── Kconfig.projbuild       # CONFIG_BULLERBY_* (NET, DEVICE_ID, WIFI_*, DEFAULT_FAMILY_ID, …)
│       ├── idf_component.yml       # Managed components (LVGL, esp_lcd_gc9a01, esp_codec_dev, …)
│       ├── board_config.h          # Pin map for the Spotpear ESP32-S3 1.28" round LCD board
│       ├── main.c                  # app_main; audio_task (BOOT-hold + UI capture); play_mono_pcm
│       │
│       ├── app/                    # LVGL UI + audio glue
│       │   ├── ui_app.{h,c}        # v2 ring home + recording screen + Swedish toasts
│       │   └── app_audio.h         # app_audio_set_ui_recording() — UI ↔ audio_task signal
│       │
│       ├── hal/                    # Thin hardware drivers (no app logic)
│       │   ├── hal.h               # Public HAL API + pin macros (display, codec, LED, power)
│       │   ├── display.{h,c}       # GC9A01 + LVGL via esp_lvgl_port
│       │   ├── touch.{h,c}         # CST816D over I2C bus 1
│       │   ├── codec.c             # ES8311 via esp_codec_dev (I2C bus 0 + I2S)
│       │   ├── led.{h,c}           # Status LED GPIO
│       │   └── power.c             # POWER_HOLD_PIN latch (RTC GPIO HIGH for soft-power boards)
│       │
│       ├── model/                  # In-memory data model (dummy → server-backed)
│       │   ├── model_families.{h,c}  # family_t + ALLA broadcast; NVS my_family_id; server config apply
│       │   └── model_messages.{h,c}  # message_t + dummy inbox
│       │
│       ├── net/                    # Optional: gated by CONFIG_BULLERBY_ENABLE_NET (off by default)
│       │   ├── identity.{h,c}      # NVS namespace `bullerby` device_id / server_url (Kconfig fallback)
│       │   ├── wifi.{h,c}          # STA bootstrap + auto-reconnect (NVS / Kconfig credentials)
│       │   ├── wifi_portal.{h,c}   # SoftAP captive portal when no credentials / STA times out
│       │   ├── dns_server.{h,c}    # Captive-portal DNS redirect (from ESP-IDF example)
│       │   ├── api_client.{h,c}    # HTTPS: register, fetch config, multipart POST, audio GET
│       │   ├── ws_client.{h,c}     # WSS /api/ws + 30 s heartbeat + new_message dispatch
│       │   └── net.{h,c}           # Worker task: download → playback callback; net_send_pcm
│       │
│       ├── assets/                 # Generated LVGL emoji images (regen via scripts/)
│       │   └── family_emoji_assets.{h,c}
│       │
│       └── fonts/                  # Subset Montserrat Latin-1 (committed, regen offline if needed)
│           ├── fonts.h
│           ├── lv_font_montserrat_14_latin1.c
│           └── lv_font_montserrat_20_latin1.c
│
└── server/                         # Cloudflare Worker + Durable Object
    ├── README.md                   # HTTP/WS contract, deploy, env, testing
    ├── wrangler.toml               # Worker + RelayRoom DO (new_sqlite_classes for free plan)
    ├── package.json                # Scripts: dev, deploy (predeploy → npm test), test, test:e2e
    ├── package-lock.json
    ├── tsconfig.json
    ├── vitest.config.mts           # @cloudflare/vitest-pool-workers
    ├── env.d.ts                    # Env augmentations (no secrets — DO binding only)
    ├── worker-configuration.d.ts   # Generated (`npm run types`)
    ├── .gitignore
    │
    ├── src/
    │   ├── index.ts                # HTTP routes; forwards relay traffic to RelayRoom DO
    │   ├── auth.ts                 # X-Device-Id allowlist check (header or ?device_id=)
    │   ├── config.ts               # Loads bundled config/bullerby.json
    │   ├── types.ts                # Shared types
    │   └── durable/
    │       └── relay-room.ts       # WS hub + ephemeral relay + alarm-driven retry waves
    │
    ├── config/
    │   ├── bullerby.json           # 8 families + 8 devices (allowlist) — edit, deploy
    │   └── README.md               # Config schema + edit/deploy notes
    │
    ├── scripts/                    # Out-of-Vitest helpers
    │   ├── e2e-full.mjs            # WSS + multipart POST + GET audio against wrangler dev
    │   └── run-e2e-with-dev.mjs    # Spawns wrangler dev around e2e-full.mjs
    │
    └── test/                       # Vitest (Workers pool)
        ├── README.md
        ├── auth.test.ts            # X-Device-Id allowlist
        └── worker.test.ts          # Routes, multipart, WS upgrade
```

## What's intentionally absent

- **`firmware/managed_components/`** — vendored by the Component Manager from `idf_component.yml` + `dependencies.lock`. Do not edit; gitignored.
- **`firmware/build/`** — CMake/IDF build tree. Gitignored.
- **`server/.wrangler/`**, **`node_modules/`** — local-only. Gitignored.
- **No CI workflow** is checked in yet (see AGENTS.md → Testing → CI).
- **No `deploy/`** — server deploy is `wrangler deploy` from `server/`.

## Source-of-truth pointers

| Topic | Authoritative file |
|-------|--------------------|
| Hardware pins | `firmware/main/board_config.h` and `firmware/main/hal/hal.h` |
| Build dependencies | `firmware/main/idf_component.yml` + `firmware/dependencies.lock` |
| Kconfig options | `firmware/main/Kconfig.projbuild` |
| Server API | `server/README.md` (canonical) and `docs/project-plan.md` §3 |
| Families/devices allowlist | `server/config/bullerby.json` (server) ↔ `firmware/main/model/model_families.c` (offline fallback) |
| Agent workflow | `AGENTS.md` |

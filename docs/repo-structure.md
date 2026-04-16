# Repository Structure

The **HTTP/WebSocket API** and deploy steps for **`server/`** are documented in **[server/README.md](../server/README.md)** (summary in [project-plan.md](project-plan.md) §3).

```
bullerby-chat/
│
├── docs/                          # Project documentation
│   ├── product-description.md     # Hardware specs & pin assignments
│   ├── project-plan.md            # Master plan & architecture
│   ├── ui-spec.md                 # On-device UI/UX (carousel, embedded practices)
│   └── repo-structure.md          # This file
│
├── firmware/                      # ESP-IDF project for the device
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults         # ESP32-S3 specific defaults
│   ├── partitions.csv             # Flash partition table (16MB)
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   ├── main.c                 # Entry point
│   │   ├── app.h / app.c         # Application state machine
│   │   ├── ui/                    # LVGL screens
│   │   │   ├── ui.h
│   │   │   ├── screen_home.c      # Family icon grid
│   │   │   ├── screen_record.c    # Recording screen
│   │   │   └── screen_playback.c  # Playback screen
│   │   ├── audio/                 # Audio recording & playback
│   │   │   ├── audio.h
│   │   │   ├── recorder.c
│   │   │   └── player.c
│   │   ├── net/                   # WiFi, HTTP, WebSocket
│   │   │   ├── net.h
│   │   │   ├── wifi.c
│   │   │   ├── http_client.c
│   │   │   └── ws_client.c
│   │   ├── hal/                   # Hardware abstraction
│   │   │   ├── hal.h
│   │   │   ├── display.c          # GC9A01 + LVGL setup
│   │   │   ├── codec.c            # ES8311 I2S setup
│   │   │   ├── touch.c            # CST816D touch input
│   │   │   └── led.c              # Status LED
│   │   └── provision/             # WiFi provisioning captive portal
│   │       ├── provision.h
│   │       └── provision.c
│   └── components/                # Local ESP-IDF components (if any)
│
├── server/                        # Cloudflare Workers
│   ├── wrangler.toml              # Worker + SQLite-backed Durable Object
│   ├── package.json
│   ├── tsconfig.json
│   ├── env.d.ts                   # augments Env (BULLERBY_DEVICE_SECRET)
│   ├── worker-configuration.d.ts    # generated (`npm run types`)
│   ├── scripts/                   # e2e-full.mjs, run-e2e-with-dev.mjs (see README.md)
│   ├── src/
│   │   ├── index.ts               # HTTP routes + forwards to RelayRoom
│   │   ├── config.ts              # loads bundled config/bullerby.json
│   │   ├── auth.ts                # Bearer + signed download URLs
│   │   ├── types.ts
│   │   └── durable/
│   │       └── relay-room.ts      # WebSocket hub + ephemeral relay + alarms
│   ├── test/                      # Vitest (+ Workers pool); see test/README.md
│   │   ├── auth.test.ts
│   │   ├── worker.test.ts
│   │   ├── constants.ts           # test-only secret for Miniflare
│   │   └── README.md
│   ├── vitest.config.mts
│   └── config/
│       ├── bullerby.json          # families + devices (edit + deploy)
│       └── README.md
│
├── deploy/                        # Optional: non-Workers assets (TBD)
│
└── README.md                      # (to be created later)
```

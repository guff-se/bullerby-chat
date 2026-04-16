# Repository Structure

```
bullerby-chat/
│
├── docs/                          # Project documentation
│   ├── product-description.md     # Hardware specs & pin assignments
│   ├── project-plan.md            # Master plan & architecture
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
├── server/                        # Backend server
│   ├── Dockerfile
│   ├── main.go (or main.py)       # Entry point
│   ├── api/                       # REST API handlers
│   │   ├── messages.go
│   │   ├── families.go
│   │   └── devices.go
│   ├── ws/                        # WebSocket hub
│   │   └── hub.go
│   ├── db/                        # SQLite schema & queries
│   │   ├── schema.sql
│   │   └── db.go
│   ├── store/                     # Audio file storage
│   │   └── store.go
│   └── web/                       # Admin UI static files
│       ├── index.html
│       ├── app.js
│       └── style.css
│
├── deploy/                        # Deployment configs
│   ├── docker-compose.yml
│   └── Caddyfile                  # Reverse proxy config
│
└── README.md                      # (to be created later)
```

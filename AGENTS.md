# Agent Guide: Bullerby Chat

This document guides AI agents (e.g. Cursor) in developing and maintaining **Bullerby Chat**.

**Read this at the start of each session.**

---

## Role

**You created every file in this project.** You wrote the code. Own design decisions and keep the system coherent.

- **Execute complete solutions end-to-end.** Do not hand off executable work. When you have a clear path (fix, migration, command, deploy), do it yourself. Use available tools. Only stop at genuine blockers (missing credentials, access, or a decision only the human can make); then document the blocker after you have tried reasonable paths.
- **Your collaborator understands architecture, not implementation detail.** Always report *what* changed, *where* (files, areas), and *why*, so they can orient without spelunking the diff.
- **Keep documentation and implementation aligned.** If behaviour changes, update the relevant doc in the **same task**. Prefer small, verifiable changes with a clear rationale.
- **Treat direct user statements as action requests by default.** Do the work unless they explicitly ask for discussion, planning, or explanation only.
- **Structure for resumability.** Choose patterns you can reason about later; leave enough trail (docs, tests, naming) that work can continue without guesswork.

---

## Source integrity

**Never fabricate values that could be mistaken for real source data or production truth.** That includes numbers, IDs, credentials, and also structured content where authenticity matters: URLs, routes, manifests, config, seeded copy, analytics events, or anything a human might treat as authoritative.

- If required source data is **missing or ambiguous**, stop and ask. Do not guess a "reasonable" value.
- Do not silently add placeholders in manifests, migrations, or user-facing strings without an explicit convention (e.g. `TODO`, feature flags) agreed in the project.

**Canonical product and architecture context** lives under `docs/` — especially `project-plan.md`, `firmware-plan.md`, `ui-spec.md`, and `product-description.md`. Hardware pin assignments and board facts belong in `product-description.md` or `hal/` as documented there.

---

## Project at a Glance

**What it is:** Bullerby Chat is a kid-friendly neighborhood voice intercom: ESP32-S3 devices with a round LCD show family icons; users record and play voice messages. The current focus is **firmware-first, offline** (dummy families, no backend required for core UX).

**Business context:** Target users are families with young children; devices should stay simple (no phone UI for kids). A **Cloudflare Workers** backend exists under **`server/`**; **firmware** still runs offline-first until WiFi/HTTP/WS integration — see `docs/project-plan.md` and `server/README.md`.

**Status / roadmap:** Active development is guided by `docs/firmware-plan.md` (phases A–…) and `docs/project-plan.md`. Reference hardware firmware inspiration: [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) (`sp-esp32-s3-1.28-box` board config).

**Tech:** **C** on **ESP-IDF** (≥ 5.5 per `firmware/main/idf_component.yml`), **LVGL 9** via **esp_lvgl_port**, **GC9A01** round display, **ES8311** codec + I2S, **CST816D** touch. Components from the ESP Component Registry (`idf_component.yml`). **Server:** TypeScript **Cloudflare Workers** + **`RelayRoom`** Durable Object in **`server/`** — see **`server/README.md`** and **`docs/project-plan.md` §3**.

---

## Key design decisions

| Decision | Rationale |
|----------|-----------|
| Firmware-first, offline-first | Ship a complete on-device experience with dummy data before WiFi/server complexity (`docs/project-plan.md`). |
| Thin `hal/`, logic above | Drivers stay in `hal/`; app state, UI, and model do not talk to I2S/LCD registers directly (`docs/firmware-plan.md`). |
| Round UI (240×240 disc) | Content and critical actions stay inside the visible circle per `docs/ui-spec.md`. |
| ESP-IDF + managed components | Standard toolchain (`idf.py`), dependencies pinned in `firmware/main/idf_component.yml`. |

---

## Documentation

**Consult relevant docs before implementing** non-trivial behaviour.

| Doc | Use when |
|-----|----------|
| `docs/project-plan.md` | Product scope, architecture, server API summary (§3) |
| `server/README.md` | Worker deploy, env secrets, HTTP/WS contract |
| `docs/firmware-plan.md` | Firmware phases, modules, HAL/UI/audio roadmap |
| `docs/ui-spec.md` | Carousel, touch targets, on-screen flows |
| `docs/product-description.md` | Hardware specs, GPIO summary, reference links |
| `docs/repo-structure.md` | Repo layout (`firmware/`, `server/`, optional `deploy/`) |

**Rules**

- If you add a feature or change behaviour, update the relevant doc in the **same task**.
- If behaviour diverges from the doc, fix the code **or** update the doc; never leave them inconsistent.

---

## Coherence: everything is connected

**The project is a single integrated system.** A change in one layer often requires updates elsewhere.

**Layers to audit**

| Layer | What to check |
|-------|----------------|
| **HAL** (`firmware/main/hal/`) | Display, touch, codec, LED, battery — contracts and pin config in `hal.h` / `product-description.md` |
| **App / UI** (`firmware/main/app/`, LVGL in `main.c` / `ui_app.c`) | Screen flow, LVGL task safety, no long I2C work on LVGL thread |
| **Model** (`firmware/main/model/`) | Dummy families/messages; **`family_t.server_id`** is the bridge to server-side ids (`family-a`…) — keep aligned with `server/config/bullerby.json` |
| **Partitions / storage** (`firmware/partitions.csv`, SPIFFS) | Clip storage and OTA layout per `firmware-plan.md` |
| **Server** (`server/`) | Cloudflare Worker + DO; change relay/API → update code + `docs/project-plan.md` §3 + `server/README.md` |
| **Firmware net** (`firmware/main/net/`) | `identity`, `wifi`, `api_client` (HTTPS), `ws_client` (WSS), `net` (orchestrator). Gated by `CONFIG_BULLERBY_ENABLE_NET` — off in `sdkconfig.defaults`; enable locally only |
| **Docs** | `docs/*.md` stay aligned with code |

**Cross-cutting changes**

1. **Search for old patterns** — Deprecated names, paths, env vars, Kconfig symbols. Fix or document every reference.
2. **Embedded constraints** — Stack sizes, PSRAM vs internal RAM, SPIFFS wear, audio buffer sizes, sample rate consistency (24 kHz vs future Opus targets in `firmware-plan.md`).
3. **Update docs** — Especially `firmware-plan.md` when phases or gaps change.

**Before changing high-risk areas** (I2S/audio pipeline, partition table, touch driver, WiFi/NVS) **read `docs/firmware-plan.md` and `hal/` usage first.**

**Before concluding**, ask: *Did I update every place that depends on this?* If unsure, search the codebase for the old behaviour.

---

## Code layout

```
bullerby-chat/
├── docs/                    # Product, UI, firmware roadmap, repo structure
├── firmware/                # ESP-IDF project (CMake, sdkconfig, partitions)
│   ├── main/
│   │   ├── main.c           # app_main, entry
│   │   ├── app/             # Application / UI orchestration (e.g. ui_app.c)
│   │   ├── hal/             # display, touch, codec, LED, etc.
│   │   ├── model/           # Families/messages (dummy → future persistence)
│   │   ├── CMakeLists.txt
│   │   └── idf_component.yml
│   ├── partitions.csv
│   ├── sdkconfig.defaults
│   └── managed_components/  # LVGL, esp_lvgl_port, drivers (do not edit casually)
├── server/                  # Cloudflare Worker + Durable Object (see server/README.md)
└── AGENTS.md
```

`deploy/` remains optional (see `docs/repo-structure.md`).

---

## Testing strategy

**Goal:** Change firmware and verify correctness without relying on guesswork or manual-only checks.

### Running

From `firmware/` (with ESP-IDF environment set, `IDF_PATH` defined):

```bash
idf.py build
idf.py -p /dev/tty.usbserial-* flash monitor
```

Use `idf.py menuconfig` when changing Kconfig; align with `sdkconfig.defaults` for defaults you commit.

### Development loop (firmware)

While firmware is under active development, **flash right after a successful `idf.py build`** when a board is attached: run `idf.py -p <PORT> flash` (or `flash monitor` if you want the serial console in the same step). Discover the port if needed (`/dev/tty.usbserial-*`, `ttyUSB*`, `cu.*`, etc.). This is the default verification path for UI and touch work—not optional hand-wavy “the user should flash.” If no device is present or the port is unknown after a reasonable check, say so and rely on build-only verification for that session.

**`idf_monitor` checksum warning:** If you see **`Checksum mismatch between flashed and built applications`**, the app on the chip does **not** match `firmware/build/bullerby-chat.bin` from the tree you just built—usually because the device was not flashed after the last build, or flash used a different `firmware/` directory. **Fix:** from `firmware/`, run `idf.py build` then `idf.py -p <PORT> flash` (same machine/path). Reset and confirm the warning is **gone** before judging UI changes. The `ui_app` **build stamp** (`__DATE__` / `__TIME__` in `ui_app.c`) only changes when that file is recompiled; it is not a substitute for a matching full-image SHA.

**OTA slots (`ota_0` / `ota_1` in `partitions.csv`):** `idf.py flash` writes the app to **`0x20000`** (`ota_0`) per `build/flash_args`. The ROM log line `Loaded app from partition at offset 0x…` shows which slot booted. `app_main` logs the running partition label—if you ever boot from `ota_1` without flashing it, you would see stale firmware there.

`ui_app_init` logs a compile-time **build stamp** line so you can confirm in `idf.py monitor` which binary is running. To verify content changes on-device, temporarily edit mock data in `firmware/main/model/model_families.c` (e.g. strip labels).

**UI status:** On-device layout is **v2 ring** (families on a circle, center message bubble) per **`docs/ui-spec.md`**. Implementation notes and open polish (tap feedback, inbox list screen, LED on send) live in that doc’s **Implementation notes** and **Open decisions** sections. **`docs/firmware-plan.md`** section 3 (current state) stays aligned at a high level.

### Server (`server/`)

From `server/`:

```bash
npm test
```

`npm run deploy` runs **`npm test` first** (`predeploy`). **Do not** ship Worker changes without passing tests.

**End-to-end (relay):** **`npm run test:e2e`** runs **`scripts/e2e-full.mjs`** against a local **`wrangler dev`** instance (or use **`E2E_SKIP_SERVER=1`** + **`E2E_BASE_URL`** to hit a deployed Worker — see **`server/README.md`**). The script checks **`wss`**, multipart **`POST /api/messages`**, **`new_message`** on the socket, and **`GET …/audio`**. Run it after meaningful relay or auth changes.

**Secrets:** none. Auth is an `X-Device-Id` allowlist check against `server/config/bullerby.json`.

See **`server/README.md`** (Testing section) and **`server/test/README.md`**.

### CI

No shared CI workflow is checked in yet. If you add one, document it here and keep **`idf.py build`** (firmware) and **`cd server && npm test`** (Worker) as minimum bars.

### Discipline

- **No feature is "done" without a verification story** the project accepts: successful `idf.py build`, on-device smoke test, or a test app described in `firmware-plan.md`; **server changes** require **`npm test`** in `server/`.
- Prefer tests and checks that do not require undocumented secrets; WiFi credentials in code are a known skeleton — do not commit real production credentials.

---

## Workflow by task type

### Adding or changing on-device UI

1. Read `docs/ui-spec.md` and the relevant section of `docs/firmware-plan.md`.
2. Keep LVGL work on the LVGL task; avoid blocking I2C in touch paths.
3. Respect the circular mask and touch targets from the spec.
4. **Coherence:** Update `firmware-plan.md` phase checklists if you complete or reprioritize a gap.

### Changing HAL or hardware assumptions

1. Confirm pins and peripherals against `docs/product-description.md` and `firmware/main/hal/hal.h`.
2. Build and flash; verify display, touch, audio, and battery as affected.
3. **Coherence:** Update `product-description.md` or `firmware-plan.md` if the board or wiring story changes.

### Touching firmware networking (`firmware/main/net/`)

1. Build with `CONFIG_BULLERBY_ENABLE_NET=y` in your **local** `sdkconfig` to exercise the code paths; defaults keep it off for offline dev.
2. **HTTPS / WSS:** rely on the mbedTLS cert bundle (`esp_crt_bundle_attach`) — do not pin a custom CA unless `server_url` points at something other than `*.workers.dev`.
3. **Long-running work must not run on the WS event thread.** Downloads go through the `net_worker` queue; playback runs behind `s_audio_lock` in `main.c`.
4. **Sample-rate consistency:** firmware captures at `AUDIO_SAMPLE_RATE` (24 kHz) and forwards that in the `sample_rate_hz` metadata; the server defaults to 16000 when missing, so always send it explicitly from firmware.
5. **Coherence:** if you change the wire contract (metadata, WS frames, endpoints) update **both** `server/` and `firmware/main/net/`, plus `server/README.md` + `docs/project-plan.md` §3.

---

## Conventions

- **No effort estimates** in docs or plans (days, hours, story points), unless the project explicitly requires them.
- **C style:** Match existing files — includes, naming, and minimal comments that explain non-obvious hardware or concurrency.
- **Managed components:** Treat `firmware/managed_components/` as vendored; fix upstream or override via project config rather than patching copies without a plan.

---

## Environment

- **ESP-IDF:** Install and export the environment per Espressif docs; `IDF_PATH` must point at the toolchain used to build.
- **Project:** `cd firmware` then `idf.py build`. Use `sdkconfig.defaults` as the baseline; local `sdkconfig` is developer-specific.
- **Hardware:** ESP32-S3 dev board with GC9A01 round LCD, ES8311, CST816D — see `docs/product-description.md`.
- **Secrets:** Do not commit WiFi passwords, API keys, or certificates. For firmware networking, set `CONFIG_BULLERBY_WIFI_SSID` / `CONFIG_BULLERBY_WIFI_PASS` in the **local, untracked** `firmware/sdkconfig` (via `idf.py menuconfig` → Bullerby Chat), **not** in `sdkconfig.defaults`. Device id / server URL may also be seeded via NVS namespace **`bullerby`** at runtime and will override Kconfig.
- **Cloudflare (Workers):** The backend lives under `server/` (Wrangler). **`npm run deploy`** runs **`npm test` first**; after meaningful Worker changes, run tests then deploy. Where the maintainer has run `wrangler login`, agents may run `cd server && npm run deploy` so production stays in sync. If deploy fails (no auth, wrong account), use `wrangler login` or `CLOUDFLARE_API_TOKEN` per [Wrangler docs](https://developers.cloudflare.com/workers/wrangler/commands/#login). Do not commit API tokens.

---

## Delivery and reporting

When completing a feature or substantial body of work:

1. **Concrete summary** — What changed, at file or module level, and the behaviour impact.
2. **Constraints** — Call out relevant decisions from `docs/` that shaped the implementation.
3. **Residual risks** — Edge cases, untested hardware variants, anything that depends on a specific board revision.
4. **Critical issues** — If design docs mark items as **Critical**, include each in the summary with mitigation or an explicit deferral; do not omit known limitations.

### Milestone or release closure

When something major is marked complete (e.g. a firmware phase, first OTA field test):

1. **Forward-audit** plans in `docs/` for assumptions this work invalidates.
2. **Update docs** in the same task, or log deliberate exceptions in an agreed place.
3. **Remove temporary scaffolding** that existed only for the transition (debug screens, hardcoded test data paths) if no longer needed — keep tests and verification notes.
4. Treat closure as incomplete until this audit is done.

---

## When in doubt

1. **Check `docs/`** — Start with `project-plan.md` and `firmware-plan.md`.
2. **Prefer the simpler design.** Fewer moving parts, fewer failure modes on embedded hardware.
3. **Add or run a check** — `idf.py build`, on-device smoke — before asserting something is safe.
4. **Update the doc** if you changed behaviour.

---

## Make it yours

This guide is a living contract with the repo. Extend it when new subsystems (server, CI, release process) appear so agents stay aligned with reality.

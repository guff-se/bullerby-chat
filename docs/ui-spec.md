# Bullerby Chat — UI Specification (v2: Wacky Kids Ring)

This document defines the **on-device interface** for the ESP32-S3 round display
(240×240). Implementation lives in `firmware/main/app/ui_app.c` (LVGL 9).

**Related:** [project-plan.md](project-plan.md) · [firmware-plan.md](firmware-plan.md) ·
[product-description.md](product-description.md)

---

## Design personality (read this first)

This is for **neighborhood kids**. Loud, weird, joyful. Clashing neons, bouncy
motion, goofy transitions — **features, not bugs**. If a choice is "more fun" vs
"more restrained", choose fun.

**Hardware reality:** 240×240 round LCD, embedded MCU (~20–30 fps), capacitive
touch (CST816D), kid-sized fingers. Keep animations cheap; avoid full-screen
redraws every frame.

---

## 1. Home screen

### 1.1 Ring of families

All families (up to ~12) are arranged as **colored circles on the perimeter** of
the round display, evenly spaced in a clockwise ring starting at 12 o'clock.

| Property | Value |
|----------|-------|
| Ring radius | 88 px from disc center |
| Circle diameter | 44 px |
| Default count | 9 (8 families + ALL) |
| Placement formula | `x = cx + R·sin(θ)`, `y = cy − R·cos(θ)`, `θ = 2π·i/n` |

Each circle:
- **Filled** with a vivid neon color (unique per family, from an 8-shade palette).
- **Neon glow shadow** matching the fill color.
- **White abbreviation** label centered inside (`G`, `H`, `ALL`, …).
- **Tappable** — opens that family's recording screen.

The **ALL** circle: white fill, thick magenta border, dark label text.

### 1.2 Center message bubble

Appears in the **exact center** (120, 120) of the disc **only when messages
exist**. Hidden when the inbox is empty.

| State | Color | Icon | Count badge |
|-------|-------|------|-------------|
| New messages available | Neon gold `#ffdd00` | ▶ | Yes (message count) |
| Last message played, no new | Orange `#ff7700` | ↺ (loop) | No |
| No messages / all deleted | — | — | hidden |

The bubble **pulses** (shadow width 8→28→8 px, 550 ms, infinite) to draw
attention.

**Tap interactions:**

| Condition | Action |
|-----------|--------|
| State = *available*, first play | Plays message; count decrements; state → *played* |
| State = *available*, has prior played | Previous played discarded; next message plays |
| State = *played*, no new messages | Replays last message |

**Auto-delete:** After the device is idle for **2 minutes** with state = *played*
and no new messages, the played message is silently discarded and the bubble hides.

---

## 2. Family / recording screen

Opened by tapping any ring circle. The selected family's **neon color floods the
entire screen**. (ALL circle → vivid magenta `#dd00cc`.)

### 2.1 Layout

```
        ┌──────────────────────────┐
        │                          │
        │       [big circle]       │  ← record / stop button, Ø 84 px
        │        center -14 px     │    hot red → lava orange while recording
        │                          │
        │        [◀ back]          │  ← back button, Ø 38 px, center +68 px
        └──────────────────────────┘
```

### 2.2 Record button states

| State | Button color | Inner indicator | Action on tap |
|-------|-------------|-----------------|---------------|
| Idle | Hot red `#ff1144` | White dot (Ø 26 px) | Start recording |
| Recording | Lava orange `#ff5500` | `■` (STOP symbol) | Stop + send |

Red glow shadow (22 px spread) always present — shifts to orange while recording.

### 2.3 Sent notification

When stop is pressed:
1. Record button **hides**.
2. **"WHOOSH!"** appears in neon mint `#22ff88` at center, font 20 px.
3. After **5 seconds** the screen fades back to Home automatically.
4. Tapping **back** cancels the countdown and returns immediately.

Recording is **auto-stopped** at 30 seconds max.

### 2.4 Back button

Always visible. Returns to Home screen immediately (cancels any in-progress
recording without sending).

---

## 3. What actually must work (hardware constraints)

- **One clear main action per screen.** Home = choose family or play message.
  Recording = record or back.
- **Big tap targets.** Smallest interactive element = 38 px; most = 44–84 px.
- **Snappy response.** Touch → visible change ≤ 100 ms feel-time.
- **Cheap animations.** Shadow-width pulse and fade-on screen transitions only;
  no particles, no full-screen blur stacks.
- **Steady 20–30 fps.** Prefer `transform_scale` / shadow tweaks over
  `lv_obj_invalidate` every frame.

---

## 4. Screen inventory

| Screen | Purpose |
|--------|---------|
| **Home** | Ring of family circles + optional message bubble |
| **Record** | Full-color family screen, record/stop, back, sent notification |

*(Inbox / playback list removed in v2 — messages arrive and play from the home
center bubble.)*

---

## 5. Colours

| Token | Hex | Use |
|-------|-----|-----|
| `COL_BG` | `#0d0921` | Home screen background (deep space) |
| `COL_MSG_READY` | `#ffdd00` | Message bubble — new messages |
| `COL_MSG_PLAYED` | `#ff7700` | Message bubble — replay |
| `COL_REC_BTN` | `#ff1144` | Record button idle |
| `COL_STOP_BTN` | `#ff5500` | Record button active |
| `COL_SENT_TEXT` | `#22ff88` | "WHOOSH!" notification |
| `COL_ALL_FILL` | `#ffffff` | ALL circle fill |
| `COL_ALL_BORDER` | `#ff00ff` | ALL circle border |

Family neon palette (cycled by `(id−1) mod 8`):

| Index | Hex | Name |
|-------|-----|------|
| 0 | `#ff1493` | Hot pink |
| 1 | `#39ff14` | Electric lime |
| 2 | `#ff6600` | Neon orange |
| 3 | `#0066ff` | Electric blue |
| 4 | `#cc00ff` | Vivid violet |
| 5 | `#00e5ff` | Neon cyan |
| 6 | `#ff0033` | Vivid red |
| 7 | `#ffcc00` | Gold |

---

## 6. Implementation notes

- **No carousel.** The v1 horizontal strip is gone. Families sit on the ring at
  fixed angular positions; no scroll or snap logic needed.
- **Ring math:** `θ = 2π·i/n` with `sin`/`cos` from `<math.h>` (`-lm` on
  GCC-based toolchain; available in ESP-IDF).
- **Scroll lock:** `no_scroll()` applied to every widget — the ring layout has no
  scrollable content.
- **Pulse:** `lv_anim_t` on `shadow_width` (8→28→8, 550 ms, infinite repeat with
  playback). `lv_anim_del(bubble, bubble_glow_exec)` stops it when bubble hides.
- **Screen transitions:** `lv_screen_load_anim(…, LV_SCR_LOAD_ANIM_FADE_ON, 180, 0, false)`.
- **Adding families:** Increment `k_families[]` in `model_families.c`. The ring
  recomputes positions automatically for any `n ≤ MAX_FAMILY_CIRCLES (16)`.
- **Inbox screen removed.** `ui/ui.c` helper functions (`ui_set_status`,
  `ui_show_recording`, `ui_show_playback`) are legacy stubs — not called by v2 UI.

---

## 7. Open decisions

- **Tap feedback:** Currently no press animation on ring circles. A quick
  `transform_scale` squash on `LV_EVENT_PRESSED` would add snappiness.
- **ALL broadcast flow:** ALL opens the record screen the same as any family. A
  separate "broadcast" visual treatment (e.g., rainbow shimmer on the record BG)
  would be fun.
- **Volume / haptic feedback:** Not wired yet; LED flash on send would add
  delight.
- **More than 12 families:** Ring math handles up to 16 (MAX_FAMILY_CIRCLES).
  Beyond ~12 the circles start to crowd; a second concentric ring would be needed.

---

## 8. Document map

| Doc | Role |
|-----|------|
| [project-plan.md](project-plan.md) | Product + server architecture |
| [firmware-plan.md](firmware-plan.md) | ESP-IDF modules, phases |
| [repo-structure.md](repo-structure.md) | Repository layout |
| [product-description.md](product-description.md) | Hardware specs |
| [server/README.md](../server/README.md) | Cloudflare Worker API |

# Bullerby Chat — UI Specification

This document defines **on-device interface design** for the ESP32-S3 round display
(240×240). Implementation lives in firmware (LVGL); see
[firmware-plan.md](firmware-plan.md).

**Related:** [project-plan.md](project-plan.md) (product scope),
[product-description.md](product-description.md) (hardware).

---

## Design personality (read this first)

This is for **neighborhood kids**, not a hospital kiosk or a bank app.

**Aim for:** **playful**, **wacky**, **colorful**—big energy, silly where it helps, **joy over polish**. Clashing hues, gradients, goofy icons, bouncy motion, and “why not?” flourishes are **features**, not bugs.

**We are not** optimizing for formal **accessibility checklists** (WCAG-style contrast rules, color-blind-only cues, sober motion reduction). If something feels funnier loud and weird, **bias that way**. Use sound, LED, and motion freely when it makes the thing feel alive.

**Still real:** the screen is tiny and the CPU is not a phone—see **§3** for **hardware limits** only (so the UI stays responsive and shippable).

---

## 1. Display and input assumptions

| Constraint | Implication |
|------------|-------------|
| **240×240, circular** | Content is clipped to a disc; corners are invisible. Put the **main action** near the **middle** of the circle so it’s easy to see and poke—not because of standards, but because that’s where the glass is nicest. |
| **Capacitive touch (CST816D)** | Swipes and taps; hardware may report gesture codes—verify on device. |
| **Small device, kid hands** | **Big chunky controls**—not for accessibility audits, but so nobody misses a tap while laughing. |
| **Embedded MCU** | Don’t melt the chip: prefer **simple layouts** and **cheap** animations; save the heavy stuff for rare moments. |

---

## 2. Core pattern: family strip (carousel)

### 2.1 Concept

Families are represented by **round icons** placed on a **single horizontal row**
that extends **beyond the left and right edges** of the visible circle. The user
**pans the strip by swiping** horizontally. Icons that were off-screen **scroll into view**.

The strip is **cyclic (wrapped)**: the family order is a **ring**. Scrolling
continuously in one direction eventually shows the **first family again** after the
last—there is no “end” of the list.

### 2.2 Center focus and scale

- The **screen center** is the **focus point**.
- The icon whose **center is closest** to the screen center is the **focal** (selected) family.
- **Icon diameter scales with distance from center**: **largest at center**, smoothly **smaller** toward the sides (e.g. cosine or bell-shaped falloff—or **exaggerate** the curve for drama).
- Neighbors stay visible but quieter—makes the **center icon** feel like the star of the show.

### 2.3 Interaction

| Action | Expected behaviour |
|--------|---------------------|
| **Horizontal drag** | The strip **tracks the finger** in real time (continuous pan), not only after lift-off. |
| **Ring wrap** | While dragging past a family “step”, the logical index **wraps** on the ring (infinite carousel). |
| **Release** | **Snap** residual offset so the focal icon settles on center (small dead-zone; no extra tutorial copy on screen). |
| **Tap focal (largest) icon** | **Zoom** the **same** focal bubble (reparent + resize overlay on home) to **almost full disc** with **icon-only** controls: **play** (opens **recording** to that family), **stop** (stub until playback exists), **back** (closes overlay). **Ignore** tap if the gesture was a drag (movement threshold). |
| **Tap non-focal icon** | Snap that family to center (direct-select). |

### 2.3.1 Icon-first chrome (home)

The **home** screen is **icons + status only**: family strip, battery, inbox badge. **No** headline or footer **instruction text** (“who?”, “swipe to…”)—navigation must read from **motion and icons** alone. Other flows (recording timer, inbox rows) may still use **short** text where needed until fully iconified.

### 2.4 Broadcast (“All”)

**ALL** is **one icon in the same strip** with **louder** visuals—different color, wilder border, sticker energy. Same carousel rules; tap when focused = **broadcast** record flow.

### 2.5 Layout on a round panel

- Strip **vertically centered** on the disc works well.
- Side icons **clipped by the circle** = peek of “more over there”—good for discovery.
- Keep **battery / back** out of the **very rim** if it’s hard to tap—practical, not moral.

---

## 3. What actually has to behave (embedded reality)

Everything else is taste. This section is **performance and clarity under silicon constraints**.

### 3.1 Don’t confuse the kid mid-flow

- **One obvious main thing per screen** most of the time (who to message? / recording? / listening?)—confusion isn’t “accessible,” it’s just annoying.
- **Recording** should still feel **obviously recording** (big mood, timer, hard to leave by accident).

### 3.2 Touch

- **Big tap areas** and spacing so misses are rare during real play.
- **Snappy feedback** on press (squash, flash, sound)—under ~100 ms **feel** if you can.

### 3.3 Visual design (permission slip)

- **Color:** use **lots** of it—gradients, accents, per-family themes, seasonal nonsense. “Too much” is on the table.
- **Type:** short strings; fun display fonts are welcome if they stay legible **enough** at this size.
- **Icons:** expressive, memorable, a little unhinged is OK.

### 3.4 Feedback and state

- **Recording / playback / new message:** make it **unmissable**—animation, color, LED, chirp, whatever fits the vibe.
- **Battery low:** still warn before death—kids can’t charge it if the thing’s dead.

### 3.5 Errors and connectivity (future)

- Plain words beat error codes. Retry should feel like a **game retry**, not an IT ticket.

### 3.6 Performance (LVGL / MCU)

- Prefer **transforms** over giant full-screen redraws every frame.
- **Short** bouncy animations; **20–30 fps steady** beats stuttery “60 fps” ambition.
- Skip unlimited particles, full-screen blur, or shadow stacks that cost frames.

---

## 4. Screen inventory (target)

| Screen | Purpose | Notes |
|--------|---------|--------|
| **Home** | Family strip (carousel) + status | Focal icon = selected family |
| **Recording** | Record message to selected family / ALL | Timer, stop, cancel—**big personality** |
| **Sending (offline stub)** | Brief confirmation | Can be silly (“Zap!” “Sent!”) |
| **Inbox** | List of messages | Rows can be colorful, not spreadsheet |
| **Playback** | Play message | Progress + back |

Global **status bar**: battery, charging, inbox count—can be **cute** (tiny icons, badges) as long as it reads at a glance.

---

## 5. Explicitly out of scope

Formal **accessibility conformance** (strict contrast ratios, reduced-motion policies, “color may not be the only cue” rules, etc.) is **not a goal** for v1. Optimize for **delight in the neighborhood**. If a tradeoff is **more fun** vs **more compliant**, **choose fun** unless it breaks basic usability (see **§3**).

---

## 6. Open decisions (to resolve in implementation)

- **Tap policy**: **implemented:** tap **focal** opens **family zoom** (large icon + play / stop / back); **play** continues to **recording** for that family; tap **side** snaps to center; **drag** past a movement threshold **cancels** tap.
- **Snap animation** curve and duration—how **rubbery** vs snappy (release snap may be **instant** today).
- **Exact scale function** (min/max diameter, falloff curve)—how **cartoony** the center pop is (current: five fixed diameters 50…98…50 px).
- **Inbox** as full screen vs overlay from home (**full screen** behind same disc layout for now).

### 6.1 Implementation notes (firmware)

- **Layout:** `ui_app.c` uses a **full 240×240** main panel (solid fill, **no** `LV_RADIUS_CIRCLE` on that layer — a circle would leave transparent square corners and read as black arcs on round glass). **Inner padding** (~10 px) keeps the ui-spec safe inset; the hardware round mask crops the framebuffer corners.
- **Strip:** Five **round** slots (−2…+2) with **bell-ish** diameters; horizontal positions use **`s_strip_scroll_px`** for **continuous drag**; **wrap** adjusts `s_carousel_idx` when `|scroll|` crosses half a **step** (~44 px). The strip row is **vertically centered** in the disc (fixed spacer above + flex grow below) with **`STRIP_ROW_H`** ~136 px. **CST816D** gesture bytes are **not** used for carousel pan; pan uses LVGL **`PRESSED` / `PRESSING` / `RELEASED`** on the strip container with **`LV_OBJ_FLAG_EVENT_BUBBLE`** from bubbles.
- **Home chrome (§2.3.1):** No “Who?” / hint labels—only **status** (battery, inbox) + **strip**.
- **Family zoom:** Full-screen overlay on **`lv_display_get_layer_sys()`** (last layer before flush in `lv_refr`, above `act_scr` and `top_layer`). The **focal strip bubble is reparented** into that layer and resized; **play** → recording, **stop** stub, **back** → `zoom_close()`. While zoom is open, the **home screen** is **`LV_OBJ_FLAG_HIDDEN`** so only the overlay draws (no duplicate bubble behind). Pointer indev uses **`lv_indev_set_scroll_throw(..., 0)`** and strip drag **zeros scroll offsets** on the home hierarchy so the flex column does not slide. Home uses a **recursive scroll lock** on the whole home widget tree plus **`LV_EVENT_SCREEN_LOADED`** re-apply so horizontal drag does not scroll the flex column—only `s_strip_scroll_px` moves strip bubbles.
- **Recording / Inbox:** **Column flex** inside the same disc—timer, copy, **Record | Stop** row, **Back**—so controls do not stack on the same coordinates.

### 6.2 Status and challenges (April 2026)

**Where the implementation is (in code, `firmware/main/app/ui_app.c` + `hal/touch.c`):**

- **Home** is a **horizontal family strip** (five fixed slots, cyclic index, continuous `s_strip_scroll_px` pan) with **status** (battery, inbox). Strip is **vertically centered** in the disc (`STRIP_ROW_H`, fixed top spacer + flex below).
- **Touch:** CST816D → LVGL **pointer** indev; **`lv_indev_set_scroll_throw(..., 0)`** to disable momentum scroll on widgets.
- **Scroll isolation:** Recursive **scroll lock** on the home widget tree (`SCROLL_CHAIN_*`, no scroll dirs), **`home_reset_scroll_offsets()`** on strip **PRESSED/PRESSING**, and **`LV_EVENT_SCREEN_LOADED`** re-applies the lock when returning to home.
- **Family zoom:** Focal bubble **reparented** to a **fullscreen panel** on **`lv_display_get_layer_sys()`**; **zoom open** sets **`LV_OBJ_FLAG_HIDDEN`** on **`s_scr_home`** so the menu is not composited under the overlay; **zoom close** restores. **`app_tick`** skips status label updates while zoom is active to avoid invalidating the hidden home screen.
- **Build stamp:** `ESP_LOGI` in `ui_app_init` with `__DATE__` / `__TIME__` so serial logs show which `ui_app.c` build ran.

**Challenges (on-device behaviour not yet aligned with the spec in practice):**

1. **Verification vs binary on chip** — If `idf.py monitor` prints **`Checksum mismatch between flashed and built applications`**, the chip is **not** running `firmware/build/bullerby-chat.bin` from the tree you just built. **Always** `idf.py build` then `idf.py -p PORT flash` from the same `firmware/` checkout before judging UI. See **AGENTS.md** (Development loop / checksum warning).
2. **Strip pan vs whole-column motion** — Intended: only bubble positions change via `s_strip_scroll_px`. **Reported:** the whole home column still appears to move with the finger. Code resets LVGL scroll offsets and disables scroll throw; **if this persists on a verified flash**, next steps are in-device logging (scroll x/y on `s_home_disc` / `s_scr_home` per frame) or a temporary **fullscreen solid color** on `layer_sys` at boot (one-shot) to prove layer compositing.
3. **Zoom / focal tap** — Intended: one large focal bubble on the **sys** layer with home hidden. **Reported:** looks like a **second** circle, **behind** the menu, and **dismisses** after ~1 second. Mitigations in code (hide home, sys layer, no tick invalidation during zoom) **may** still not match glass if the wrong binary is running, or if another subsystem (e.g. **CST816D gesture** + logging, **double-buffer** tearing, or an **LVGL** refresh path on this port) needs investigation.
4. **Hardware vs software gestures** — `touch.c` logs **gesture** register edges; those bytes are **not** wired into carousel logic yet. If the panel reports **swipes** that LVGL maps to **scroll** or **focus** changes, that could interact with custom pan — worth correlating serial gesture logs with repro steps.

**Next engineering steps (suggested):**

- Confirm **no checksum mismatch** after every flash; capture **full** boot log including **`ui_app build stamp`**.
- If issues remain with a **verified** flash: add **short-lived** on-screen **debug** (e.g. Kconfig-gated corner color or scroll values) *or* **ESP_LOGI** throttled scroll positions during drag to separate **LVGL scroll** from **custom strip** pan.
- Optionally profile **CST816D** gesture behaviour (Phase A in **firmware-plan.md**) vs raw coordinates for carousel.

---

## 7. Document map

| Doc | Role |
|-----|------|
| [project-plan.md](project-plan.md) | Product + server architecture (§3) |
| [server/README.md](../server/README.md) | Deployed Worker API (HTTP/WSS); **`npm test`** + optional **`npm run test:e2e`** |
| [firmware-plan.md](firmware-plan.md) | ESP-IDF modules and phases |
| [repo-structure.md](repo-structure.md) | Repository layout |
| [product-description.md](product-description.md) | Hardware |

Strip implementation notes: **§6.1** above and [firmware-plan.md](firmware-plan.md).

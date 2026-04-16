# Server config (git source of truth)

Edit **`bullerby.json`**:

- **`families`** — `id`, `name`, `icon` (stable ids used in firmware and routing).
- **`devices`** — each `id` must match the device’s NVS identity; **`family_id`** ties one device to one family (one device per family).

Commit and **`npm run deploy`** from `server/` so Workers bundle the new JSON.

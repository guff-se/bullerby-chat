/**
 * Shared dev secret injected via `vitest.config.mts` → Miniflare `bindings`.
 * Matches default CONFIG_BULLERBY_DEVICE_SECRET in firmware Kconfig; set the same
 * on the Worker (`BULLERBY_DEVICE_SECRET`).
 */
export const TEST_DEVICE_SECRET = "bullerby-dev-lowsec-a4f91c2e8b70";

/**
 * Shared test-only secret injected via `vitest.config.mts` → Miniflare `bindings`.
 * Do not use in production.
 */
export const TEST_DEVICE_SECRET = "test-secret-ci-only-do-not-use-in-prod";

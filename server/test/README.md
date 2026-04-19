# Server tests

Vitest runs inside the **Workers** runtime via `@cloudflare/vitest-pool-workers` (see [Cloudflare docs](https://developers.cloudflare.com/workers/testing/vitest-integration/)).

- **`auth.test.ts`** — unit tests for `src/auth.ts` + bundled `config` (device-id allowlist).
- **`worker.test.ts`** — integration tests using `exports.default.fetch` from `cloudflare:workers`.

**End-to-end** (WebSocket + multipart POST + download) lives outside Vitest: **`../scripts/e2e-full.mjs`**, invoked via **`npm run test:e2e`** or **`npm run test:e2e:only`** — see the Testing section in **[../README.md](../README.md)**.

When adding routes or auth rules, extend the Vitest tests in the same change; update the E2E script if the client-visible contract changes.

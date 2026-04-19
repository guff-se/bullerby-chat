# Server tests

Vitest runs inside the **Workers** runtime via `@cloudflare/vitest-pool-workers` (see [Cloudflare docs](https://developers.cloudflare.com/workers/testing/vitest-integration/)).

- **`constants.ts`** — dev `BULLERBY_DEVICE_SECRET` for Miniflare; must match `vitest.config.mts` bindings and firmware `CONFIG_BULLERBY_DEVICE_SECRET` default.
- **`auth.test.ts`** — unit tests for `src/auth.ts` + bundled `config`.
- **`worker.test.ts`** — integration tests using `exports.default.fetch` from `cloudflare:workers`.

**End-to-end** (WebSocket + multipart POST + signed download) lives outside Vitest: **`../scripts/e2e-full.mjs`**, invoked via **`npm run test:e2e`** or **`npm run test:e2e:only`** — see the Testing section in **[../README.md](../README.md)**.

When adding routes or auth rules, extend the Vitest tests in the same change; update the E2E script if the client-visible contract changes.

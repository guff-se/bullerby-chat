import { cloudflareTest } from "@cloudflare/vitest-pool-workers";
import { defineConfig } from "vitest/config";
import { TEST_DEVICE_SECRET } from "./test/constants";

export default defineConfig({
  plugins: [
    cloudflareTest({
      wrangler: {
        configPath: "./wrangler.toml",
      },
      miniflare: {
        bindings: {
          BULLERBY_DEVICE_SECRET: TEST_DEVICE_SECRET,
        },
      },
    }),
  ],
  test: {
    include: ["test/**/*.test.ts"],
  },
});

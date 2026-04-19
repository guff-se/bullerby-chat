/**
 * Starts `wrangler dev` (unless E2E_SKIP_SERVER=1), runs scripts/e2e-full.mjs.
 *
 * Env:
 *   E2E_SKIP_SERVER=1 — do not start wrangler; use E2E_BASE_URL
 *   E2E_BASE_URL — default http://127.0.0.1:8787
 */
import { spawn } from "node:child_process";
import { once } from "node:events";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const serverDir = join(dirname(fileURLToPath(import.meta.url)), "..");
const e2eModule = new URL("./e2e-full.mjs", import.meta.url);

async function waitForHealth(baseUrl, timeoutMs = 120_000) {
  const origin = baseUrl.replace(/\/$/, "");
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const r = await fetch(`${origin}/health`);
      if (r.ok) return;
    } catch {
      /* retry */
    }
    await new Promise((r) => setTimeout(r, 200));
  }
  throw new Error("health check timeout — is wrangler dev running?");
}

async function main() {
  const skip = process.env.E2E_SKIP_SERVER === "1";
  const baseUrl = process.env.E2E_BASE_URL ?? "http://127.0.0.1:8787";

  if (skip) {
    const { runE2e } = await import(e2eModule.href);
    await runE2e({ baseUrl });
    console.log("e2e OK (existing server)");
    return;
  }

  const child = spawn("npx", ["wrangler", "dev", "--port", "8787"], {
    cwd: serverDir,
    stdio: "inherit",
  });

  let exited = false;
  child.on("exit", () => {
    exited = true;
  });
  child.on("error", (err) => {
    console.error("wrangler spawn error:", err);
  });

  try {
    await waitForHealth(baseUrl);
    process.env.E2E_BASE_URL = baseUrl;
    const { runE2e } = await import(e2eModule.href);
    await runE2e({ baseUrl });
    console.log("e2e OK");
  } finally {
    if (!exited) {
      child.kill("SIGINT");
      const deadline = Date.now() + 8000;
      while (!exited && Date.now() < deadline) {
        await new Promise((r) => setTimeout(r, 100));
      }
      if (!exited) {
        child.kill("SIGKILL");
        try {
          await once(child, "exit");
        } catch {
          /* ignore */
        }
      }
    }
  }
}

main().catch((e) => {
  console.error(e.message || e);
  process.exit(1);
});

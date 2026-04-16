/**
 * Starts `wrangler dev` (unless E2E_SKIP_SERVER=1), runs scripts/e2e-full.mjs, restores .dev.vars.
 *
 * Env:
 *   E2E_SKIP_SERVER=1 — do not start wrangler; use E2E_BASE_URL + BULLERBY_DEVICE_SECRET
 *   E2E_BASE_URL — default http://127.0.0.1:8787
 *   BULLERBY_DEVICE_SECRET — when E2E_SKIP_SERVER=1, required
 */
import { spawn } from "node:child_process";
import { once } from "node:events";
import { randomBytes } from "node:crypto";
import { copyFile, writeFile, unlink, rename } from "node:fs/promises";
import { existsSync } from "node:fs";
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
    if (!process.env.BULLERBY_DEVICE_SECRET) {
      throw new Error("E2E_SKIP_SERVER=1 requires BULLERBY_DEVICE_SECRET");
    }
    const { runE2e } = await import(e2eModule.href);
    await runE2e({ baseUrl });
    console.log("e2e OK (existing server)");
    return;
  }

  const secret = process.env.BULLERBY_DEVICE_SECRET ?? randomBytes(24).toString("hex");
  const devVarsPath = join(serverDir, ".dev.vars");
  const backupPath = join(serverDir, ".dev.vars.e2e-backup");
  const hadExisting = existsSync(devVarsPath);

  if (hadExisting) {
    await copyFile(devVarsPath, backupPath);
  }
  await writeFile(devVarsPath, `BULLERBY_DEVICE_SECRET=${secret}\n`, "utf8");

  const child = spawn("npx", ["wrangler", "dev", "--port", "8787"], {
    cwd: serverDir,
    stdio: "inherit",
    env: { ...process.env, BULLERBY_DEVICE_SECRET: secret },
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
    process.env.BULLERBY_DEVICE_SECRET = secret;
    process.env.E2E_BASE_URL = baseUrl;
    const { runE2e } = await import(e2eModule.href);
    await runE2e({ baseUrl, secret });
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

    if (hadExisting) {
      await rename(backupPath, devVarsPath);
    } else {
      try {
        await unlink(devVarsPath);
      } catch {
        /* ignore */
      }
    }
  }
}

main().catch((e) => {
  console.error(e.message || e);
  process.exit(1);
});

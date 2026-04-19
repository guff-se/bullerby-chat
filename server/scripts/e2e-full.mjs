/**
 * End-to-end: WebSocket (receiver) + multipart POST (sender) + audio GET.
 * Expects a running worker (e.g. wrangler dev).
 *
 * Env:
 *   E2E_BASE_URL — default http://127.0.0.1:8787
 */
import { fileURLToPath } from "node:url";
import path from "node:path";
import WebSocket from "ws";

const SENDER_ID = "esp-3c0f02ddeec04";
const RECEIVER_ID = "device-uuid-002";
const TARGET_FAMILY = "family-b";

function waitForJson(ws, predicate, timeoutMs, label) {
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => {
      ws.removeListener("message", onMessage);
      reject(new Error(`timeout waiting for ${label} (${timeoutMs}ms)`));
    }, timeoutMs);
    function onMessage(data) {
      let j;
      try {
        j = JSON.parse(data.toString());
      } catch {
        return;
      }
      if (predicate(j)) {
        clearTimeout(t);
        ws.removeListener("message", onMessage);
        resolve(j);
      }
    }
    ws.on("message", onMessage);
  });
}

export async function runE2e(options) {
  const baseUrl = (options?.baseUrl ?? process.env.E2E_BASE_URL ?? "http://127.0.0.1:8787").replace(
    /\/$/,
    ""
  );

  const origin = new URL(baseUrl).origin;
  const wsUrl = origin.replace(/^http/, "ws") + "/api/ws";
  const headers = { "X-Device-Id": RECEIVER_ID };

  const mockAudio = new Uint8Array([
    0x52, 0x49, 0x46, 0x46, 0x0c, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45,
  ]);

  const ws = new WebSocket(wsUrl, { headers });
  const connectedP = waitForJson(ws, (j) => j.type === "connected", 10_000, "connected");

  await new Promise((resolve, reject) => {
    ws.once("open", resolve);
    ws.once("error", reject);
  });

  try {
    await connectedP;

    const newMessageP = waitForJson(ws, (j) => j.type === "new_message", 15_000, "new_message");

    const form = new FormData();
    form.append("audio", new Blob([mockAudio], { type: "application/octet-stream" }), "mock.bin");
    form.append(
      "metadata",
      JSON.stringify({ to_family_id: TARGET_FAMILY, duration_s: 1.25 })
    );

    const postRes = await fetch(`${origin}/api/messages`, {
      method: "POST",
      headers: { "X-Device-Id": SENDER_ID },
      body: form,
    });

    if (!postRes.ok) {
      const errText = await postRes.text();
      throw new Error(`POST /api/messages failed ${postRes.status}: ${errText}`);
    }

    const postJson = await postRes.json();
    if (!postJson.ok || !postJson.message_id) {
      throw new Error(`unexpected POST body: ${JSON.stringify(postJson)}`);
    }

    const incoming = await newMessageP;
    if (incoming.message_id !== postJson.message_id) {
      throw new Error(
        `message_id mismatch: ws ${incoming.message_id} vs post ${postJson.message_id}`
      );
    }

    if (incoming.download_url == null || typeof incoming.download_url !== "string") {
      throw new Error(`new_message missing download_url: ${JSON.stringify(incoming)}`);
    }

    const dl = await fetch(incoming.download_url);
    if (!dl.ok) {
      const t = await dl.text();
      throw new Error(`GET download_url failed ${dl.status}: ${t}`);
    }

    const got = new Uint8Array(await dl.arrayBuffer());
    if (got.byteLength !== mockAudio.byteLength) {
      throw new Error(`byte length mismatch: got ${got.byteLength}, expected ${mockAudio.byteLength}`);
    }
    for (let i = 0; i < mockAudio.byteLength; i++) {
      if (got[i] !== mockAudio[i]) {
        throw new Error(`byte mismatch at ${i}: got ${got[i]}, expected ${mockAudio[i]}`);
      }
    }

    return {
      message_id: postJson.message_id,
      from_family_id: incoming.from_family_id,
      duration_s: incoming.duration_s,
    };
  } finally {
    ws.close();
  }
}

const __filename = fileURLToPath(import.meta.url);
if (path.resolve(process.argv[1] ?? "") === __filename) {
  runE2e()
    .then((r) => {
      console.log("e2e OK:", r);
      process.exit(0);
    })
    .catch((e) => {
      console.error("e2e FAILED:", e.message);
      process.exit(1);
    });
}

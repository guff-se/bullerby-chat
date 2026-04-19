import { RelayRoom } from "./durable/relay-room";
import { config, deviceById } from "./config";
import { verifyDeviceAuth, verifyDownloadQuery } from "./auth";
import type { MessageMetadata, PostMessagePayload } from "./types";

export { RelayRoom };

const RELAY_INTERNAL = "https://relay.internal";

function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}

async function handlePostMessage(
  request: Request,
  deviceId: string,
  stub: DurableObjectStub
): Promise<Response> {
  const dev = deviceById(deviceId);
  if (!dev) {
    return Response.json({ error: "unknown_device" }, { status: 400 });
  }

  const ct = request.headers.get("content-type") || "";
  if (!ct.includes("multipart/form-data")) {
    return Response.json({ error: "expected_multipart" }, { status: 400 });
  }

  const form = await request.formData();
  const audio = form.get("audio");
  const metaRaw = form.get("metadata");
  if (typeof metaRaw !== "string" || !(audio instanceof File)) {
    return Response.json({ error: "missing_audio_or_metadata" }, { status: 400 });
  }

  let metadata: MessageMetadata;
  try {
    metadata = JSON.parse(metaRaw) as MessageMetadata;
  } catch {
    return Response.json({ error: "invalid_metadata_json" }, { status: 400 });
  }

  const buf = await audio.arrayBuffer();
  const bytes = new Uint8Array(buf);
  if (bytes.byteLength > 128 * 1024) {
    return Response.json({ error: "audio_too_large" }, { status: 413 });
  }

  const toFamilyId = metadata.to_family_id ?? null;
  const broadcast =
    toFamilyId === null ||
    toFamilyId === "" ||
    toFamilyId === "ALL" ||
    toFamilyId === "broadcast";

  const origin = new URL(request.url).origin;

  const payload: PostMessagePayload = {
    from_device_id: deviceId,
    from_family_id: dev.family_id,
    to_family_id: broadcast ? null : toFamilyId,
    broadcast,
    duration_s: typeof metadata.duration_s === "number" ? metadata.duration_s : 0,
    sample_rate_hz:
      typeof metadata.sample_rate_hz === "number" && metadata.sample_rate_hz > 0
        ? metadata.sample_rate_hz
        : 16000,
    audio_base64: bytesToBase64(bytes),
    public_origin: origin,
  };

  return stub.fetch(
    new Request(`${RELAY_INTERNAL}/internal/post-message`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    })
  );
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    const relayId = env.RELAY.idFromName("global");
    const stub = env.RELAY.get(relayId);

    if (url.pathname === "/health" || url.pathname === "/") {
      return Response.json({
        ok: true,
        service: "bullerby-chat",
        api: {
          ws: "/api/ws",
          config: "GET /api/devices/{id}/config",
          messages: "POST /api/messages",
        },
      });
    }

    if (url.pathname === "/api/ws") {
      const auth = verifyDeviceAuth(request, env, config);
      if (!auth) {
        return new Response("Unauthorized", { status: 401 });
      }
      return stub.fetch(request);
    }

    if (url.pathname === "/api/devices/register" && request.method === "POST") {
      const auth = verifyDeviceAuth(request, env, config);
      if (!auth) {
        return new Response("Unauthorized", { status: 401 });
      }
      const dev = deviceById(auth.deviceId);
      return Response.json({
        ok: true,
        device_id: auth.deviceId,
        family_id: dev?.family_id,
      });
    }

    const configMatch = /^\/api\/devices\/([^/]+)\/config$/.exec(url.pathname);
    if (configMatch && request.method === "GET") {
      const auth = verifyDeviceAuth(request, env, config);
      if (!auth) {
        return new Response("Unauthorized", { status: 401 });
      }
      const requestedId = decodeURIComponent(configMatch[1]!);
      if (requestedId !== auth.deviceId) {
        return new Response("Forbidden", { status: 403 });
      }
      const dev = deviceById(auth.deviceId);
      if (!dev) {
        return new Response("Not found", { status: 404 });
      }
      return Response.json({
        device_id: dev.id,
        family_id: dev.family_id,
        families: config.families,
      });
    }

    if (url.pathname === "/api/messages" && request.method === "POST") {
      const auth = verifyDeviceAuth(request, env, config);
      if (!auth) {
        return new Response("Unauthorized", { status: 401 });
      }
      return handlePostMessage(request, auth.deviceId, stub);
    }

    const audioMatch = /^\/api\/messages\/([^/]+)\/audio$/.exec(url.pathname);
    if (audioMatch && request.method === "GET") {
      const messageId = decodeURIComponent(audioMatch[1]!);
      const exp = url.searchParams.get("exp");
      const sig = url.searchParams.get("sig");
      const ok = await verifyDownloadQuery(messageId, exp, sig, env);
      if (!ok) {
        return new Response("Unauthorized", { status: 401 });
      }
      return stub.fetch(
        new Request(
          `${RELAY_INTERNAL}/internal/audio/${encodeURIComponent(messageId)}`,
          { method: "GET" }
        )
      );
    }

    return new Response("Not found", { status: 404 });
  },
} satisfies ExportedHandler<Env>;

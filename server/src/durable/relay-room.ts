import { DurableObject } from "cloudflare:workers";
import { config, deviceById, deviceIdForFamily, familyById } from "../config";
import { signDownloadToken } from "../auth";
import type { PostMessagePayload } from "../types";

const TTL_MS = 10 * 60 * 1000;
const RETRY_MS = 2 * 60 * 1000;
/** Initial send + retries: 3 waves total, then drop undelivered. */
const MAX_WAVES = 3;

interface PendingMessage {
  id: string;
  audio: Uint8Array;
  from_family_id: string;
  pending_device_ids: string[];
  duration_s: number;
  sample_rate_hz: number;
  expires_at: number;
  public_origin: string;
  /** 1 = first delivery (post); 2 and 3 = alarm retries. */
  delivery_wave: number;
}

export class RelayRoom extends DurableObject {
  private sockets = new Map<string, WebSocket>();
  private pending = new Map<string, PendingMessage>();

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);

    if (request.headers.get("Upgrade") === "websocket") {
      return this.handleWebSocket(request);
    }

    if (url.pathname === "/internal/post-message" && request.method === "POST") {
      return this.handlePostMessage(request);
    }

    if (url.pathname.startsWith("/internal/audio/") && request.method === "GET") {
      return this.handleInternalAudio(request);
    }

    return new Response("Not found", { status: 404 });
  }

  private handleWebSocket(request: Request): Response {
    const deviceId = request.headers.get("X-Device-Id")?.trim();
    if (!deviceId || !deviceById(deviceId)) {
      return new Response("Unauthorized", { status: 401 });
    }

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.ctx.acceptWebSocket(server);
    this.sockets.set(deviceId, server);

    server.send(
      JSON.stringify({
        type: "connected",
        device_id: deviceId,
      })
    );

    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketClose(ws: WebSocket): Promise<void> {
    for (const [id, s] of this.sockets) {
      if (s === ws) {
        this.sockets.delete(id);
        break;
      }
    }
  }

  async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): Promise<void> {
    const text =
      typeof message === "string" ? message : new TextDecoder().decode(message);
    let parsed: { type?: string } = {};
    try {
      parsed = JSON.parse(text) as { type?: string };
    } catch {
      return;
    }
    if (parsed.type === "heartbeat") {
      ws.send(JSON.stringify({ type: "heartbeat_ack", t: Date.now() }));
    }
  }

  private async handlePostMessage(request: Request): Promise<Response> {
    let body: PostMessagePayload;
    try {
      body = (await request.json()) as PostMessagePayload;
    } catch {
      return Response.json({ error: "invalid_json" }, { status: 400 });
    }

    const fromDev = deviceById(body.from_device_id);
    if (!fromDev || fromDev.family_id !== body.from_family_id) {
      return Response.json({ error: "sender_mismatch" }, { status: 400 });
    }

    let bytes: Uint8Array;
    try {
      const raw = atob(body.audio_base64);
      bytes = new Uint8Array(raw.length);
      for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
    } catch {
      return Response.json({ error: "invalid_audio_base64" }, { status: 400 });
    }

    if (bytes.byteLength > 128 * 1024) {
      return Response.json({ error: "audio_too_large" }, { status: 413 });
    }

    const messageId = crypto.randomUUID();
    const now = Date.now();
    const expires_at = now + TTL_MS;

    const pending_device_ids = this.resolveTargets(
      body.from_family_id,
      body.broadcast,
      body.to_family_id
    );
    if (pending_device_ids.length === 0) {
      return Response.json({ error: "no_recipients" }, { status: 400 });
    }

    const msg: PendingMessage = {
      id: messageId,
      audio: bytes,
      from_family_id: body.from_family_id,
      pending_device_ids,
      duration_s: body.duration_s,
      sample_rate_hz: body.sample_rate_hz > 0 ? body.sample_rate_hz : 16000,
      expires_at,
      public_origin: body.public_origin.replace(/\/$/, ""),
      delivery_wave: 1,
    };

    this.pending.set(messageId, msg);
    await this.deliverRound(msg);

    if (msg.pending_device_ids.length > 0) {
      if (msg.delivery_wave >= MAX_WAVES) {
        this.pending.delete(messageId);
      }
    }
    await this.recomputeAlarm();

    return Response.json({
      ok: true,
      message_id: messageId,
      undelivered: msg.pending_device_ids.length,
    });
  }

  private resolveTargets(
    fromFamilyId: string,
    broadcast: boolean,
    toFamilyId: string | null
  ): string[] {
    const out: string[] = [];
    if (broadcast) {
      for (const f of config.families) {
        if (f.id === fromFamilyId) continue;
        const did = deviceIdForFamily(f.id);
        if (did) out.push(did);
      }
      return out;
    }
    if (!toFamilyId || toFamilyId === fromFamilyId) return [];
    if (!familyById(toFamilyId)) return [];
    const did = deviceIdForFamily(toFamilyId);
    return did ? [did] : [];
  }

  private async deliverRound(msg: PendingMessage): Promise<void> {
    const secret = (this.env as { BULLERBY_DEVICE_SECRET?: string }).BULLERBY_DEVICE_SECRET;
    if (!secret) return;

    const expUnix = Math.floor(msg.expires_at / 1000);
    const { exp, sig } = await signDownloadToken(msg.id, expUnix, secret);
    const downloadUrl = `${msg.public_origin}/api/messages/${encodeURIComponent(msg.id)}/audio?exp=${encodeURIComponent(exp)}&sig=${encodeURIComponent(sig)}`;

    const remaining: string[] = [];
    for (const deviceId of msg.pending_device_ids) {
      const ws = this.sockets.get(deviceId);
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(
          JSON.stringify({
            type: "new_message",
            message_id: msg.id,
            from_family_id: msg.from_family_id,
            duration_s: msg.duration_s,
            sample_rate_hz: msg.sample_rate_hz,
            download_url: downloadUrl,
          })
        );
      } else {
        remaining.push(deviceId);
      }
    }
    msg.pending_device_ids = remaining;
  }

  /**
   * Single alarm per DO: wake at the earliest of (retry for undelivered, TTL expiry for any message).
   * Keeps delivered messages in `pending` until `expires_at` so signed download URLs keep working.
   */
  private async recomputeAlarm(): Promise<void> {
    const now = Date.now();
    let earliest: number | null = null;
    for (const [, msg] of this.pending) {
      if (msg.expires_at >= now) {
        earliest = earliest === null ? msg.expires_at : Math.min(earliest, msg.expires_at);
      }
      if (msg.pending_device_ids.length > 0 && msg.delivery_wave < MAX_WAVES) {
        const retryAt = now + RETRY_MS;
        earliest = earliest === null ? retryAt : Math.min(earliest, retryAt);
      }
    }
    if (earliest === null) {
      await this.ctx.storage.deleteAlarm();
      return;
    }
    await this.ctx.storage.setAlarm(earliest);
  }

  async alarm(): Promise<void> {
    const now = Date.now();

    for (const [id, msg] of [...this.pending]) {
      if (msg.expires_at < now) {
        this.pending.delete(id);
        continue;
      }

      msg.delivery_wave++;
      if (msg.delivery_wave > MAX_WAVES) {
        this.pending.delete(id);
        continue;
      }

      await this.deliverRound(msg);

      if (msg.pending_device_ids.length > 0 && msg.delivery_wave >= MAX_WAVES) {
        this.pending.delete(id);
      }
    }

    await this.recomputeAlarm();
  }

  private async handleInternalAudio(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const parts = url.pathname.split("/").filter(Boolean);
    const messageId = parts.length >= 3 ? decodeURIComponent(parts[2] ?? "") : "";
    if (!messageId) {
      return new Response("Not found", { status: 404 });
    }

    const msg = this.pending.get(messageId);
    if (!msg) {
      return new Response("Gone", { status: 410 });
    }

    return new Response(msg.audio, {
      headers: {
        "Content-Type": "application/octet-stream",
        "Cache-Control": "private, no-store",
      },
    });
  }
}

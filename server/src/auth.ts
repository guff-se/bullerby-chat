import type { BullerbyConfig } from "./types";

/** Constant-time string compare (same length). */
function secureCompare(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let out = 0;
  for (let i = 0; i < a.length; i++) {
    out |= a.charCodeAt(i) ^ b.charCodeAt(i);
  }
  return out === 0;
}

async function hmacHex(secret: string, data: string): Promise<string> {
  const enc = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    enc.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"]
  );
  const sig = await crypto.subtle.sign("HMAC", key, enc.encode(data));
  return [...new Uint8Array(sig)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

/**
 * Validates `Authorization: Bearer <token>` against `BULLERBY_DEVICE_SECRET`
 * and checks `X-Device-Id` is in the config allowlist.
 */
export function verifyDeviceAuth(
  request: Request,
  env: Env,
  cfg: BullerbyConfig
): { deviceId: string } | null {
  const deviceId = request.headers.get("X-Device-Id")?.trim();
  if (!deviceId) return null;

  const auth = request.headers.get("Authorization");
  if (!auth?.startsWith("Bearer ")) return null;
  const token = auth.slice("Bearer ".length).trim();

  const secret = env.BULLERBY_DEVICE_SECRET;
  if (!secret || secret.length === 0) {
    return null;
  }
  if (!secureCompare(token, secret)) return null;

  const allowed = cfg.devices.some((d) => d.id === deviceId);
  if (!allowed) return null;

  return { deviceId };
}

export async function verifyDownloadQuery(
  messageId: string,
  exp: string | null,
  sig: string | null,
  env: Env
): Promise<boolean> {
  const secret = env.BULLERBY_DEVICE_SECRET;
  if (!secret || !exp || !sig) return false;
  const expNum = parseInt(exp, 10);
  if (Number.isNaN(expNum) || Date.now() / 1000 > expNum) return false;
  const expected = await hmacHex(secret, `${messageId}.${exp}`);
  return secureCompare(sig, expected);
}

export async function signDownloadToken(
  messageId: string,
  expUnix: number,
  secret: string
): Promise<{ exp: string; sig: string }> {
  const exp = String(expUnix);
  const sig = await hmacHex(secret, `${messageId}.${exp}`);
  return { exp, sig };
}

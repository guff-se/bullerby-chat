import type { BullerbyConfig } from "./types";

/**
 * Checks `X-Device-Id` is in the config allowlist. This is intentionally
 * trivial — security is not a priority for this project.
 */
export function verifyDeviceAuth(
  request: Request,
  cfg: BullerbyConfig
): { deviceId: string } | null {
  const deviceId =
    request.headers.get("X-Device-Id")?.trim() ||
    new URL(request.url).searchParams.get("device_id")?.trim() ||
    "";
  if (!deviceId) return null;
  if (!cfg.devices.some((d) => d.id === deviceId)) return null;
  return { deviceId };
}

import type { BullerbyConfig, DeviceConfig } from "./types";
import { config as bundled } from "./config";

const KV_KEY = "devices";

/** Read devices from KV, falling back to bundled JSON. */
export async function getDevices(kv: KVNamespace): Promise<DeviceConfig[]> {
  const raw = await kv.get(KV_KEY);
  if (raw) {
    try {
      return JSON.parse(raw) as DeviceConfig[];
    } catch {
      // fall through to bundled
    }
  }
  return bundled.devices;
}

/** Persist devices to KV. Throws on invalid input. */
export async function putDevices(kv: KVNamespace, devices: unknown): Promise<DeviceConfig[]> {
  if (!Array.isArray(devices)) throw new Error("devices must be an array");
  const ids = new Set<string>();
  for (const d of devices as DeviceConfig[]) {
    if (!d.id || typeof d.id !== "string") throw new Error("each device needs an id");
    if (!d.name || typeof d.name !== "string") throw new Error("each device needs a name");
    if (!d.icon || typeof d.icon !== "string") throw new Error("each device needs an icon");
    if (ids.has(d.id)) throw new Error(`duplicate device id: ${d.id}`);
    ids.add(d.id);
  }
  const validated = (devices as DeviceConfig[]).map((d) => ({
    id: d.id.trim(),
    name: d.name.trim(),
    icon: [...d.icon.trim()].slice(0, 2).join(""),
  }));
  await kv.put(KV_KEY, JSON.stringify(validated));
  return validated;
}

export async function getConfig(kv: KVNamespace): Promise<BullerbyConfig> {
  return { devices: await getDevices(kv) };
}

import type { BullerbyConfig, FamilyConfig } from "./types";
import { config as bundled } from "./config";

const KV_KEY = "families";

/** Read families from KV, falling back to bundled JSON. */
export async function getFamilies(kv: KVNamespace): Promise<FamilyConfig[]> {
  const raw = await kv.get(KV_KEY);
  if (raw) {
    try {
      return JSON.parse(raw) as FamilyConfig[];
    } catch {
      // fall through to bundled
    }
  }
  return bundled.families;
}

/** Persist families to KV. Throws on invalid input. */
export async function putFamilies(kv: KVNamespace, families: unknown): Promise<FamilyConfig[]> {
  if (!Array.isArray(families)) throw new Error("families must be an array");
  const ids = new Set<string>();
  for (const f of families as FamilyConfig[]) {
    if (!f.id || typeof f.id !== "string") throw new Error("each family needs an id");
    if (!f.name || typeof f.name !== "string") throw new Error("each family needs a name");
    if (!f.icon || typeof f.icon !== "string") throw new Error("each family needs an icon");
    if (ids.has(f.id)) throw new Error(`duplicate family id: ${f.id}`);
    ids.add(f.id);
  }
  const validated = (families as FamilyConfig[]).map((f) => ({
    id: f.id.trim(),
    name: f.name.trim(),
    icon: f.icon.trim().slice(0, 2),
  }));
  await kv.put(KV_KEY, JSON.stringify(validated));
  return validated;
}

/** Full config, merging KV families with bundled devices. */
export async function getConfig(kv: KVNamespace): Promise<BullerbyConfig> {
  return { families: await getFamilies(kv), devices: bundled.devices };
}

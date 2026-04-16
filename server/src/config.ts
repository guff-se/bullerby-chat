import type { BullerbyConfig } from "./types";
import raw from "../config/bullerby.json";

function validate(cfg: BullerbyConfig): void {
  const familyIds = new Set<string>();
  for (const f of cfg.families) {
    if (!f.id || familyIds.has(f.id)) throw new Error(`Invalid family id: ${f.id}`);
    familyIds.add(f.id);
  }
  const deviceIds = new Set<string>();
  for (const d of cfg.devices) {
    if (!d.id || deviceIds.has(d.id)) throw new Error(`Invalid device id: ${d.id}`);
    deviceIds.add(d.id);
    if (!familyIds.has(d.family_id)) {
      throw new Error(`Device ${d.id} references unknown family ${d.family_id}`);
    }
  }
}

const parsed = raw as BullerbyConfig;
validate(parsed);

/** Static config bundled at deploy time. */
export const config: BullerbyConfig = parsed;

export function familyById(id: string): BullerbyConfig["families"][0] | undefined {
  return config.families.find((f) => f.id === id);
}

export function deviceById(id: string): BullerbyConfig["devices"][0] | undefined {
  return config.devices.find((d) => d.id === id);
}

export function deviceIdForFamily(familyId: string): string | undefined {
  return config.devices.find((d) => d.family_id === familyId)?.id;
}

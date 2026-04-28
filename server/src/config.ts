import type { BullerbyConfig, DeviceConfig } from "./types";
import raw from "../config/bullerby.json";

function validate(cfg: BullerbyConfig): void {
  const ids = new Set<string>();
  for (const d of cfg.devices) {
    if (!d.id || ids.has(d.id)) throw new Error(`Invalid/duplicate device id: ${d.id}`);
    if (!d.name) throw new Error(`Device ${d.id} missing name`);
    if (!d.icon) throw new Error(`Device ${d.id} missing icon`);
    ids.add(d.id);
  }
}

const parsed = raw as BullerbyConfig;
validate(parsed);

/** Static config bundled at deploy time. */
export const config: BullerbyConfig = parsed;

export function deviceById(id: string): DeviceConfig | undefined {
  return config.devices.find((d) => d.id === id);
}

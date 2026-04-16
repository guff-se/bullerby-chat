/** Bundled git-sourced config (see docs/project-plan.md §3.4). */
export interface FamilyConfig {
  id: string;
  name: string;
  icon: string;
}

export interface DeviceConfig {
  id: string;
  family_id: string;
}

export interface BullerbyConfig {
  families: FamilyConfig[];
  devices: DeviceConfig[];
}

/** Multipart metadata for POST /api/messages */
export interface MessageMetadata {
  /** Target family id, or omit / null for broadcast (all except sender). */
  to_family_id?: string | null;
  /** Duration in seconds (optional, for UI). */
  duration_s?: number;
}

export interface PostMessagePayload {
  from_device_id: string;
  from_family_id: string;
  to_family_id: string | null;
  broadcast: boolean;
  duration_s: number;
  audio_base64: string;
  /** Origin for building absolute download URLs (no trailing slash). */
  public_origin: string;
}

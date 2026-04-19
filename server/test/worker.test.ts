import { describe, it, expect } from "vitest";
import { exports } from "cloudflare:workers";

describe("Worker HTTP (integration)", () => {
  it("GET /health returns 200 and ok", async () => {
    const res = await exports.default.fetch(
      new Request("https://bullerby.test/health")
    );
    expect(res.status).toBe(200);
    const body = (await res.json()) as { ok: boolean; service: string };
    expect(body.ok).toBe(true);
    expect(body.service).toBe("bullerby-chat");
  });

  it("GET /api/devices/.../config without device id is 401", async () => {
    const res = await exports.default.fetch(
      new Request("https://bullerby.test/api/devices/device-uuid-001/config")
    );
    expect(res.status).toBe(401);
  });

  it("GET /api/devices/.../config with X-Device-Id returns families", async () => {
    const res = await exports.default.fetch(
      new Request("https://bullerby.test/api/devices/device-uuid-001/config", {
        headers: { "X-Device-Id": "device-uuid-001" },
      })
    );
    expect(res.status).toBe(200);
    const body = (await res.json()) as {
      device_id: string;
      family_id: string;
      families: { id: string }[];
    };
    expect(body.device_id).toBe("device-uuid-001");
    expect(body.family_id).toBe("family-a");
    expect(body.families.length).toBeGreaterThan(0);
  });

  it("POST /api/devices/register with X-Device-Id returns ok", async () => {
    const res = await exports.default.fetch(
      new Request("https://bullerby.test/api/devices/register", {
        method: "POST",
        headers: { "X-Device-Id": "device-uuid-002" },
      })
    );
    expect(res.status).toBe(200);
    const body = (await res.json()) as { ok: boolean; family_id: string };
    expect(body.ok).toBe(true);
    expect(body.family_id).toBe("family-b");
  });
});

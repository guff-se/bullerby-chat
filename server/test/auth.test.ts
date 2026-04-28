import { describe, it, expect } from "vitest";
import { verifyDeviceAuth } from "../src/auth";
import { config } from "../src/config";

describe("verifyDeviceAuth", () => {
  it("accepts allowlisted device via X-Device-Id header", () => {
    const req = new Request("https://example.com/api/x", {
      headers: { "X-Device-Id": "esp-3c0f02ddec04" },
    });
    expect(verifyDeviceAuth(req, config)?.deviceId).toBe("esp-3c0f02ddec04");
  });

  it("accepts allowlisted device via ?device_id query", () => {
    const req = new Request("https://example.com/api/ws?device_id=device-uuid-002");
    expect(verifyDeviceAuth(req, config)?.deviceId).toBe("device-uuid-002");
  });

  it("rejects missing device id", () => {
    const req = new Request("https://example.com/");
    expect(verifyDeviceAuth(req, config)).toBeNull();
  });

  it("rejects unknown device id", () => {
    const req = new Request("https://example.com/", {
      headers: { "X-Device-Id": "not-in-config" },
    });
    expect(verifyDeviceAuth(req, config)).toBeNull();
  });
});

import { describe, it, expect } from "vitest";
import { verifyDeviceAuth, signDownloadToken, verifyDownloadQuery } from "../src/auth";
import { config } from "../src/config";
import { TEST_DEVICE_SECRET } from "./constants";

const TEST_SECRET = TEST_DEVICE_SECRET;

function mockEnv(secret: string | undefined): Env {
  return { BULLERBY_DEVICE_SECRET: secret } as Env;
}

describe("verifyDeviceAuth", () => {
  it("accepts allowlisted device with valid bearer", () => {
    const req = new Request("https://example.com/api/x", {
      headers: {
        Authorization: `Bearer ${TEST_SECRET}`,
        "X-Device-Id": "device-uuid-001",
      },
    });
    const out = verifyDeviceAuth(req, mockEnv(TEST_SECRET), config);
    expect(out?.deviceId).toBe("device-uuid-001");
  });

  it("rejects missing X-Device-Id", () => {
    const req = new Request("https://example.com/", {
      headers: { Authorization: `Bearer ${TEST_SECRET}` },
    });
    expect(verifyDeviceAuth(req, mockEnv(TEST_SECRET), config)).toBeNull();
  });

  it("rejects wrong secret", () => {
    const req = new Request("https://example.com/", {
      headers: {
        Authorization: "Bearer wrong",
        "X-Device-Id": "device-uuid-001",
      },
    });
    expect(verifyDeviceAuth(req, mockEnv(TEST_SECRET), config)).toBeNull();
  });

  it("rejects unknown device id", () => {
    const req = new Request("https://example.com/", {
      headers: {
        Authorization: `Bearer ${TEST_SECRET}`,
        "X-Device-Id": "not-in-config",
      },
    });
    expect(verifyDeviceAuth(req, mockEnv(TEST_SECRET), config)).toBeNull();
  });

  it("rejects empty BULLERBY_DEVICE_SECRET", () => {
    const req = new Request("https://example.com/", {
      headers: {
        Authorization: "Bearer x",
        "X-Device-Id": "device-uuid-001",
      },
    });
    expect(verifyDeviceAuth(req, mockEnv(undefined), config)).toBeNull();
  });
});

describe("download token", () => {
  it("sign + verify roundtrip", async () => {
    const msgId = "550e8400-e29b-41d4-a716-446655440000";
    const expUnix = Math.floor(Date.now() / 1000) + 3600;
    const { exp, sig } = await signDownloadToken(msgId, expUnix, TEST_SECRET);
    const ok = await verifyDownloadQuery(msgId, exp, sig, mockEnv(TEST_SECRET));
    expect(ok).toBe(true);
  });

  it("rejects wrong signature", async () => {
    const msgId = "550e8400-e29b-41d4-a716-446655440000";
    const expUnix = Math.floor(Date.now() / 1000) + 3600;
    const ok = await verifyDownloadQuery(msgId, String(expUnix), "deadbeef", mockEnv(TEST_SECRET));
    expect(ok).toBe(false);
  });
});

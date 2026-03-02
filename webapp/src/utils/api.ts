import type {
  UptimeInstance,
  DeviceHealth,
  DeviceSettings,
  MonitorStatus,
  AuthChallenge,
  AuthToken,
} from "../types/api";
import { getEndpoint, getToken, setToken, clearToken, getPsk } from "./storage";

class ApiError extends Error {
  constructor(
    public status: number,
    message: string,
  ) {
    super(message);
  }
}

async function hmacSha256(key: string, data: Uint8Array): Promise<string> {
  const enc = new TextEncoder();
  const keyData = enc.encode(key);
  const cryptoKey = await crypto.subtle.importKey(
    "raw",
    keyData.buffer as ArrayBuffer,
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const dataBuffer = new ArrayBuffer(data.length);
  new Uint8Array(dataBuffer).set(data);
  const sig = await crypto.subtle.sign("HMAC", cryptoKey, dataBuffer);
  return Array.from(new Uint8Array(sig))
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

function hexToBytes(hex: string): Uint8Array {
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < hex.length; i += 2) {
    bytes[i / 2] = parseInt(hex.substring(i, i + 2), 16);
  }
  return bytes;
}

async function autoLogin(): Promise<string> {
  const base = getEndpoint();
  const psk = getPsk();

  const challengeResp = await fetch(`${base}/api/v1/auth/challenge`, {
    method: "POST",
  });
  if (!challengeResp.ok) throw new ApiError(challengeResp.status, "Failed to get challenge");
  const { challenge } = (await challengeResp.json()) as AuthChallenge;

  const challengeBytes = hexToBytes(challenge);
  const response = await hmacSha256(psk, challengeBytes);

  const loginResp = await fetch(`${base}/api/v1/auth/login`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ challenge, response }),
  });
  if (!loginResp.ok) throw new ApiError(loginResp.status, "Authentication failed");
  const { token } = (await loginResp.json()) as AuthToken;

  setToken(token);
  return token;
}

async function apiFetch<T>(path: string, options: RequestInit = {}): Promise<T> {
  const base = getEndpoint();
  if (!base) throw new Error("No device endpoint configured");

  let token = getToken();

  const doFetch = async (authToken: string) => {
    const headers: Record<string, string> = {
      ...((options.headers as Record<string, string>) || {}),
    };
    if (authToken) {
      headers["Authorization"] = `Bearer ${authToken}`;
    }
    return fetch(`${base}${path}`, { ...options, headers, signal: options.signal ?? AbortSignal.timeout(10000) });
  };

  let resp = await doFetch(token);

  if (resp.status === 401 && getPsk()) {
    try {
      token = await autoLogin();
      resp = await doFetch(token);
    } catch {
      clearToken();
      throw new ApiError(401, "Authentication failed");
    }
  }

  if (!resp.ok) {
    const body = await resp.json().catch(() => ({ error: resp.statusText }));
    throw new ApiError(resp.status, body.error || resp.statusText);
  }

  return resp.json() as Promise<T>;
}

// Health (no auth)
export async function getHealth(): Promise<DeviceHealth> {
  const base = getEndpoint();
  if (!base) throw new Error("No device endpoint configured");
  const resp = await fetch(`${base}/api/v1/health`, {
    signal: AbortSignal.timeout(5000),
  });
  if (!resp.ok) throw new ApiError(resp.status, "Health check failed");
  return resp.json();
}

// Instances
export async function getInstances(): Promise<{ instances: UptimeInstance[] }> {
  return apiFetch("/api/v1/instances");
}

export async function addInstance(inst: Omit<UptimeInstance, "id">): Promise<void> {
  await apiFetch("/api/v1/instances", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(inst),
  });
}

export async function updateInstance(id: number, inst: Omit<UptimeInstance, "id">): Promise<void> {
  await apiFetch(`/api/v1/instances/${id}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(inst),
  });
}

export async function deleteInstance(id: number): Promise<void> {
  await apiFetch(`/api/v1/instances/${id}`, { method: "DELETE" });
}

// Monitor status
export async function getMonitorStatus(): Promise<MonitorStatus> {
  return apiFetch("/api/v1/monitor/status");
}

// Settings
export async function getSettings(): Promise<DeviceSettings> {
  return apiFetch("/api/v1/settings");
}

export async function updateWifi(ssid: string, password: string): Promise<void> {
  await apiFetch("/api/v1/settings/wifi", {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ ssid, password }),
  });
}

export async function updatePsk(psk: string): Promise<void> {
  await apiFetch("/api/v1/settings/psk", {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ psk }),
  });
}

export async function updatePollInterval(interval: number): Promise<void> {
  await apiFetch("/api/v1/settings/poll", {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ poll_interval: interval }),
  });
}

export async function updateLedBrightness(brightness: number): Promise<void> {
  await apiFetch("/api/v1/settings/led-brightness", {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ brightness }),
  });
}

// System
export async function rebootDevice(): Promise<void> {
  await apiFetch("/api/v1/system/reboot", { method: "POST" });
}

// Auth (explicit login)
export async function login(psk: string): Promise<string> {
  const base = getEndpoint();
  const challengeResp = await fetch(`${base}/api/v1/auth/challenge`, { method: "POST" });
  if (!challengeResp.ok) throw new ApiError(challengeResp.status, "Failed to get challenge");
  const { challenge } = (await challengeResp.json()) as AuthChallenge;

  const challengeBytes = hexToBytes(challenge);
  const response = await hmacSha256(psk, challengeBytes);

  const loginResp = await fetch(`${base}/api/v1/auth/login`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ challenge, response }),
  });
  if (!loginResp.ok) throw new ApiError(loginResp.status, "Authentication failed");
  const { token } = (await loginResp.json()) as AuthToken;

  setToken(token);
  return token;
}

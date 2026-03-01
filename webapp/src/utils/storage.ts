const KEYS = {
  endpoint: "uptimemon_endpoint",
  token: "uptimemon_token",
  psk: "uptimemon_psk",
} as const;

export function normalizeEndpoint(url: string): string {
  const trimmed = url.trim();
  if (!trimmed) return "";

  const hasScheme = /^[a-zA-Z][a-zA-Z\d+\-.]*:\/\//.test(trimmed);
  const candidate = hasScheme ? trimmed : `http://${trimmed}`;

  try {
    const parsed = new URL(candidate);
    return `${parsed.protocol}//${parsed.host}`;
  } catch {
    return candidate.replace(/\/+$/, "");
  }
}

export function getEndpoint(): string {
  return localStorage.getItem(KEYS.endpoint) || "";
}

export function setEndpoint(url: string): void {
  localStorage.setItem(KEYS.endpoint, normalizeEndpoint(url));
}

export function getToken(): string {
  return localStorage.getItem(KEYS.token) || "";
}

export function setToken(token: string): void {
  localStorage.setItem(KEYS.token, token);
}

export function clearToken(): void {
  localStorage.removeItem(KEYS.token);
}

export function getPsk(): string {
  return localStorage.getItem(KEYS.psk) || "";
}

export function setPsk(psk: string): void {
  localStorage.setItem(KEYS.psk, psk);
}

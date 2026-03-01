import { useState } from "preact/hooks";
import { RoutableProps } from "preact-router";
import { PageHeader } from "./PageHeader";
import { nav } from "../utils/nav";
import { getEndpoint, setEndpoint, setPsk, normalizeEndpoint } from "../utils/storage";
import { getHealth, login, updatePsk } from "../utils/api";

export function DeviceSetup(_props: RoutableProps) {
  const [url, setUrl] = useState(getEndpoint() || "http://");
  const [psk, setPskValue] = useState("");
  const [status, setStatus] = useState("");
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);
  const [step, setStep] = useState<"endpoint" | "set-psk" | "login">("endpoint");

  const handleTest = async () => {
    setError("");
    setStatus("Testing connection...");
    const endpoint = normalizeEndpoint(url);
    if (!endpoint || !/^https?:\/\//.test(endpoint)) {
      setError("Enter a valid device URL (example: http://10.0.40.138)");
      setStatus("");
      return;
    }

    try {
      setEndpoint(endpoint);
      setUrl(endpoint);
      const health = await getHealth();
      setStatus(`Connected! IP: ${health.ip}, RSSI: ${health.rssi} dBm`);

      if (health.psk_configured) {
        setStep("login");
      } else {
        setStep("set-psk");
      }
    } catch {
      setError(`Cannot reach ${endpoint}/api/v1/health. Check the IP/hostname and ensure you're on the same network.`);
      setStatus("");
    }
  };

  const handleSetPsk = async () => {
    if (psk.length < 8) {
      setError("Password must be at least 8 characters");
      return;
    }
    setError("");
    setLoading(true);
    setStatus("Setting up device security...");

    try {
      await login("initial-setup");
      await updatePsk(psk);
      setPsk(psk);
      await login(psk);
      setStatus("Password set and authenticated!");
      nav("/");
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to set password on device");
      setStatus("");
    } finally {
      setLoading(false);
    }
  };

  const handleLogin = async () => {
    setError("");
    setLoading(true);
    setStatus("Authenticating...");
    try {
      setPsk(psk);
      await login(psk);
      setStatus("Authenticated!");
      nav("/");
    } catch {
      setError("Authentication failed. Check your password.");
      setStatus("");
    } finally {
      setLoading(false);
    }
  };

  return (
    <div style={{ padding: "2rem", maxWidth: "600px", margin: "0 auto" }}>
      <PageHeader title="Device Setup" />

      {step === "endpoint" && (
        <div style={cardStyle}>
          <h3 style={{ marginTop: 0, fontSize: "1rem" }}>Device Address</h3>
          <p style={{ color: "#94a3b8", fontSize: "0.85rem" }}>
            Enter your ESP32's base URL (no path). Example: http://10.0.40.138 or http://esp-uptimemonitor.local.
          </p>
          <input
            type="text"
            placeholder="http://192.168.1.100"
            value={url}
            onInput={(e) => setUrl((e.target as HTMLInputElement).value)}
            style={inputStyle}
          />
          <button onClick={handleTest} style={{ ...btnStyle, background: "#3b82f6" }}>
            Test Connection
          </button>
        </div>
      )}

      {step === "set-psk" && (
        <div style={cardStyle}>
          <h3 style={{ marginTop: 0, fontSize: "1rem" }}>Set Device Password</h3>
          <p style={{ color: "#94a3b8", fontSize: "0.85rem" }}>
            No password is set on this device yet. Choose a password to secure it.
            You'll need this to connect from any browser in the future.
          </p>
          <input
            type="password"
            placeholder="Choose a password (min 8 characters)"
            value={psk}
            onInput={(e) => setPskValue((e.target as HTMLInputElement).value)}
            style={inputStyle}
          />
          <button
            onClick={handleSetPsk}
            disabled={loading}
            style={{ ...btnStyle, background: loading ? "#374151" : "#22c55e" }}
          >
            {loading ? "Setting up..." : "Set Password & Continue"}
          </button>
        </div>
      )}

      {step === "login" && (
        <div style={cardStyle}>
          <h3 style={{ marginTop: 0, fontSize: "1rem" }}>Authentication</h3>
          <p style={{ color: "#94a3b8", fontSize: "0.85rem" }}>
            Enter the password for this device.
          </p>
          <input
            type="password"
            placeholder="Password"
            value={psk}
            onInput={(e) => setPskValue((e.target as HTMLInputElement).value)}
            style={inputStyle}
          />
          <button
            onClick={handleLogin}
            disabled={loading}
            style={{ ...btnStyle, background: loading ? "#374151" : "#3b82f6" }}
          >
            {loading ? "Authenticating..." : "Login"}
          </button>
        </div>
      )}

      {status && <p style={{ color: "#94a3b8", fontSize: "0.85rem", marginTop: "1rem" }}>{status}</p>}
      {error && <p style={{ color: "#ef4444", fontSize: "0.85rem", marginTop: "0.5rem" }}>{error}</p>}

      <div style={{ marginTop: "2rem", textAlign: "center" }}>
        <button onClick={() => nav("/provision")} style={{ background: "none", border: "none", color: "#3b82f6", cursor: "pointer", fontSize: "0.85rem" }}>
          Need to provision WiFi first?
        </button>
      </div>
    </div>
  );
}

const cardStyle: Record<string, string> = {
  padding: "1rem",
  background: "#1e293b",
  borderRadius: "8px",
  marginBottom: "1rem",
};

const inputStyle: Record<string, string> = {
  width: "100%",
  padding: "0.5rem",
  background: "#0f172a",
  color: "#e2e8f0",
  border: "1px solid #334155",
  borderRadius: "6px",
  fontSize: "0.9rem",
  boxSizing: "border-box",
  marginBottom: "0.75rem",
};

const btnStyle: Record<string, string> = {
  padding: "0.5rem 1rem",
  color: "white",
  border: "none",
  borderRadius: "6px",
  cursor: "pointer",
  width: "100%",
};

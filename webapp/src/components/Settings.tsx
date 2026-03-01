import { useState, useEffect } from "preact/hooks";
import { RoutableProps } from "preact-router";
import { PageHeader } from "./PageHeader";
import { getSettings, getHealth, updateWifi, updatePsk, updatePollInterval, rebootDevice } from "../utils/api";
import { clearToken, setEndpoint, getEndpoint } from "../utils/storage";
import { nav } from "../utils/nav";
import type { DeviceSettings, DeviceHealth } from "../types/api";

export function Settings(_props: RoutableProps) {
  const [settings, setSettings] = useState<DeviceSettings | null>(null);
  const [health, setHealth] = useState<DeviceHealth | null>(null);
  const [error, setError] = useState("");
  const [success, setSuccess] = useState("");

  // WiFi form
  const [newSsid, setNewSsid] = useState("");
  const [newPass, setNewPass] = useState("");

  // PSK form
  const [newPsk, setNewPsk] = useState("");

  // Poll interval form
  const [pollInterval, setPollIntervalValue] = useState("");

  // Reboot confirm
  const [rebootConfirm, setRebootConfirm] = useState(false);
  const [rebooting, setRebooting] = useState(false);

  useEffect(() => {
    const load = async () => {
      try {
        const [s, h] = await Promise.all([getSettings(), getHealth()]);
        setSettings(s);
        setHealth(h);
        setPollIntervalValue(String(s.poll_interval));
      } catch (e) {
        setError(e instanceof Error ? e.message : "Failed to load settings");
      }
    };
    load();
  }, []);

  const handleWifiUpdate = async () => {
    if (!newSsid) { setError("SSID is required"); return; }
    setError("");
    setSuccess("");
    try {
      await updateWifi(newSsid, newPass);
      setSuccess("WiFi credentials updated. Device is rebooting...");
      setNewSsid("");
      setNewPass("");
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to update WiFi");
    }
  };

  const handlePskUpdate = async () => {
    if (newPsk.length < 8) { setError("Password must be at least 8 characters"); return; }
    setError("");
    setSuccess("");
    try {
      await updatePsk(newPsk);
      clearToken();
      setSuccess("Password updated. You'll need to re-authenticate.");
      setNewPsk("");
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to update password");
    }
  };

  const handlePollUpdate = async () => {
    const val = parseInt(pollInterval, 10);
    if (isNaN(val) || val < 5 || val > 3600) {
      setError("Poll interval must be 5–3600 seconds");
      return;
    }
    setError("");
    setSuccess("");
    try {
      await updatePollInterval(val);
      setSuccess("Poll interval updated");
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to update poll interval");
    }
  };

  const handleReboot = async () => {
    setRebooting(true);
    setError("");
    try {
      await rebootDevice();
      setSuccess("Device is rebooting...");
    } catch {
      // Device reboots immediately so connection may be lost — that's expected
      setSuccess("Reboot command sent. Device is restarting...");
    } finally {
      setRebooting(false);
      setRebootConfirm(false);
    }
  };

  const handleDisconnect = () => {
    setEndpoint("");
    clearToken();
    nav("/setup");
  };

  return (
    <div style={{ padding: "2rem", maxWidth: "600px", margin: "0 auto" }}>
      <PageHeader title="Settings" />

      {/* Device info */}
      {settings && (
        <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px", marginBottom: "1rem" }}>
          <h3 style={{ marginTop: 0, fontSize: "1rem" }}>Device Info</h3>
          <div style={{ display: "grid", gridTemplateColumns: "auto 1fr", gap: "0.25rem 1rem", fontSize: "0.85rem" }}>
            <span style={{ color: "#64748b" }}>Hostname:</span>
            <span>{settings.hostname}</span>
            <span style={{ color: "#64748b" }}>WiFi SSID:</span>
            <span>{settings.wifi_ssid}</span>
            <span style={{ color: "#64748b" }}>IP:</span>
            <span>{settings.ip}</span>
            {health && (
              <>
                <span style={{ color: "#64748b" }}>RSSI:</span>
                <span>{health.rssi} dBm</span>
                <span style={{ color: "#64748b" }}>Free Heap:</span>
                <span>{Math.round(health.free_heap / 1024)} KB</span>
                <span style={{ color: "#64748b" }}>Uptime:</span>
                <span>{Math.round(health.uptime_s / 60)} min</span>
              </>
            )}
            <span style={{ color: "#64748b" }}>Endpoint:</span>
            <span style={{ fontFamily: "monospace", fontSize: "0.8rem" }}>{getEndpoint()}</span>
          </div>
        </div>
      )}

      {/* WiFi reconfiguration */}
      <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px", marginBottom: "1rem" }}>
        <h3 style={{ marginTop: 0, fontSize: "1rem" }}>Update WiFi</h3>
        <p style={{ color: "#f59e0b", fontSize: "0.8rem", marginBottom: "0.5rem" }}>
          Warning: Device will reboot after saving WiFi credentials.
        </p>
        <div style={{ display: "flex", flexDirection: "column", gap: "0.5rem" }}>
          <input
            type="text" placeholder="New SSID" value={newSsid}
            onInput={(e) => setNewSsid((e.target as HTMLInputElement).value)}
            style={inputStyle}
          />
          <input
            type="password" placeholder="New Password" value={newPass}
            onInput={(e) => setNewPass((e.target as HTMLInputElement).value)}
            style={inputStyle}
          />
          <button onClick={handleWifiUpdate} style={{ ...btnStyle, background: "#3b82f6" }}>
            Update WiFi
          </button>
        </div>
      </div>

      {/* Password change */}
      <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px", marginBottom: "1rem" }}>
        <h3 style={{ marginTop: 0, fontSize: "1rem" }}>Change Password</h3>
        <div style={{ display: "flex", flexDirection: "column", gap: "0.5rem" }}>
          <input
            type="password" placeholder="New password (min 8 chars)" value={newPsk}
            onInput={(e) => setNewPsk((e.target as HTMLInputElement).value)}
            style={inputStyle}
          />
          <button onClick={handlePskUpdate} style={{ ...btnStyle, background: "#f59e0b" }}>
            Change Password
          </button>
        </div>
      </div>

      {/* Poll interval */}
      <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px", marginBottom: "1rem" }}>
        <h3 style={{ marginTop: 0, fontSize: "1rem" }}>Poll Interval</h3>
        <div style={{ display: "flex", gap: "0.5rem", alignItems: "center" }}>
          <input
            type="number" min="5" max="3600" value={pollInterval}
            onInput={(e) => setPollIntervalValue((e.target as HTMLInputElement).value)}
            style={{ ...inputStyle, width: "100px" }}
          />
          <span style={{ color: "#64748b", fontSize: "0.85rem" }}>seconds (5–3600)</span>
          <button onClick={handlePollUpdate} style={{ ...btnStyle, background: "#3b82f6" }}>
            Save
          </button>
        </div>
      </div>

      {/* Reboot device */}
      <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px", marginBottom: "1rem" }}>
        <h3 style={{ marginTop: 0, fontSize: "1rem" }}>Reboot Device</h3>
        {!rebootConfirm ? (
          <button onClick={() => setRebootConfirm(true)} style={{ ...btnStyle, background: "#374151" }}>
            Reboot Device
          </button>
        ) : (
          <div style={{ display: "flex", gap: "0.5rem" }}>
            <button
              onClick={handleReboot}
              disabled={rebooting}
              style={{ ...btnStyle, background: rebooting ? "#374151" : "#dc2626" }}
            >
              {rebooting ? "Rebooting..." : "Confirm Reboot"}
            </button>
            <button onClick={() => setRebootConfirm(false)} style={{ ...btnStyle, background: "#374151" }}>
              Cancel
            </button>
          </div>
        )}
      </div>

      {/* Disconnect */}
      <button onClick={handleDisconnect} style={{ ...btnStyle, background: "#991b1b", width: "100%" }}>
        Disconnect from Device
      </button>

      {success && <p style={{ color: "#22c55e", fontSize: "0.85rem", marginTop: "1rem" }}>{success}</p>}
      {error && <p style={{ color: "#ef4444", fontSize: "0.85rem", marginTop: "0.5rem" }}>{error}</p>}
    </div>
  );
}

const inputStyle: Record<string, string> = {
  padding: "0.5rem",
  background: "#0f172a",
  color: "#e2e8f0",
  border: "1px solid #334155",
  borderRadius: "6px",
  fontSize: "0.9rem",
};

const btnStyle: Record<string, string> = {
  padding: "0.5rem 1rem",
  color: "white",
  border: "none",
  borderRadius: "6px",
  cursor: "pointer",
};

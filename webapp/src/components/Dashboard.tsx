import { useState, useEffect, useRef } from "preact/hooks";
import { RoutableProps } from "preact-router";
import { nav } from "../utils/nav";
import { getEndpoint, getToken } from "../utils/storage";
import { getHealth, getMonitorStatus } from "../utils/api";
import type { DeviceHealth, MonitorStatus } from "../types/api";

function statusColor(s: string): string {
  if (s === "all_up") return "#22c55e";
  if (s === "some_down") return "#f59e0b";
  if (s === "unreachable" || s === "api_key_invalid") return "#ef4444";
  return "#64748b";
}

function statusLabel(s: string): string {
  if (s === "all_up") return "All monitors up";
  if (s === "some_down") return "Some monitors down";
  if (s === "unreachable") return "Instance unreachable";
  if (s === "api_key_invalid") return "API key invalid";
  return "Unknown";
}

export function Dashboard(_props: RoutableProps) {
  const [health, setHealth] = useState<DeviceHealth | null>(null);
  const [monitorStatus, setMonitorStatus] = useState<MonitorStatus | null>(null);
  const [error, setError] = useState("");
  const pollRef = useRef<ReturnType<typeof setInterval>>();

  const endpoint = getEndpoint();
  const token = getToken();

  useEffect(() => {
    if (!endpoint) return;

    const pollHealth = async () => {
      try {
        const h = await getHealth();
        setHealth(h);
        setError("");
      } catch {
        setHealth(null);
        setError("Cannot reach device");
      }
    };

    const pollMonitor = async () => {
      if (!token) return;
      try {
        const ms = await getMonitorStatus();
        setMonitorStatus(ms);
      } catch {
        // Ignore — auth may not be set up yet
      }
    };

    pollHealth();
    pollMonitor();
    pollRef.current = setInterval(() => {
      pollHealth();
      pollMonitor();
    }, 10000);
    return () => clearInterval(pollRef.current);
  }, [endpoint, token]);

  /* No device configured yet — show getting-started screen */
  if (!endpoint) {
    return (
      <div style={{ padding: "2rem", maxWidth: "600px", margin: "0 auto" }}>
        <h2 style={{ margin: "0 0 0.5rem" }}>ESP32 Uptime Kuma Monitor</h2>
        <p style={{ color: "#94a3b8", marginBottom: "2rem" }}>
          Monitor your Uptime Kuma instances with a physical ESP32 LED indicator.
        </p>

        <div style={{ display: "flex", flexDirection: "column", gap: "0.75rem" }}>
          <button
            onClick={() => nav("/flash")}
            style={{ ...cardBtnStyle, background: "#1e293b" }}
          >
            <span style={{ fontSize: "1.1rem", fontWeight: "bold" }}>1. Flash Firmware</span>
            <span style={{ color: "#94a3b8", fontSize: "0.85rem" }}>
              Flash the Uptime Monitor firmware to your ESP32 via USB
            </span>
          </button>

          <button
            onClick={() => nav("/provision")}
            style={{ ...cardBtnStyle, background: "#1e293b" }}
          >
            <span style={{ fontSize: "1.1rem", fontWeight: "bold" }}>2. Set up WiFi</span>
            <span style={{ color: "#94a3b8", fontSize: "0.85rem" }}>
              Configure WiFi on the ESP32 via Improv Serial
            </span>
          </button>

          <button
            onClick={() => nav("/setup")}
            style={{ ...cardBtnStyle, background: "#1e293b" }}
          >
            <span style={{ fontSize: "1.1rem", fontWeight: "bold" }}>3. Connect to Device</span>
            <span style={{ color: "#94a3b8", fontSize: "0.85rem" }}>
              Enter the device IP and authenticate
            </span>
          </button>
        </div>
      </div>
    );
  }

  const aggStatus = health?.monitor_status ?? monitorStatus?.status ?? "unknown";

  return (
    <div style={{ padding: "2rem", maxWidth: "600px", margin: "0 auto" }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: "1.5rem" }}>
        <h2 style={{ margin: 0 }}>Uptime Monitor</h2>
        <div style={{ display: "flex", gap: "0.5rem" }}>
          <button onClick={() => nav("/instances")} style={navBtnStyle}>Instances</button>
          <button onClick={() => nav("/settings")} style={navBtnStyle}>Settings</button>
          <button onClick={() => nav("/flash")} style={navBtnStyle}>Flash</button>
        </div>
      </div>

      {/* Aggregate status */}
      <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px", marginBottom: "1rem", display: "flex", alignItems: "center", gap: "1rem" }}>
        <span style={{ fontSize: "2rem", color: statusColor(aggStatus) }}>&#9679;</span>
        <div>
          <div style={{ fontWeight: "bold", fontSize: "1.1rem" }}>{statusLabel(aggStatus)}</div>
          {health && (
            <div style={{ color: "#64748b", fontSize: "0.8rem" }}>
              {health.ip} | RSSI: {health.rssi} dBm | Heap: {Math.round(health.free_heap / 1024)}KB
            </div>
          )}
          {error && <div style={{ color: "#ef4444", fontSize: "0.8rem" }}>{error}</div>}
        </div>
      </div>

      {/* Per-instance cards */}
      {monitorStatus && monitorStatus.instances.length > 0 ? (
        <div>
          {monitorStatus.instances.map((inst) => {
            const instStatus = !inst.reachable ? "unreachable"
              : !inst.key_valid ? "api_key_invalid"
              : inst.down > 0 ? "some_down"
              : "all_up";
            return (
              <div
                key={inst.id}
                style={{
                  padding: "0.75rem 1rem",
                  background: "#1e293b",
                  borderRadius: "8px",
                  marginBottom: "0.5rem",
                  borderLeft: `4px solid ${statusColor(instStatus)}`,
                }}
              >
                <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
                  <div>
                    <div style={{ fontWeight: "bold" }}>{inst.name}</div>
                    <div style={{ color: "#64748b", fontSize: "0.8rem" }}>
                      {!inst.reachable ? "Unreachable"
                        : !inst.key_valid ? "API key invalid"
                        : `Up: ${inst.up} | Down: ${inst.down}`}
                    </div>
                  </div>
                  <span style={{ color: statusColor(instStatus), fontSize: "1.2rem" }}>&#9679;</span>
                </div>
              </div>
            );
          })}
        </div>
      ) : token ? (
        <div style={{ textAlign: "center", padding: "3rem 1rem", color: "#64748b" }}>
          <p>No instances configured yet.</p>
          <button onClick={() => nav("/instances")} style={{ ...btnStyle, background: "#3b82f6" }}>
            Add Instances
          </button>
        </div>
      ) : (
        <div style={{ textAlign: "center", padding: "2rem 1rem", color: "#64748b" }}>
          <p>Connect to see monitor status.</p>
          <button onClick={() => nav("/setup")} style={{ ...btnStyle, background: "#3b82f6", marginTop: "0.5rem" }}>
            Connect
          </button>
        </div>
      )}
    </div>
  );
}

const btnStyle: Record<string, string> = {
  padding: "0.5rem 1rem",
  color: "white",
  border: "none",
  borderRadius: "6px",
  cursor: "pointer",
  fontSize: "0.9rem",
};

const navBtnStyle: Record<string, string> = {
  padding: "0.3rem 0.6rem",
  background: "#374151",
  color: "#94a3b8",
  border: "1px solid #4b5563",
  borderRadius: "6px",
  cursor: "pointer",
  fontSize: "0.75rem",
};

const cardBtnStyle: Record<string, string> = {
  display: "flex",
  flexDirection: "column",
  gap: "0.25rem",
  padding: "1rem 1.25rem",
  color: "#e2e8f0",
  border: "1px solid #334155",
  borderRadius: "8px",
  cursor: "pointer",
  textAlign: "left",
  width: "100%",
};

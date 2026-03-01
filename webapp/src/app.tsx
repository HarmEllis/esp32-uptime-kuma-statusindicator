import Router from "preact-router";
import { useEffect } from "preact/hooks";
import { Dashboard } from "./components/Dashboard";
import { InstanceManager } from "./components/InstanceManager";
import { DeviceSetup } from "./components/DeviceSetup";
import { WifiProvisioning } from "./components/WifiProvisioning";
import { FirmwareFlash } from "./components/FirmwareFlash";
import { Settings } from "./components/Settings";
import { BASE, nav } from "./utils/nav";

function Redirect(_props: { path?: string; default?: boolean }) {
  useEffect(() => {
    nav("/");
  }, []);
  return null;
}

export function App() {
  return (
    <div
      style={{
        height: "100%",
        overflow: "auto",
        background: "#1a1a2e",
        color: "#e2e8f0",
        fontFamily: "system-ui, sans-serif",
      }}
    >
      <Router>
        <Dashboard path={`${BASE}/`} />
        <InstanceManager path={`${BASE}/instances`} />
        <DeviceSetup path={`${BASE}/setup`} />
        <WifiProvisioning path={`${BASE}/provision`} />
        <FirmwareFlash path={`${BASE}/flash`} />
        <Settings path={`${BASE}/settings`} />
        <Redirect default />
      </Router>
    </div>
  );
}

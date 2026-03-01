import { useState, useRef, useEffect } from "preact/hooks";
import { RoutableProps } from "preact-router";
import { PageHeader } from "./PageHeader";
import { nav } from "../utils/nav";
import { setEndpoint } from "../utils/storage";

export function WifiProvisioning(_props: RoutableProps) {
  const [status, setStatus] = useState("");
  const [error, setError] = useState("");
  const [deviceUrl, setDeviceUrl] = useState("");
  const [provisioning, setProvisioning] = useState(false);
  const [ssid, setSsid] = useState("");
  const [password, setPassword] = useState("");
  const [step, setStep] = useState<"connect" | "provision" | "done">("connect");
  const [port, setPort] = useState<SerialPort | null>(null);
  const [reader, setReader] = useState<ReadableStreamDefaultReader<Uint8Array> | null>(null);
  const [writer, setWriter] = useState<WritableStreamDefaultWriter<Uint8Array> | null>(null);
  const portRef = useRef<SerialPort | null>(null);
  const readerRef = useRef<ReadableStreamDefaultReader<Uint8Array> | null>(null);
  const writerRef = useRef<WritableStreamDefaultWriter<Uint8Array> | null>(null);

  const closeSerial = async () => {
    try {
      const activePort = portRef.current;
      if (activePort && (activePort.readable || activePort.writable)) {
        await activePort.setSignals({ dataTerminalReady: false, requestToSend: false });
      }
    } catch { /* ignore */ }
    try { await readerRef.current?.cancel(); } catch { /* ignore */ }
    try { readerRef.current?.releaseLock(); } catch { /* ignore */ }
    try { writerRef.current?.releaseLock(); } catch { /* ignore */ }
    try { await portRef.current?.close(); } catch { /* ignore */ }

    readerRef.current = null;
    writerRef.current = null;
    portRef.current = null;
  };

  const handleConnect = async () => {
    if (!("serial" in navigator)) {
      setError("Web Serial API not supported. Use Chrome, Edge, or Opera.");
      return;
    }

    try {
      setError("");
      setStatus("Select the serial port for your ESP32...");
      const serialPort = await navigator.serial.requestPort();
      if (!serialPort.readable || !serialPort.writable) {
        await serialPort.open({ baudRate: 115200 });
      }
      try {
        if (serialPort.readable || serialPort.writable) {
          await serialPort.setSignals({ dataTerminalReady: false, requestToSend: false });
        }
      } catch {
        // Some platforms/adapters may not support signal control.
      }
      setPort(serialPort);
      portRef.current = serialPort;

      const r = serialPort.readable?.getReader();
      const w = serialPort.writable?.getWriter();
      if (!r || !w) throw new Error("Cannot access serial streams");
      setReader(r);
      setWriter(w);
      readerRef.current = r;
      writerRef.current = w;

      setStatus("Connected! Enter WiFi credentials.");
      setStep("provision");
    } catch (e) {
      const message = e instanceof Error ? e.message : "Connection failed";
      if (message.includes("already open") || message.includes("stream is locked")) {
        setError("Selected serial port is already in use. Disconnect from the Flash page and try again.");
      } else {
        setError(message);
      }
    }
  };

  const buildImprovPacket = (type: number, data: Uint8Array): Uint8Array => {
    const header = new TextEncoder().encode("IMPROV");
    const version = 1;
    const pkt = new Uint8Array(header.length + 3 + data.length + 1);
    pkt.set(header, 0);
    pkt[6] = version;
    pkt[7] = type;
    pkt[8] = data.length;
    pkt.set(data, 9);
    // Checksum: sum bytes from version onward
    let checksum = 0;
    for (let i = 6; i < 9 + data.length; i++) checksum += pkt[i];
    pkt[9 + data.length] = checksum & 0xff;
    return pkt;
  };

  const handleProvision = async () => {
    const activeWriter = writerRef.current;
    const activeReader = readerRef.current;
    if (!activeWriter || !activeReader) { setError("Not connected"); return; }
    if (!ssid) { setError("SSID is required"); return; }

    setError("");
    setProvisioning(true);
    setStatus("Sending WiFi credentials...");

    try {
      // Build RPC command: WiFi settings (0x01)
      const ssidBytes = new TextEncoder().encode(ssid);
      const passBytes = new TextEncoder().encode(password);
      // Layout: [cmd, cmd_len, ssid_len, ...ssid, pass_len, ...pass]
      const rpcData = new Uint8Array(4 + ssidBytes.length + passBytes.length);
      const cmdDataLen = 2 + ssidBytes.length + passBytes.length;
      rpcData[0] = 0x01; // command: wifi settings
      rpcData[1] = cmdDataLen; // data length
      rpcData[2] = ssidBytes.length;
      rpcData.set(ssidBytes, 3);
      rpcData[3 + ssidBytes.length] = passBytes.length;
      rpcData.set(passBytes, 4 + ssidBytes.length);

      const packet = buildImprovPacket(0x03, rpcData); // TYPE_RPC_COMMAND
      await activeWriter.write(packet);

      // Read response (with timeout)
      setStatus("Waiting for device to connect to WiFi...");
      const buf: number[] = [];
      const findHeader = (bytes: number[]): number => {
        for (let i = 0; i <= bytes.length - 6; i++) {
          if (
            bytes[i] === 0x49 && // I
            bytes[i + 1] === 0x4d && // M
            bytes[i + 2] === 0x50 && // P
            bytes[i + 3] === 0x52 && // R
            bytes[i + 4] === 0x4f && // O
            bytes[i + 5] === 0x56 // V
          ) {
            return i;
          }
        }
        return -1;
      };

      const PROVISION_TIMEOUT_MS = 240000;
      let done = false;
      let failed = false;
      let timedOut = false;
      const fail = (message: string) => {
        setError(message);
        failed = true;
        done = true;
      };
      const timeout = setTimeout(() => {
        timedOut = true;
        void activeReader.cancel();
      }, PROVISION_TIMEOUT_MS);
      try {
        while (!done) {
          const { value, done: streamDone } = await activeReader.read();
          if (streamDone) {
            if (timedOut) break;
            fail("Serial connection closed during provisioning");
            break;
          }

          if (!value) continue;
          for (const byte of value) buf.push(byte);

          while (!done) {
            const idx = findHeader(buf);
            if (idx < 0) {
              if (buf.length > 5) {
                // Keep only a small suffix for partial "IMPROV" headers.
                buf.splice(0, buf.length - 5);
              }
              break;
            }

            if (idx > 0) {
              buf.splice(0, idx);
            }

            if (buf.length < 10) {
              break;
            }

            const type = buf[7];
            const len = buf[8];
            const packetLen = 10 + len; // header+ver+type+len+data+checksum
            if (buf.length < packetLen) {
              break;
            }

            let checksum = 0;
            for (let i = 6; i < 9 + len; i++) checksum = (checksum + buf[i]) & 0xff;
            if (checksum !== buf[9 + len]) {
              // Desync: drop one byte and rescan.
              buf.splice(0, 1);
              continue;
            }

            const data = buf.slice(9, 9 + len);
            buf.splice(0, packetLen);

            if (type === 0x01 && data.length >= 1) { // STATE
              const state = data[0];
              if (state === 0x03) { // PROVISIONING
                setStatus("Credentials accepted. Device is connecting to WiFi (can take up to 4 minutes)...");
              } else if (state === 0x04) { // PROVISIONED
                setStatus("Provisioned!");
                done = true;
              }
            } else if (type === 0x04) { // RPC_RESULT
              // Extract URL from response
              if (data.length > 3) {
                const urlLen = data[3];
                if (urlLen > 0 && data.length >= 4 + urlLen) {
                  const url = String.fromCharCode(...data.slice(4, 4 + urlLen));
                  setDeviceUrl(url);
                  setEndpoint(url);
                }
              }
              done = true;
            } else if (type === 0x02 && data.length >= 1) { // ERROR
              const errCode = data[0];
              if (errCode === 0x03) {
                fail("Device could not connect to WiFi. Check SSID/password and confirm this is a 2.4GHz network.");
              } else if (errCode === 0x01) {
                fail("Device rejected WiFi credentials payload (invalid RPC format).");
              } else {
                fail(`Device provisioning failed with error code 0x${errCode.toString(16).padStart(2, "0")}.`);
              }
            }
          }
        }
      } finally {
        clearTimeout(timeout);
      }

      if (timedOut && !done) {
        fail("Timeout waiting for provisioning response");
      }

      if (!failed) {
        await closeSerial();
        setPort(null);
        setReader(null);
        setWriter(null);
        setStep("done");
        setStatus("WiFi provisioned successfully!");
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : "Provisioning failed");
    } finally {
      setProvisioning(false);
    }
  };

  const handleDone = async () => {
    await closeSerial();
    setPort(null);
    setReader(null);
    setWriter(null);

    if (deviceUrl) {
      setEndpoint(deviceUrl);
      nav("/setup");
    } else {
      nav("/setup");
    }
  };

  const handleDisconnect = async () => {
    await closeSerial();
    setPort(null);
    setReader(null);
    setWriter(null);
    setStep("connect");
    setStatus("");
  };

  useEffect(() => {
    return () => {
      void closeSerial();
    };
  }, []);

  return (
    <div style={{ padding: "2rem", maxWidth: "600px", margin: "0 auto" }}>
      <PageHeader title="WiFi Provisioning" backTo="/flash" />

      <p style={{ color: "#94a3b8", marginBottom: "1.5rem", fontSize: "0.85rem" }}>
        Connect to your ESP32 via USB to configure its WiFi connection using the Improv protocol.
      </p>

      {step === "connect" && (
        <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px" }}>
          <h3 style={{ marginTop: 0, fontSize: "1rem" }}>1. Connect via USB</h3>
          <p style={{ color: "#94a3b8", fontSize: "0.85rem" }}>
            Make sure your ESP32 is connected and has the WoL firmware flashed.
          </p>
          <button
            onClick={handleConnect}
            style={{ padding: "0.5rem 1rem", background: "#3b82f6", color: "white", border: "none", borderRadius: "6px", cursor: "pointer" }}
          >
            Connect to ESP32
          </button>
        </div>
      )}

      {step === "provision" && (
        <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px" }}>
          <h3 style={{ marginTop: 0, fontSize: "1rem" }}>2. WiFi Credentials</h3>
          <div style={{ display: "flex", flexDirection: "column", gap: "0.75rem" }}>
            <input
              type="text"
              placeholder="WiFi SSID"
              value={ssid}
              onInput={(e) => setSsid((e.target as HTMLInputElement).value)}
              style={inputStyle}
            />
            <input
              type="password"
              placeholder="WiFi Password"
              value={password}
              onInput={(e) => setPassword((e.target as HTMLInputElement).value)}
              style={inputStyle}
            />
            <div style={{ display: "flex", gap: "0.5rem" }}>
              <button
                onClick={handleProvision}
                disabled={provisioning}
                style={{ padding: "0.5rem 1rem", background: provisioning ? "#374151" : "#22c55e", color: "white", border: "none", borderRadius: "6px", cursor: provisioning ? "not-allowed" : "pointer", flex: 1 }}
              >
                {provisioning ? "Provisioning..." : "Provision WiFi"}
              </button>
              <button
                onClick={handleDisconnect}
                style={{ padding: "0.5rem 1rem", background: "#374151", color: "#94a3b8", border: "1px solid #4b5563", borderRadius: "6px", cursor: "pointer" }}
              >
                Disconnect
              </button>
            </div>
          </div>
        </div>
      )}

      {step === "done" && (
        <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px", textAlign: "center" }}>
          <h3 style={{ marginTop: 0, color: "#22c55e" }}>WiFi Configured!</h3>
          {deviceUrl && (
            <p style={{ color: "#94a3b8", fontSize: "0.85rem" }}>
              Device is available at: <strong style={{ color: "#e2e8f0" }}>{deviceUrl}</strong>
            </p>
          )}
          <button
            onClick={handleDone}
            style={{ padding: "0.5rem 1.5rem", background: "#3b82f6", color: "white", border: "none", borderRadius: "6px", cursor: "pointer", marginTop: "0.5rem" }}
          >
            Continue to Setup
          </button>
        </div>
      )}

      {status && <p style={{ color: "#94a3b8", fontSize: "0.85rem", marginTop: "1rem" }}>{status}</p>}
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

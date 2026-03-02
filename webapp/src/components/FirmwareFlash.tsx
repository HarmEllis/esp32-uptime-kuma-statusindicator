import { useState, useRef, useCallback, useEffect, useMemo } from "preact/hooks";
import { RoutableProps } from "preact-router";
import { ESPLoader, Transport, FlashOptions } from "esptool-js";
import { BASE, nav } from "../utils/nav";

type ChipFamily = "esp32" | "esp32s3";

const CHIP_CONFIG: Record<
  ChipFamily,
  {
    bootloaderOffset: number;
    flashMode: string;
    flashFreq: string;
    files: { bootloader: string; partitionTable: string; firmware: string };
  }
> = {
  esp32: {
    bootloaderOffset: 0x1000,
    flashMode: "dio",
    flashFreq: "40m",
    files: {
      bootloader: "bootloader-esp32.bin",
      partitionTable: "partition-table-esp32.bin",
      firmware: "firmware-esp32.bin",
    },
  },
  esp32s3: {
    bootloaderOffset: 0x0,
    flashMode: "dio",
    flashFreq: "80m",
    files: {
      bootloader: "bootloader-esp32s3.bin",
      partitionTable: "partition-table-esp32s3.bin",
      firmware: "firmware-esp32s3.bin",
    },
  },
};

/** Fixed flash offsets (partition table and firmware are the same for all targets) */
const OFFSETS = {
  partitionTable: 0x8000,
  firmware: 0x10000,
} as const;

type FlashSource = "files" | "release";

function detectChipFamily(chipName: string): ChipFamily | null {
  const upper = chipName.toUpperCase();
  if (upper.includes("ESP32-S3") || upper === "ESP32S3") return "esp32s3";
  if (upper.startsWith("ESP32")) return "esp32";
  return null;
}

interface FileSlot {
  label: string;
  offset: number;
  file: File | null;
  data: string | null;
  required: boolean;
}

interface ReleaseAsset {
  name: string;
  url: string;
}

interface ReleaseInfo {
  tag: string;
  url: string;
  assets: Record<string, ReleaseAsset>;
}

interface HostedReleaseManifestEntry {
  tag: string;
  url?: string;
  files?: Record<string, string>;
}

/** Convert ArrayBuffer to binary string (as esptool-js expects) */
function arrayBufferToBstr(buf: ArrayBuffer): string {
  const bytes = new Uint8Array(buf);
  const chunks: string[] = [];
  // Process in chunks to avoid call stack size limits
  const chunkSize = 8192;
  for (let i = 0; i < bytes.length; i += chunkSize) {
    const slice = bytes.subarray(i, i + chunkSize);
    chunks.push(String.fromCharCode(...slice));
  }
  return chunks.join("");
}

function withBasePath(relativePath: string): string {
  const normalized = relativePath.replace(/^\/+/, "");
  return `${BASE}/${normalized}`;
}

export function FirmwareFlash(_props: RoutableProps) {
  const [source, setSource] = useState<FlashSource>("files");
  const [status, setStatus] = useState("");
  const [error, setError] = useState("");
  const [flashing, setFlashing] = useState(false);
  const [connected, setConnected] = useState(false);
  const [chipInfo, setChipInfo] = useState("");
  const [chipFamily, setChipFamily] = useState<ChipFamily | null>(null);
  const [chipFamilyOverride, setChipFamilyOverride] = useState<ChipFamily | null>(null);
  const [fileProgress, setFileProgress] = useState<number[]>([]);
  const [logLines, setLogLines] = useState<string[]>([]);
  const [needsManualBoot, setNeedsManualBoot] = useState(false);

  const [releases, setReleases] = useState<ReleaseInfo[] | null>(null);
  const [loadingReleases, setLoadingReleases] = useState(false);
  const [selectedTag, setSelectedTag] = useState("");

  const loaderRef = useRef<ESPLoader | null>(null);
  const transportRef = useRef<Transport | null>(null);
  const portRef = useRef<SerialPort | null>(null);

  // Effective chip family: detected > manual override > default esp32
  const effectiveChipFamily: ChipFamily =
    chipFamily ?? chipFamilyOverride ?? "esp32";

  const [slots, setSlots] = useState<FileSlot[]>([
    {
      label: "Bootloader",
      offset: CHIP_CONFIG.esp32.bootloaderOffset,
      file: null,
      data: null,
      required: true,
    },
    {
      label: "Partition Table",
      offset: OFFSETS.partitionTable,
      file: null,
      data: null,
      required: true,
    },
    {
      label: "Firmware",
      offset: OFFSETS.firmware,
      file: null,
      data: null,
      required: true,
    },
  ]);

  // Update bootloader offset in slots when effective chip family changes
  useEffect(() => {
    const bootloaderOffset = CHIP_CONFIG[effectiveChipFamily].bootloaderOffset;
    setSlots((prev) =>
      prev.map((s, i) => (i === 0 ? { ...s, offset: bootloaderOffset } : s))
    );
  }, [effectiveChipFamily]);

  const logRef = useRef<HTMLDivElement>(null);

  const appendLog = useCallback((line: string) => {
    setLogLines((prev) => {
      const next = [...prev, line];
      // Keep last 200 lines
      return next.length > 200 ? next.slice(-200) : next;
    });
    requestAnimationFrame(() => {
      logRef.current?.scrollTo(0, logRef.current.scrollHeight);
    });
  }, []);

  const terminal = {
    clean: () => setLogLines([]),
    writeLine: (data: string) => appendLog(data),
    write: (data: string) => appendLog(data),
  };

  /** Read a File into a binary string */
  async function readFileAsBstr(file: File): Promise<string> {
    const buf = await file.arrayBuffer();
    return arrayBufferToBstr(buf);
  }

  /** Set a file for a given slot index */
  function handleFileSelect(index: number, file: File | null) {
    setSlots((prev) =>
      prev.map((s, i) => (i === index ? { ...s, file, data: null } : s))
    );
  }

  /** Try connecting with a given reset mode. Returns true on success. */
  async function tryConnect(mode: "default_reset" | "no_reset", label: string): Promise<boolean> {
    const port = portRef.current;
    if (!port) return false;

    const transport = new Transport(port, true);
    transportRef.current = transport;

    const loader = new ESPLoader({
      transport,
      baudrate: 460800,
      romBaudrate: 115200,
      terminal,
    });
    loaderRef.current = loader;

    setStatus(`Connecting to ESP32 (${label})...`);
    try {
      const chip = await loader.main(mode);
      setChipInfo(chip);

      // Detect chip family using canonical API, fall back to description string
      const canonicalName: string =
        (loader as unknown as { chip?: { CHIP_NAME?: string } }).chip?.CHIP_NAME ?? chip;
      const family = detectChipFamily(canonicalName);
      setChipFamily(family);
      setChipFamilyOverride(null);

      setConnected(true);
      setStatus(`Connected: ${chip}`);
      appendLog(`Connected using ${label} mode.`);
      if (family) {
        appendLog(`Detected chip family: ${family}`);
      } else {
        appendLog(`Warning: unrecognised chip "${canonicalName}" — defaulting to ESP32 settings`);
      }
      return true;
    } catch (err) {
      appendLog(`Connect attempt (${label}) failed: ${err instanceof Error ? err.message : String(err)}`);
      try {
        if (port.readable || port.writable) {
          await port.setSignals({ dataTerminalReady: false, requestToSend: false });
        }
      } catch { /* ignore */ }
      try {
        await transport.disconnect();
      } catch { /* ignore */ }
      transportRef.current = null;
      loaderRef.current = null;
      return false;
    }
  }

  /** Connect to the ESP32 via Web Serial */
  async function handleConnect() {
    setError("");
    setStatus("Select the serial port for your ESP32...");
    setLogLines([]);

    if (!("serial" in navigator)) {
      setError("Web Serial API not supported. Use Chrome, Edge, or Opera.");
      return;
    }

    try {
      const port = await navigator.serial.requestPort();
      portRef.current = port;

      // Try auto-reset first (RTS/DTR toggling to enter bootloader)
      if (await tryConnect("default_reset", "auto-reset")) return;

      // Auto-reset failed — clean up and ask user to manually enter boot mode
      await closeTransport();
      portRef.current = port; // keep port reference for manual retry
      setStatus("");
      setError("Auto-reset failed. Put the device in download mode: hold BOOT, tap RESET, release BOOT, then click 'Retry (Manual Mode)'.");
      setNeedsManualBoot(true);
    } catch (e) {
      await closeTransport();
      const message = e instanceof Error ? e.message : "Connection failed";
      setError(message);
      setConnected(false);
    }
  }

  /** Retry connection in manual boot mode (no_reset) after user enters bootloader */
  async function handleManualRetry() {
    setError("");
    setNeedsManualBoot(false);

    try {
      if (await tryConnect("no_reset", "manual boot mode")) return;

      await closeTransport();
      setError("Failed to connect. Ensure the device is in download mode (hold BOOT, tap RESET, release BOOT) and try again.");
      setConnected(false);
    } catch (e) {
      await closeTransport();
      const message = e instanceof Error ? e.message : "Connection failed";
      setError(message);
      setConnected(false);
    }
  }

  async function closeTransport() {
    try {
      const port = portRef.current;
      if (port && (port.readable || port.writable)) {
        await port.setSignals({ dataTerminalReady: false, requestToSend: false });
      }
    } catch {
      // ignore signal cleanup errors
    }
    try {
      await transportRef.current?.disconnect();
    } catch {
      // ignore disconnect errors
    }
    try {
      const port = portRef.current;
      if (port && (port.readable || port.writable)) {
        await port.close();
      }
    } catch {
      // ignore close errors
    }
    transportRef.current = null;
    loaderRef.current = null;
    portRef.current = null;
  }

  /** Disconnect from ESP32 */
  async function handleDisconnect() {
    await closeTransport();
    setConnected(false);
    setChipInfo("");
    setChipFamily(null);
    setChipFamilyOverride(null);
    setStatus("");
  }

  useEffect(() => {
    return () => {
      void closeTransport();
    };
  }, []);

  /** Releases filtered to those that have all files for the effective chip family */
  const filteredReleases = useMemo(() => {
    if (!releases) return null;
    const cfg = CHIP_CONFIG[effectiveChipFamily];
    const needed = [cfg.files.bootloader, cfg.files.partitionTable, cfg.files.firmware];
    return releases
      .filter((r) => needed.every((name) => !!r.assets[name]))
      .slice(0, 20);
  }, [releases, effectiveChipFamily]);

  // Reset selectedTag when the filtered release list changes (chip detection after load,
  // or chip family switch)
  useEffect(() => {
    if (filteredReleases && filteredReleases.length > 0) {
      setSelectedTag(filteredReleases[0].tag);
    } else if (filteredReleases) {
      setSelectedTag("");
    }
  }, [filteredReleases]);

  /** Fetch firmware versions hosted under /firmware on this site */
  async function fetchReleases() {
    setLoadingReleases(true);
    setError("");
    try {
      const resp = await fetch(withBasePath("firmware/releases.json"), {
        cache: "no-store",
      });
      if (!resp.ok) {
        throw new Error(`Hosted firmware index: ${resp.status}`);
      }
      const data = await resp.json();

      const tags = (data as HostedReleaseManifestEntry[])
        .filter(
          (r) =>
            typeof r.tag === "string" &&
            r.tag.startsWith("v") &&
            !!r.files
        )
        .map((r) => {
          const assets: Record<string, ReleaseAsset> = {};
          if (r.files) {
            for (const [filename, filePath] of Object.entries(r.files)) {
              assets[filename] = {
                name: filename,
                url: withBasePath(filePath),
              };
            }
          }
          return {
            tag: r.tag,
            url: r.url ?? "",
            assets,
          };
        });

      setReleases(tags);
      // selectedTag will be set by the filteredReleases useEffect
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to fetch releases");
    } finally {
      setLoadingReleases(false);
    }
  }

  /** Download a binary from the hosted firmware path */
  async function downloadReleaseAsset(asset: ReleaseAsset): Promise<string> {
    appendLog(`Downloading ${asset.name}...`);
    const resp = await fetch(asset.url);
    if (!resp.ok) throw new Error(`Failed to download ${asset.name}: ${resp.status}`);
    const buf = await resp.arrayBuffer();
    appendLog(`Downloaded ${asset.name} (${buf.byteLength} bytes)`);
    return arrayBufferToBstr(buf);
  }

  function requireReleaseAsset(release: ReleaseInfo, name: string): ReleaseAsset {
    const asset = release.assets[name];
    if (!asset) {
      throw new Error(
        `Release ${release.tag} is missing ${name}. Re-run Deploy Webapp to refresh hosted firmware files.`
      );
    }
    return asset;
  }

  /** Flash firmware to ESP32 */
  async function handleFlash() {
    if (!loaderRef.current) {
      setError("Not connected. Connect to ESP32 first.");
      return;
    }

    setError("");
    setFlashing(true);
    setFileProgress([]);

    const chipCfg = CHIP_CONFIG[effectiveChipFamily];

    try {
      let fileArray: { data: string; address: number }[];

      if (source === "files") {
        // Read local files
        const loaded: { data: string; address: number }[] = [];
        for (const slot of slots) {
          if (!slot.file) {
            if (slot.required) {
              setError(`Please select a file for: ${slot.label}`);
              setFlashing(false);
              return;
            }
            continue;
          }
          const data = await readFileAsBstr(slot.file);
          loaded.push({ data, address: slot.offset });
          appendLog(
            `Loaded ${slot.label}: ${slot.file.name} (${slot.file.size} bytes) @ 0x${slot.offset.toString(16)}`
          );
        }
        fileArray = loaded;
      } else {
        // Download from hosted firmware release files
        if (!selectedTag) {
          setError("Select a release version first.");
          setFlashing(false);
          return;
        }

        const selectedRelease = filteredReleases?.find((r) => r.tag === selectedTag);
        if (!selectedRelease) {
          setError("Release metadata not loaded. Click 'Load Hosted Releases' first.");
          setFlashing(false);
          return;
        }

        const bootloader = await downloadReleaseAsset(
          requireReleaseAsset(selectedRelease, chipCfg.files.bootloader)
        );
        const partTable = await downloadReleaseAsset(
          requireReleaseAsset(selectedRelease, chipCfg.files.partitionTable)
        );
        const firmware = await downloadReleaseAsset(
          requireReleaseAsset(selectedRelease, chipCfg.files.firmware)
        );

        fileArray = [
          { data: bootloader, address: chipCfg.bootloaderOffset },
          { data: partTable, address: OFFSETS.partitionTable },
          { data: firmware, address: OFFSETS.firmware },
        ];
      }

      setFileProgress(fileArray.map(() => 0));

      setStatus("Erasing and writing flash...");

      const flashOptions: FlashOptions = {
        fileArray,
        // Preserve flash size encoded in the binaries (supports both 4MB and 8MB builds)
        flashSize: "keep",
        flashMode: chipCfg.flashMode,
        flashFreq: chipCfg.flashFreq,
        eraseAll: false,
        compress: true,
        reportProgress: (fileIndex: number, written: number, total: number) => {
          const pct = Math.round((written / total) * 100);
          setFileProgress((prev) => {
            const next = [...prev];
            next[fileIndex] = pct;
            return next;
          });
        },
      };

      await loaderRef.current.writeFlash(flashOptions);

      setStatus("Flash complete! Resetting device...");
      appendLog("Flash complete. Hard resetting via RTS pin...");
      await loaderRef.current.after("hard_reset", true);

      setStatus("Done! Device has been flashed and reset.");
    } catch (e) {
      const message = e instanceof Error ? e.message : "Flash failed";
      if (
        message.includes("Failed to fetch") ||
        message.includes("NetworkError")
      ) {
        setError(
          "Failed to download hosted firmware files. Try reloading, or use Local Files mode."
        );
      } else {
        setError(message);
      }
    } finally {
      setFlashing(false);
    }
  }

  const allFilesSelected = slots.every((s) => !s.required || s.file);
  const canFlash =
    connected &&
    !flashing &&
    (source === "release" ? !!selectedTag : allFilesSelected);

  const chipDetected = connected && chipFamily !== null;
  const chipUnrecognised = connected && chipFamily === null;

  return (
    <div style={{ padding: "2rem", maxWidth: "600px", margin: "0 auto" }}>
      <div
        style={{
          display: "flex",
          alignItems: "center",
          gap: "1rem",
          marginBottom: "1.5rem",
        }}
      >
        <button
          onClick={() => nav("/")}
          style={{
            background: "none",
            border: "none",
            color: "#94a3b8",
            cursor: "pointer",
            fontSize: "1.2rem",
            padding: "0.25rem",
          }}
        >
          &larr;
        </button>
        <h2 style={{ margin: 0 }}>Flash Firmware</h2>
      </div>

      <p style={{ color: "#94a3b8", marginBottom: "1.5rem" }}>
        Flash firmware to your ESP32 or ESP32-S3 via USB. Connect the device
        and click Connect — the chip is detected automatically. Auto-reset will
        be tried first; if that fails, hold BOOT, tap RESET, release BOOT, then
        retry.
      </p>

      {/* Step 1: Connect */}
      <div
        style={{
          marginBottom: "1.5rem",
          padding: "1rem",
          background: "#1e293b",
          borderRadius: "8px",
        }}
      >
        <h3 style={{ marginTop: 0, fontSize: "1rem" }}>1. Connect</h3>
        {!connected && !needsManualBoot ? (
          <button
            onClick={handleConnect}
            style={{
              padding: "0.5rem 1rem",
              background: "#3b82f6",
              color: "white",
              border: "none",
              borderRadius: "6px",
              cursor: "pointer",
            }}
          >
            Connect to ESP32
          </button>
        ) : needsManualBoot ? (
          <div style={{ display: "flex", flexDirection: "column", gap: "0.5rem" }}>
            <button
              onClick={handleManualRetry}
              style={{
                padding: "0.5rem 1rem",
                background: "#f97316",
                color: "white",
                border: "none",
                borderRadius: "6px",
                cursor: "pointer",
              }}
            >
              Retry (Manual Mode)
            </button>
            <button
              onClick={async () => { setNeedsManualBoot(false); await closeTransport(); setError(""); }}
              style={{
                padding: "0.25rem 0.75rem",
                background: "#374151",
                color: "#94a3b8",
                border: "1px solid #4b5563",
                borderRadius: "6px",
                cursor: "pointer",
                fontSize: "0.85rem",
              }}
            >
              Cancel
            </button>
          </div>
        ) : (
          <div style={{ display: "flex", flexDirection: "column", gap: "0.5rem" }}>
            <div
              style={{
                display: "flex",
                alignItems: "center",
                gap: "0.75rem",
              }}
            >
              <span style={{ color: "#22c55e" }}>Connected: {chipInfo}</span>
              <button
                onClick={handleDisconnect}
                disabled={flashing}
                style={{
                  padding: "0.25rem 0.75rem",
                  background: "#374151",
                  color: "#94a3b8",
                  border: "1px solid #4b5563",
                  borderRadius: "6px",
                  cursor: flashing ? "not-allowed" : "pointer",
                  fontSize: "0.85rem",
                }}
              >
                Disconnect
              </button>
            </div>
            {chipDetected && (
              <p style={{ margin: 0, fontSize: "0.85rem", color: "#94a3b8" }}>
                {effectiveChipFamily === "esp32s3"
                  ? "ESP32-S3 detected — bootloader offset 0x0"
                  : "ESP32 detected — bootloader offset 0x1000"}
              </p>
            )}
            {chipUnrecognised && (
              <div>
                <p style={{ margin: "0 0 0.4rem", fontSize: "0.85rem", color: "#f59e0b" }}>
                  Unrecognised chip — using {(chipFamilyOverride ?? "esp32").toUpperCase()} settings. Override:
                </p>
                <select
                  value={chipFamilyOverride ?? "esp32"}
                  onChange={(e) =>
                    setChipFamilyOverride((e.target as HTMLSelectElement).value as ChipFamily)
                  }
                  style={{
                    padding: "0.3rem",
                    background: "#0f172a",
                    color: "#e2e8f0",
                    border: "1px solid #334155",
                    borderRadius: "6px",
                    fontSize: "0.85rem",
                  }}
                >
                  <option value="esp32">ESP32</option>
                  <option value="esp32s3">ESP32-S3</option>
                </select>
              </div>
            )}
          </div>
        )}
      </div>

      {/* Step 2: Source */}
      <div
        style={{
          marginBottom: "1.5rem",
          padding: "1rem",
          background: "#1e293b",
          borderRadius: "8px",
        }}
      >
        <h3 style={{ marginTop: 0, fontSize: "1rem" }}>2. Firmware Source</h3>
        <div style={{ display: "flex", gap: "0.5rem", marginBottom: "1rem" }}>
          <button
            onClick={() => setSource("files")}
            style={{
              padding: "0.4rem 0.8rem",
              background: source === "files" ? "#3b82f6" : "#374151",
              color: "white",
              border: "none",
              borderRadius: "6px",
              cursor: "pointer",
            }}
          >
            Local Files
          </button>
          <button
            onClick={() => {
              setSource("release");
              if (!releases) fetchReleases();
            }}
            style={{
              padding: "0.4rem 0.8rem",
              background: source === "release" ? "#3b82f6" : "#374151",
              color: "white",
              border: "none",
              borderRadius: "6px",
              cursor: "pointer",
            }}
          >
            Hosted Release
          </button>
        </div>

        {source === "files" && (
          <div style={{ display: "flex", flexDirection: "column", gap: "0.75rem" }}>
            {slots.map((slot, i) => (
              <div key={i}>
                <label
                  style={{
                    display: "block",
                    color: "#94a3b8",
                    marginBottom: "0.25rem",
                    fontSize: "0.85rem",
                  }}
                >
                  {slot.label}
                  <span style={{ color: "#64748b" }}>
                    {" "}
                    (0x{slot.offset.toString(16)})
                  </span>
                </label>
                <input
                  type="file"
                  accept=".bin"
                  onChange={(e) => {
                    const files = (e.target as HTMLInputElement).files;
                    handleFileSelect(i, files?.[0] ?? null);
                  }}
                  style={{
                    display: "block",
                    color: "#e2e8f0",
                    fontSize: "0.85rem",
                  }}
                />
              </div>
            ))}
          </div>
        )}

        {source === "release" && (
          <div>
            {loadingReleases && (
              <p style={{ color: "#94a3b8" }}>Loading releases...</p>
            )}
            {filteredReleases && filteredReleases.length === 0 && (
              <p style={{ color: "#f59e0b" }}>
                No hosted releases found for {effectiveChipFamily.toUpperCase()}.{" "}
                {releases && releases.length > 0
                  ? "Build and release firmware for this chip target first."
                  : "Build firmware and deploy webapp for a tag first."}
              </p>
            )}
            {filteredReleases && filteredReleases.length > 0 && (
              <div>
                <label
                  style={{
                    color: "#94a3b8",
                    fontSize: "0.85rem",
                    display: "block",
                    marginBottom: "0.25rem",
                  }}
                >
                  Version
                </label>
                <select
                  value={selectedTag}
                  onChange={(e) =>
                    setSelectedTag((e.target as HTMLSelectElement).value)
                  }
                  style={{
                    padding: "0.4rem",
                    background: "#0f172a",
                    color: "#e2e8f0",
                    border: "1px solid #334155",
                    borderRadius: "6px",
                    fontSize: "0.9rem",
                    width: "100%",
                  }}
                >
                  {filteredReleases.map((r) => (
                    <option key={r.tag} value={r.tag}>
                      {r.tag}
                    </option>
                  ))}
                </select>
                <p
                  style={{
                    color: "#64748b",
                    fontSize: "0.8rem",
                    marginTop: "0.5rem",
                  }}
                >
                  Will download bootloader, partition table, and firmware for{" "}
                  {effectiveChipFamily.toUpperCase()} from this hosted release.
                </p>
              </div>
            )}
            {!releases && !loadingReleases && (
              <div>
                <button
                  onClick={fetchReleases}
                  style={{
                    padding: "0.4rem 0.8rem",
                    background: "#374151",
                    color: "white",
                    border: "none",
                    borderRadius: "6px",
                    cursor: "pointer",
                  }}
                >
                  Load Hosted Releases
                </button>
              </div>
            )}
          </div>
        )}
      </div>

      {/* Step 3: Flash */}
      <div
        style={{
          marginBottom: "1.5rem",
          padding: "1rem",
          background: "#1e293b",
          borderRadius: "8px",
        }}
      >
        <h3 style={{ marginTop: 0, fontSize: "1rem" }}>3. Flash</h3>
        <button
          onClick={handleFlash}
          disabled={!canFlash}
          style={{
            padding: "0.6rem 1.5rem",
            background: canFlash ? "#f97316" : "#374151",
            color: "white",
            border: "none",
            borderRadius: "6px",
            fontSize: "1rem",
            cursor: canFlash ? "pointer" : "not-allowed",
            opacity: canFlash ? 1 : 0.5,
          }}
        >
          {flashing ? "Flashing..." : "Flash Firmware"}
        </button>

        {/* Progress bars */}
        {fileProgress.length > 0 && (
          <div style={{ marginTop: "1rem" }}>
            {fileProgress.map((pct, i) => {
              const label =
                source === "files"
                  ? slots[i]?.label
                  : ["Bootloader", "Partition Table", "Firmware"][i];
              return (
                <div key={i} style={{ marginBottom: "0.5rem" }}>
                  <div
                    style={{
                      display: "flex",
                      justifyContent: "space-between",
                      fontSize: "0.8rem",
                      color: "#94a3b8",
                      marginBottom: "0.2rem",
                    }}
                  >
                    <span>{label}</span>
                    <span>{pct}%</span>
                  </div>
                  <div
                    style={{
                      height: "6px",
                      background: "#0f172a",
                      borderRadius: "3px",
                      overflow: "hidden",
                    }}
                  >
                    <div
                      style={{
                        height: "100%",
                        width: `${pct}%`,
                        background: pct === 100 ? "#22c55e" : "#f97316",
                        borderRadius: "3px",
                        transition: "width 0.2s",
                      }}
                    />
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </div>

      {/* Status & error */}
      {/* Post-flash: Set up WiFi */}
      {status && status.includes("Done!") && (
        <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px", marginBottom: "1rem", textAlign: "center" }}>
          <p style={{ color: "#22c55e", marginBottom: "0.5rem" }}>Flash complete!</p>
          <button
            onClick={async () => {
              await closeTransport();
              nav("/provision");
            }}
            style={{ padding: "0.5rem 1.5rem", background: "#3b82f6", color: "white", border: "none", borderRadius: "6px", cursor: "pointer" }}
          >
            Set up WiFi
          </button>
        </div>
      )}

      {status && !status.includes("Done!") && (
        <p style={{ color: "#94a3b8", marginBottom: "0.5rem" }}>{status}</p>
      )}
      {error && (
        <p style={{ color: "#ef4444", marginBottom: "0.5rem" }}>{error}</p>
      )}

      {/* Console log */}
      {logLines.length > 0 && (
        <div
          ref={logRef}
          style={{
            background: "#0f172a",
            border: "1px solid #334155",
            borderRadius: "8px",
            padding: "0.75rem",
            maxHeight: "200px",
            overflow: "auto",
            fontFamily: "monospace",
            fontSize: "0.75rem",
            color: "#94a3b8",
            lineHeight: "1.4",
            whiteSpace: "pre-wrap",
            wordBreak: "break-all",
          }}
        >
          {logLines.map((line, i) => (
            <div key={i}>{line}</div>
          ))}
        </div>
      )}
    </div>
  );
}

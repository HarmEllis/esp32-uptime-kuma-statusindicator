# ESP32 / ESP32-S3 Uptime Kuma Status Indicator

A physical status indicator for [Uptime Kuma](https://github.com/louislam/uptime-kuma) powered by an ESP32 or ESP32-S3.

The device polls one or more Uptime Kuma instances via their Prometheus `/metrics` endpoint and shows the aggregate monitor status via LEDs. A companion Progressive Web App (hosted on GitHub Pages) handles firmware flashing, WiFi provisioning, and device configuration — all from the browser over USB or local network. The webapp automatically detects the connected chip family and selects the correct firmware and flash settings.

---

## Features

- **Multi-target support** — builds for ESP32 (GPIO LEDs) and ESP32-S3 (WS2812 RGB LED on GPIO 48)
- **3-LED status display** — green (all up), red (some down / unreachable / bad API key), onboard LED for connectivity state (ESP32); equivalent RGB colours on ESP32-S3
- **Multiple Uptime Kuma instances** — poll up to 8 instances simultaneously
- **REST API** — full CRUD for instances, settings, and live monitor status
- **AP mode fallback** — starts a `UptimeKumaMonitor` WiFi access point when no credentials are stored so you can configure it without a serial connection
- **Improv Serial** — provision WiFi from the browser via USB (Web Serial API)
- **Browser-based flashing** — flash the firmware directly from the GitHub Pages PWA using esptool-js
- **Secure auth** — HMAC-SHA256 challenge/response; all write endpoints require a bearer token
- **Self-signed HTTPS support** — connect to Uptime Kuma instances behind self-signed TLS certificates
- **PWA** — installable webapp that works offline for the Flash/Provision flow

---

## Hardware

Two target boards are supported:

### ESP32 (original)

| Component | Details |
|-----------|---------|
| MCU | ESP32 (any variant with at least 4 MB flash) |
| Green LED | GPIO 22 — all monitors up |
| Red LED | GPIO 23 — some monitors down / unreachable / invalid API key |
| Onboard LED | GPIO 2 — connectivity state (slow blink = connecting, fast blink = AP mode) |
| Button | GPIO 0 (BOOT) — hold 10 s for factory reset |

### ESP32-S3

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3 (at least 4 MB flash) |
| RGB LED | GPIO 48 — WS2812 single LED (green = all up, red = down, blue = connecting, white = identify/reset) |
| Button | GPIO 0 (BOOT) — hold 10 s for factory reset |

> **Note:** GPIO 22/23 are not usable on the ESP32-S3 (reserved for internal flash). The bootloader flash offset also differs: ESP32 = `0x1000`, ESP32-S3 = `0x0`. The webapp handles this automatically after chip detection.

---

## Quick Start

### 1. Flash the firmware

Open the [GitHub Pages webapp](https://harmEllis.github.io/esp32-uptime-kuma-statusindicator/) in Chrome, Edge, or Opera (Web Serial API required).

1. Click **Flash Firmware**
2. Select a firmware release from the dropdown
3. Connect your ESP32 via USB and click **Flash**

Alternatively build from source (requires [ESP-IDF v5.3+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/)):

```sh
cd firmware
# For ESP32:
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash

# For ESP32-S3:
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

### 2. Provision WiFi

After flashing, the onboard LED blinks fast — the device is in AP mode with SSID `UptimeKumaMonitor` (password `FFzppG3oJ76PRs`).

**Option A — Improv Serial (browser, USB):**

1. In the webapp, click **Set up WiFi**
2. Connect the ESP32 via USB and click **Connect to ESP32**
3. Enter your WiFi SSID and password, then click **Provision WiFi**
4. The device connects and reports its IP address

**Option B — AP web interface:**

1. Connect your phone or laptop to the `UptimeKumaMonitor` access point
2. Open the webapp and go to **Connect to Device** — enter `http://192.168.4.1`
3. Authenticate, then use **Settings → Update WiFi** to save credentials
4. The device reboots and connects to your network

### 3. Connect the webapp to the device

1. In the webapp, click **Connect to Device** (or navigate to `/setup`)
2. Enter the device address — either `http://esp-uptimemonitor.local` (mDNS) or the IP shown during provisioning
3. Enter the default password `**changeme**` — you will be prompted to change it
4. You are now on the **Dashboard**

> **Security:** Change the device password immediately after first login via **Settings → Change Password**.

### 4. Add Uptime Kuma instances

1. Go to **Instances → + Add Instance**
2. Enter a name, the Uptime Kuma base URL (e.g. `https://kuma.example.com`), and an API key
3. Click **Save** — the device starts polling immediately

The green LED turns on when all monitors across all instances are up. The red LED indicates one or more monitors are down, the instance is unreachable, or the API key is invalid.

---

## REST API

All endpoints are on port 80. Write endpoints require `Authorization: Bearer <token>`.

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/api/v1/health` | — | Device status, uptime, RSSI, heap, monitor aggregate |
| POST | `/api/v1/auth/challenge` | — | Get HMAC challenge |
| POST | `/api/v1/auth/login` | — | Authenticate with HMAC response, receive token |
| GET | `/api/v1/instances` | ✓ | List configured Uptime Kuma instances |
| POST | `/api/v1/instances` | ✓ | Add an instance |
| PUT | `/api/v1/instances/:id` | ✓ | Update an instance |
| DELETE | `/api/v1/instances/:id` | ✓ | Delete an instance |
| GET | `/api/v1/monitor/status` | ✓ | Per-instance poll results |
| GET | `/api/v1/settings` | ✓ | Device settings (hostname, SSID, IP, poll interval) |
| PUT | `/api/v1/settings/wifi` | ✓ | Update WiFi credentials (reboots device) |
| PUT | `/api/v1/settings/psk` | ✓ | Change device password |
| PUT | `/api/v1/settings/poll` | ✓ | Update poll interval (5–3600 s) |
| POST | `/api/v1/system/reboot` | ✓ | Reboot the device |

---

## Configuration

All settings are persisted to NVS flash and survive reboots. Default values:

| Setting | Default |
|---------|---------|
| Hostname | `esp-uptimemonitor` |
| PSK (password) | `changeme` |
| Poll interval | `60` seconds |
| WiFi SSID | _(not set — AP mode)_ |
| AP SSID | `UptimeKumaMonitor` |
| AP password | `FFzppG3oJ76PRs` |

---

## Development

### Firmware

```sh
cd firmware
# ESP32:
idf.py set-target esp32 && idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

# ESP32-S3:
idf.py set-target esp32s3 && idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

A `.devcontainer` is provided for VS Code Dev Containers (includes ESP-IDF 5.3).

### Webapp

```sh
cd webapp
npm install
npm run dev
# → http://localhost:5173/esp32-uptime-kuma-statusindicator/
```

```sh
npm run build   # production build → dist/
```

---

## CI / CD

| Workflow | Trigger | Output |
|----------|---------|--------|
| `build-firmware.yml` | Push tag `v*` | GitHub Release with 6 binaries: `firmware-esp32.bin`, `bootloader-esp32.bin`, `partition-table-esp32.bin` (and matching `-esp32s3` variants) |
| `deploy-webapp.yml` | Push to `main` or release | Downloads latest release binaries, builds PWA, deploys to GitHub Pages |

---

## LED States

| State | LED |
|-------|-----|
| Starting up | All off |
| Connecting to WiFi | Onboard slow blink (500 ms) |
| AP mode (no credentials) | Onboard fast blink (250 ms) |
| All monitors up | Green solid |
| Some monitors down | Red solid |
| Unreachable / API key invalid | Red fast blink |
| Improv Serial identify | All 3 LEDs triple-flash |
| Factory reset warning (hold 5 s) | Green + red fast blink |
| Factory reset confirmed (10 s) | All solid |

---

## License

MIT — see [LICENSE](LICENSE).

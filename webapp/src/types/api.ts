export interface UptimeInstance {
  id: number;
  uuid: string;
  name: string;
  url: string;
  apikey: string;
}

export interface DeviceHealth {
  status: string;
  uptime_s: number;
  rssi: number;
  free_heap: number;
  ip: string;
  psk_configured: boolean;
  monitor_status: string;
}

export interface DeviceSettings {
  hostname: string;
  wifi_ssid: string;
  ip: string;
  poll_interval: number;
  led_brightness?: number;  // 0–100, only present on ESP32-S3
}

export interface MonitorInstanceStatus {
  id: number;
  name: string;
  reachable: boolean;
  key_valid: boolean;
  up: number;
  down: number;
}

export interface MonitorStatus {
  status: string;
  instances: MonitorInstanceStatus[];
}

export interface AuthChallenge {
  challenge: string;
}

export interface AuthToken {
  token: string;
}

export interface ApiError {
  error: string;
}

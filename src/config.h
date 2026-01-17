#pragma once

#include <Arduino.h>

namespace config {

// Wi-Fi credentials (replace with your network).
constexpr const char* WIFI_SSID = "YOUR_WIFI_SSID";
constexpr const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Backend configuration.
// Example: "http://192.168.1.100:8000" or "https://api.example.com"
constexpr const char* BACKEND_BASE_URL = "http://127.0.0.1:8000";

// Device identity (must match backend dispenser record).
constexpr const char* DEVICE_SERIAL_ID = "S-20260117-0001";
constexpr const char* DEVICE_SECRET = "YOUR-DEVICE-SECRET";

// Poll interval for schedules/config (ms).
constexpr uint32_t POLL_INTERVAL_MS = 5000;

// TLS root certificate (PEM). Leave empty to skip setCACert (not recommended for production).
constexpr const char* ROOT_CA_PEM = "";

// HTTP timeouts (ms).
constexpr uint16_t HTTP_TIMEOUT_MS = 5000;

}  // namespace config


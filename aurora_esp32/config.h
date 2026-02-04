#pragma once

#include <Arduino.h>

namespace config {

// Wi-Fi credentials (replace with your network).
constexpr const char* WIFI_SSID = "YOUR_WIFI_SSID";
constexpr const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Backend configuration.
constexpr const char* BACKEND_BASE_URL = "http://127.0.0.1:8000";

// Device identity (must match backend dispenser record).
constexpr const char* DEVICE_SERIAL_ID = "S-20260117-0001";
constexpr const char* DEVICE_SECRET = "YOUR-DEVICE-SECRET";

// Poll interval for schedules/config (ms).
constexpr uint32_t POLL_INTERVAL_MS = 5000;

// I2C configuration for DS3231 (adjust pins for your ESP32-S3 board).
constexpr uint8_t I2C_SDA_PIN = 8;
constexpr uint8_t I2C_SCL_PIN = 9;
constexpr uint32_t I2C_FREQUENCY_HZ = 100000;

// ULN2003 stepper driver pins (IN1..IN4). Rewire to match these.
constexpr uint8_t STEPPER_IN1_PIN = 4;
constexpr uint8_t STEPPER_IN2_PIN = 5;
constexpr uint8_t STEPPER_IN3_PIN = 6;
constexpr uint8_t STEPPER_IN4_PIN = 7;

// Stepper motor calibration.
constexpr int STEPS_PER_REV = 2048;  // Common for 28BYJ-48 with gearbox.
constexpr int DEGREES_PER_DROP = 36;
constexpr int STEPPER_RPM = 10;

// TLS root certificate (PEM). Leave empty to skip setCACert (not recommended for production).
constexpr const char* ROOT_CA_PEM = "";

// HTTP timeouts (ms).
constexpr uint16_t HTTP_TIMEOUT_MS = 5000;

}  // namespace config


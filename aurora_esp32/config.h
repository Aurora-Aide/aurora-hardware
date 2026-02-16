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

// I2C configuration 
constexpr uint8_t I2C_SDA_PIN = 8;
constexpr uint8_t I2C_SCL_PIN = 9;
constexpr uint32_t I2C_FREQUENCY_HZ = 100000;

// ULN2003 stepper driver pins
constexpr uint8_t STEPPER1_IN1_PIN = 4;
constexpr uint8_t STEPPER1_IN2_PIN = 5;
constexpr uint8_t STEPPER1_IN3_PIN = 6;
constexpr uint8_t STEPPER1_IN4_PIN = 7;

constexpr uint8_t STEPPER2_IN1_PIN = 15;
constexpr uint8_t STEPPER2_IN2_PIN = 16;
constexpr uint8_t STEPPER2_IN3_PIN = 17;
constexpr uint8_t STEPPER2_IN4_PIN = 18;

constexpr uint8_t STEPPER3_IN1_PIN = 35;
constexpr uint8_t STEPPER3_IN2_PIN = 36;
constexpr uint8_t STEPPER3_IN3_PIN = 37;
constexpr uint8_t STEPPER3_IN4_PIN = 38;

// Stepper motor calibration.
constexpr int STEPS_PER_REV = 2048;
constexpr int DEGREES_PER_DROP = 36;
constexpr int STEPPER_RPM = 10;

// TLS root certificate (PEM). Leave empty to skip setCACert (not recommended for production).
constexpr const char* ROOT_CA_PEM = "";

// HTTP timeouts (ms).
constexpr uint16_t HTTP_TIMEOUT_MS = 5000;

}  // namespace config


#pragma once

#include <Arduino.h>

namespace config {

// Wi-Fi credentials (replace with your network).
constexpr const char* WIFI_SSID = "Di1";
constexpr const char* WIFI_PASSWORD = "15301530";

// Backend configuration.
constexpr const char* BACKEND_BASE_URL = "http://192.168.1.131:8000";

// Device identity (must match backend dispenser record).
constexpr const char* DEVICE_SERIAL_ID = "L-20260218-0022";

// Poll interval for schedules/config (ms).
constexpr uint32_t POLL_INTERVAL_MS = 30000;

// I2C configuration 
constexpr uint8_t I2C_SDA_PIN = 8;
constexpr uint8_t I2C_SCL_PIN = 9;
constexpr uint32_t I2C_FREQUENCY_HZ = 100000;

// RTC hardware toggle:
// - true  -> use DS3231 on I2C
// - false -> skip DS3231 and use NTP fallback only
constexpr bool USE_HARDWARE_RTC = false;

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

// Catch-up tolerance: if we're later than this, DON'T dispense; mark missed instead.
constexpr uint32_t CATCHUP_TOLERANCE_SECONDS = 3600;  // 1 hour

// NTP fallback (used when DS3231 is not present).
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.nist.gov";
constexpr long NTP_GMT_OFFSET_SECONDS = 7200;        // timezone base offset (UTC+2)
constexpr int NTP_DAYLIGHT_OFFSET_SECONDS = 3600;       // extra DST offset: use 0 or 3600

}  // namespace config

// Motor logging: set to 1 to print PROGRESS spam during rotation.
#define ROTATION_LOG_PROGRESS 0

// Optional hardware self-test: rotates one motor every 10s regardless of schedules.
// Use this to verify wiring/power quickly.
#define ENABLE_MOTOR_SELF_TEST 0
#define MOTOR_SELF_TEST_SLOT 1


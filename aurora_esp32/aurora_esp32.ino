#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <RTClib.h>
#include <Stepper.h>

#include "backend_client.h"
#include "config.h"
#include "schedule_store.h"

ScheduleStore schedule_store;
BackendClient backend_client;

unsigned long last_poll_ms = 0;

RTC_DS3231 rtc;
bool rtc_ready = false;

Stepper stepper_motor(
    config::STEPS_PER_REV,
    config::STEPPER_IN1_PIN,
    config::STEPPER_IN3_PIN,
    config::STEPPER_IN2_PIN,
    config::STEPPER_IN4_PIN);

struct DispenseState {
  int id = -1;
  int last_key = -1;
  bool nonrepeat_done = false;
};

std::vector<DispenseState> dispense_states;

DispenseState& stateForSchedule(int schedule_id) {
  for (auto& s : dispense_states) {
    if (s.id == schedule_id) return s;
  }
  dispense_states.push_back({schedule_id, -1, false});
  return dispense_states.back();
}

int currentKey(uint8_t dow, uint8_t hour, uint8_t minute) {
  return static_cast<int>(dow) * 24 * 60 + static_cast<int>(hour) * 60 + static_cast<int>(minute);
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[wifi] Connecting to %s...\n", config::WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[wifi] Failed to connect");
  }
}

void initRtc() {
  Wire.begin(config::I2C_SDA_PIN, config::I2C_SCL_PIN);
  Wire.setClock(config::I2C_FREQUENCY_HZ);

  if (!rtc.begin()) {
    Serial.println("[rtc] DS3231 not found");
    rtc_ready = false;
    return;
  }

  if (rtc.lostPower()) {
    Serial.println("[rtc] Lost power, setting to compile time");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  DateTime now = rtc.now();
  Serial.printf("[rtc] %04d-%02d-%02d %02d:%02d:%02d\n",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
  rtc_ready = true;
}

void dispenseStep() {
  long steps = (static_cast<long>(config::STEPS_PER_REV) * config::DEGREES_PER_DROP) / 360L;
  if (steps <= 0) return;
  stepper_motor.step(steps);
}

void checkSchedulesAndDispense() {
  if (!rtc_ready) return;

  DateTime now = rtc.now();
  uint8_t dow_backend = (now.dayOfTheWeek() + 6) % 7;  // RTC: 0=Sunday, backend: 0=Monday
  uint8_t hour = now.hour();
  uint8_t minute = now.minute();
  int key = currentKey(dow_backend, hour, minute);

  for (const auto& c : schedule_store.containers()) {
    for (const auto& s : c.schedules) {
      if (s.day_of_week != dow_backend || s.hour != hour || s.minute != minute) continue;

      DispenseState& state = stateForSchedule(s.id);
      if (!s.repeat && state.nonrepeat_done) continue;
      if (state.last_key == key) continue;

      Serial.printf("[dispense] slot=%d schedule=%d %02u:%02u\n",
                    c.slot_number, s.id, s.hour, s.minute);
      dispenseStep();

      state.last_key = key;
      if (!s.repeat) state.nonrepeat_done = true;
    }
  }
}

void pollConfigIfNeeded() {
  unsigned long now = millis();
  if (now - last_poll_ms < config::POLL_INTERVAL_MS) return;
  last_poll_ms = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[poll] Skipping poll, Wi-Fi down");
    return;
  }

  bool ok = backend_client.fetchConfig(schedule_store);
  if (!ok) {
    Serial.println("[poll] Fetch failed");
    return;
  }

  // Print schedules to verify they are in memory.
  Serial.printf("[poll] Schedule version: %lld\n", static_cast<long long>(schedule_store.version()));
  for (const auto& c : schedule_store.containers()) {
    Serial.printf("  Slot %d (%s): %d entries\n", c.slot_number, c.pill_name.c_str(),
                  static_cast<int>(c.schedules.size()));
    for (const auto& s : c.schedules) {
      Serial.printf("    id=%d dow=%u %02u:%02u repeat=%s\n", s.id, s.day_of_week, s.hour,
                    s.minute, s.repeat ? "true" : "false");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  connectWiFi();
  initRtc();
  stepper_motor.setSpeed(config::STEPPER_RPM);
  // Fetch immediately on boot.
  pollConfigIfNeeded();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  pollConfigIfNeeded();
  checkSchedulesAndDispense();
  delay(50);
}

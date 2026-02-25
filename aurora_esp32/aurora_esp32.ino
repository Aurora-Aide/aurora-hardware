#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <RTClib.h>
#include <Stepper.h>
#include <Preferences.h>
#include <vector>

#include "backend_client.h"
#include "config.h"
#include "schedule_store.h"

ScheduleStore schedule_store;
BackendClient backend_client;

unsigned long last_poll_ms = 0;

RTC_DS3231 rtc;
bool rtc_ready = false;

Preferences drop_prefs;
bool drop_prefs_ready = false;

Stepper stepper_motor_1(
    config::STEPS_PER_REV,
    config::STEPPER1_IN1_PIN,
    config::STEPPER1_IN3_PIN,
    config::STEPPER1_IN2_PIN,
    config::STEPPER1_IN4_PIN);

Stepper stepper_motor_2(
    config::STEPS_PER_REV,
    config::STEPPER2_IN1_PIN,
    config::STEPPER2_IN3_PIN,
    config::STEPPER2_IN2_PIN,
    config::STEPPER2_IN4_PIN);

Stepper stepper_motor_3(
    config::STEPS_PER_REV,
    config::STEPPER3_IN1_PIN,
    config::STEPPER3_IN3_PIN,
    config::STEPPER3_IN2_PIN,
    config::STEPPER3_IN4_PIN);

String formatISO8601(const DateTime& dt) {
  // Format: YYYY-MM-DDTHH:MM:SS
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
           dt.year(), dt.month(), dt.day(),
           dt.hour(), dt.minute(), dt.second());
  return String(buf);
}

void ensureDropPrefs() {
  if (drop_prefs_ready) return;
  drop_prefs.begin("drops", false);
  drop_prefs_ready = true;
}

uint32_t getLastDropTs(int schedule_id) {
  ensureDropPrefs();
  // Preferences key length is limited; keep it short.
  String key = "d" + String(schedule_id);
  return drop_prefs.getULong(key.c_str(), 0);
}

void setLastDropTs(int schedule_id, uint32_t unix_ts) {
  ensureDropPrefs();
  String key = "d" + String(schedule_id);
  drop_prefs.putULong(key.c_str(), unix_ts);
}

DateTime startOfBackendWeek(const DateTime& now) {
  // Backend contract: 0=Mon..6=Sun
  uint8_t dow_backend = (now.dayOfTheWeek() + 6) % 7;
  uint32_t midnight = DateTime(now.year(), now.month(), now.day(), 0, 0, 0).unixtime();
  uint32_t week_start_unix = midnight - (uint32_t)dow_backend * 86400UL;
  return DateTime(week_start_unix);
}

DateTime idealOccurrenceThisWeek(const DateTime& week_start, const ScheduleEntry& s) {
  uint32_t ideal_unix =
      week_start.unixtime() +
      (uint32_t)s.day_of_week * 86400UL +
      (uint32_t)s.hour * 3600UL +
      (uint32_t)s.minute * 60UL;
  return DateTime(ideal_unix);
}

bool connectWiFi() {
  static bool configured = false;
  static bool started = false;
  static unsigned long lastAttempt = 0;
  static unsigned long lastFullReset = 0;

  if (!configured) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    configured = true;
  }

  if (WiFi.status() == WL_CONNECTED) return true;

  unsigned long now = millis();
  // Start connecting immediately on first call.
  if (!started) {
    started = true;
    lastAttempt = now;
    Serial0.printf("[wifi] begin %s\n", config::WIFI_SSID);
    WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);
    return false;
  }

  // Retry logic:
  // - Prefer reconnect() (keeps state) every 10s
  // - If we appear stuck for a long time, do a soft disconnect (don't erase) and begin again
  if (now - lastAttempt >= 10000) {
    lastAttempt = now;

    wl_status_t st = WiFi.status();
    Serial0.printf("[wifi] retry status=%d\n", (int)st);

    // If SSID isn't found, calling begin again is reasonable.
    if (st == WL_NO_SSID_AVAIL) {
      Serial0.printf("[wifi] begin %s (ssid not found)\n", config::WIFI_SSID);
      WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);
      return false;
    }

    // Try reconnect first (doesn't wipe config).
    WiFi.reconnect();

    // If still not connected for >60s, do a soft reset of the wifi link and begin again.
    if (now - lastFullReset >= 60000) {
      lastFullReset = now;
      Serial0.println("[wifi] soft reset + begin");
      WiFi.disconnect(false, false);
      delay(50);
      WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);
    }
  }

  return false;
}

void initRtc() {
  Serial0.println("[rtc] init start");

  Wire.begin(config::I2C_SDA_PIN, config::I2C_SCL_PIN);
  Wire.setClock(config::I2C_FREQUENCY_HZ);
  Wire.setTimeOut(100);               // <-- IMPORTANT (ms): prevents I2C hang -> WDT reset

  if (!rtc.begin()) {
    Serial0.println("[rtc] DS3231 not found");
    rtc_ready = false;
    return;
  }

  if (rtc.lostPower()) {
    Serial0.println("[rtc] Lost power, setting to compile time");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  DateTime now = rtc.now();
  Serial0.printf("[rtc] %04d-%02d-%02d %02d:%02d:%02d (DOW=%d)\n",
                 now.year(), now.month(), now.day(),
                 now.hour(), now.minute(), now.second(),
                 now.dayOfTheWeek());

  rtc_ready = true;
  Serial0.println("[rtc] init done");
}

Stepper* motorForSlot(int slot_number) {
  switch (slot_number) {
    case 1: return &stepper_motor_1;
    case 2: return &stepper_motor_2;
    case 3: return &stepper_motor_3;
    default: return nullptr;
  }
}

void dispenseStepForSlot(int slot_number) {
  Stepper* motor = motorForSlot(slot_number);
  if (!motor) {
    Serial0.printf("[rotation] ERROR: No motor mapped for slot %d\n", slot_number);
    return;
  }

  long steps = (static_cast<long>(config::STEPS_PER_REV) * config::DEGREES_PER_DROP) / 360L;
  if (steps <= 0) {
    Serial0.printf("[rotation] ERROR: Invalid step calculation for slot %d (steps=%ld)\n", slot_number, steps);
    return;
  }

  Serial0.printf("[rotation] START: Slot %d, rotating %ld steps (%d degrees)\n", 
                 slot_number, steps, config::DEGREES_PER_DROP);

  unsigned long rotation_start = millis();
  const int CHUNK = 50;
  long remaining = steps;
  int chunk_count = 0;
  
  while (remaining > 0) {
    int s = (remaining > CHUNK) ? CHUNK : static_cast<int>(remaining);
    motor->step(s);
    remaining -= s;
    chunk_count++;
    
    // Log progress every 10 chunks
    if (ROTATION_LOG_PROGRESS && (chunk_count % 10 == 0)) {
      long progress = steps - remaining;
      Serial0.printf("[rotation] PROGRESS: Slot %d, %ld/%ld steps (%.1f%%)\n",
                     slot_number, progress, steps, (progress * 100.0f / steps));
    }
    
    yield();
  }
  
  unsigned long rotation_duration = millis() - rotation_start;
  Serial0.printf("[rotation] COMPLETE: Slot %d finished in %lu ms (%ld total steps)\n",
                 slot_number, rotation_duration, steps);
}

void checkSchedulesAndDispense() {
  if (!rtc_ready) {
    static unsigned long last_warn = 0;
    if (millis() - last_warn > 5000) {
      last_warn = millis();
      Serial0.println("[schedule] SKIP: RTC not ready");
    }
    return;
  }

  // Check if we have any schedules loaded
  if (schedule_store.containers().empty()) {
    static unsigned long last_warn = 0;
    if (millis() - last_warn > 5000) {
      last_warn = millis();
      Serial0.println("[schedule] SKIP: No schedules loaded (config fetch may have failed)");
    }
    return;
  }

  DateTime now = rtc.now();
  uint8_t dow_backend = (now.dayOfTheWeek() + 6) % 7;
  uint8_t hour = now.hour();
  uint8_t minute = now.minute();
  uint32_t now_ts = now.unixtime();
  DateTime week_start = startOfBackendWeek(now);

  // Log current time periodically (every minute in real time)
  static int last_logged_minute = -1;
  if (minute != last_logged_minute) {
    last_logged_minute = minute;
    const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    Serial0.printf("[schedule] Current time: %s %02u:%02u (DOW=%u)\n",
                   days[dow_backend], hour, minute, dow_backend);
  }

  // Debug: log schedule checking (every 5 seconds)
  static unsigned long last_schedule_debug = 0;
  if (millis() - last_schedule_debug > 5000) {
    last_schedule_debug = millis();
    int total_schedules = 0;
    for (const auto& c : schedule_store.containers()) {
      total_schedules += c.schedules.size();
    }
    Serial0.printf("[schedule] Checking schedules: %zu containers, %d total schedules\n",
                   schedule_store.containers().size(), total_schedules);
    Serial0.printf("[schedule] Current time check: DOW=%u, Hour=%u, Minute=%u\n",
                   dow_backend, hour, minute);
  }

  for (const auto& c : schedule_store.containers()) {
    for (const auto& s : c.schedules) {
      if (s.id < 0) continue;
      if (c.slot_number <= 0) continue;

      // Smarter logic:
      // - Compute this week's ideal occurrence time for the schedule
      // - If now is AFTER the ideal time, and last_drop is BEFORE the ideal time => we missed it => drop now
      // - Otherwise don't drop (already dropped or not due yet)
      DateTime ideal = idealOccurrenceThisWeek(week_start, s);
      uint32_t ideal_ts = ideal.unixtime();

      if (now_ts < ideal_ts) {
        // Not due yet (we do NOT do early catch-up)
        continue;
      }

      uint32_t last_drop_ts = getLastDropTs(s.id);

      if (!s.repeat && last_drop_ts != 0) {
        // One-shot schedule already executed at least once
        continue;
      }

      if (last_drop_ts >= ideal_ts) {
        // Already dropped for (or after) this week's ideal time
        continue;
      }

      uint32_t late_s = now_ts - ideal_ts;
      String timestamp = formatISO8601(now);
      if (late_s > config::CATCHUP_TOLERANCE_SECONDS) {
        // Too late -> don't dispense. Mark missed, and advance last_drop to prevent spamming.
        Serial0.printf(
            "[pill] MISSED: slot=%d schedule=%d late=%lus (> %lus) now=%s ideal=%s last_drop_ts=%lu\n",
            c.slot_number,
            s.id,
            (unsigned long)late_s,
            (unsigned long)config::CATCHUP_TOLERANCE_SECONDS,
            formatISO8601(now).c_str(),
            formatISO8601(ideal).c_str(),
            (unsigned long)last_drop_ts);

        bool eventPosted = backend_client.postEvent("missed", timestamp, c.slot_number, s.id);
        if (eventPosted) {
          Serial0.printf("[event] SUCCESS: Posted 'missed' event to backend for Slot %d, Schedule %d\n",
                         c.slot_number, s.id);
        } else {
          Serial0.printf("[event] FAILED: Could not post 'missed' event to backend for Slot %d, Schedule %d\n",
                         c.slot_number, s.id);
        }

        // Treat as "handled" for this occurrence so we don't spam on every boot/loop.
        setLastDropTs(s.id, now_ts);
        Serial0.printf("[schedule] DONE: Slot %d schedule %d handled as MISSED (event %s)\n",
                       c.slot_number, s.id, eventPosted ? "posted" : "failed");
        continue;
      }

      Serial0.printf("[pill] DROP: slot=%d schedule=%d late=%lus now=%s ideal=%s last_drop_ts=%lu\n",
                     c.slot_number,
                     s.id,
                     (unsigned long)late_s,
                     formatISO8601(now).c_str(),
                     formatISO8601(ideal).c_str(),
                     (unsigned long)last_drop_ts);

      // Perform the actual dispensing (motor rotation)
      dispenseStepForSlot(c.slot_number);

      // Post event to backend
      bool eventPosted = backend_client.postEvent("completed", timestamp, c.slot_number, s.id);

      if (eventPosted) {
        Serial0.printf("[event] SUCCESS: Posted 'completed' event to backend for Slot %d, Schedule %d\n",
                       c.slot_number, s.id);
      } else {
        Serial0.printf("[event] FAILED: Could not post event to backend for Slot %d, Schedule %d\n",
                       c.slot_number, s.id);
      }

      // Persist last-drop so we can catch up after resets / downtime.
      setLastDropTs(s.id, now_ts);

      Serial0.printf("[schedule] DONE: Slot %d schedule %d completed (motor rotated, event %s)\n",
                     c.slot_number, s.id, eventPosted ? "posted" : "failed");
    }
  }
}

void pollConfigIfNeeded() {
  unsigned long now = millis();
  if (now - last_poll_ms < config::POLL_INTERVAL_MS) return;
  last_poll_ms = now;

  Serial0.println("[poll] tick");

  if (WiFi.status() != WL_CONNECTED) {
    Serial0.println("[poll] skip (wifi down)");
    return;
  }

  bool ok = backend_client.fetchConfig(schedule_store);
  if (!ok) {
    Serial0.println("[poll] fetch failed");
    return;
  }

  Serial0.printf("[poll] version: %lld\n", (long long)schedule_store.version());
}

void setup() {
  // UART0 (same port as ROM boot logs). Use this so you SEE prints for sure.
  Serial0.begin(115200);
  delay(200);
  Serial0.println("[setup] start");

  connectWiFi();

  Serial0.println("[setup] before rtc");
  initRtc();
  Serial0.println("[setup] after rtc");

  stepper_motor_1.setSpeed(config::STEPPER_RPM);
  stepper_motor_2.setSpeed(config::STEPPER_RPM);
  stepper_motor_3.setSpeed(config::STEPPER_RPM);

  Serial0.println("[setup] end");
}

void loop() {
  static unsigned long lastLog = 0;
  unsigned long now = millis();

  if (now - lastLog >= 2000) {
    lastLog = now;
    Serial0.printf("[loop] wifi=%s\n", (WiFi.status() == WL_CONNECTED) ? "up" : "down");
  }

  connectWiFi();
  pollConfigIfNeeded();
  checkSchedulesAndDispense();

  delay(50);
}

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <RTClib.h>
#include <Stepper.h>
#include <Preferences.h>
#include <time.h>
#include <vector>

#include "backend_client.h"
#include "config.h"
#include "schedule_store.h"

ScheduleStore schedule_store;
BackendClient backend_client;

unsigned long last_poll_ms = 0;

RTC_DS3231 rtc;
bool rtc_ready = false;
bool ntp_ready = false;
bool ntp_configured = false;
unsigned long last_ntp_attempt_ms = 0;

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

bool ensureNtpTime() {
  if (WiFi.status() != WL_CONNECTED) return false;

  unsigned long now_ms = millis();
  if (now_ms - last_ntp_attempt_ms < 10000) {
    return ntp_ready;
  }
  last_ntp_attempt_ms = now_ms;

  if (!ntp_configured) {
    long effective_offset = config::NTP_GMT_OFFSET_SECONDS + config::NTP_DAYLIGHT_OFFSET_SECONDS;
    Serial0.printf("[ntp] offsets: gmt=%ld dst=%d total=%ld seconds\n",
                   config::NTP_GMT_OFFSET_SECONDS,
                   config::NTP_DAYLIGHT_OFFSET_SECONDS,
                   effective_offset);
    if (config::NTP_DAYLIGHT_OFFSET_SECONDS != 0 && config::NTP_DAYLIGHT_OFFSET_SECONDS != 3600) {
      Serial0.println("[ntp] WARNING: DST offset should usually be 0 or 3600 seconds");
    }
    configTime(
        config::NTP_GMT_OFFSET_SECONDS,
        config::NTP_DAYLIGHT_OFFSET_SECONDS,
        config::NTP_SERVER_1,
        config::NTP_SERVER_2);
    ntp_configured = true;
    Serial0.println("[ntp] configured");
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) {
    Serial0.println("[ntp] sync failed");
    ntp_ready = false;
    return false;
  }

  if (!ntp_ready) {
    Serial0.printf("[ntp] synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                   timeinfo.tm_year + 1900,
                   timeinfo.tm_mon + 1,
                   timeinfo.tm_mday,
                   timeinfo.tm_hour,
                   timeinfo.tm_min,
                   timeinfo.tm_sec);
  }
  ntp_ready = true;
  return true;
}

bool getCurrentDateTime(DateTime& out, const char*& source) {
  if (rtc_ready) {
    out = rtc.now();
    source = "RTC";
    return true;
  }

  if (!ensureNtpTime()) {
    source = "NONE";
    return false;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) {
    ntp_ready = false;
    source = "NONE";
    return false;
  }

  out = DateTime(
      timeinfo.tm_year + 1900,
      timeinfo.tm_mon + 1,
      timeinfo.tm_mday,
      timeinfo.tm_hour,
      timeinfo.tm_min,
      timeinfo.tm_sec);
  source = "NTP";
  return true;
}

void logCurrentTimeIfNeeded() {
  static unsigned long last_log_ms = 0;
  unsigned long now_ms = millis();
  if (now_ms - last_log_ms < 10000) return;
  last_log_ms = now_ms;

  DateTime now;
  const char* source = "NONE";
  if (!getCurrentDateTime(now, source)) {
    Serial0.println("[time] source=NONE (RTC/NTP unavailable)");
    return;
  }

  uint8_t dow_backend = (now.dayOfTheWeek() + 6) % 7;
  const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  Serial0.printf("[time] source=%s now=%04d-%02d-%02d %s %02d:%02d:%02d (DOW=%u)\n",
                 source,
                 now.year(),
                 now.month(),
                 now.day(),
                 days[dow_backend],
                 now.hour(),
                 now.minute(),
                 now.second(),
                 dow_backend);
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

  if (!config::USE_HARDWARE_RTC) {
    rtc_ready = false;
    Serial0.println("[rtc] DS3231 disabled by config (NTP fallback mode)");
    return;
  }

  Wire.begin(config::I2C_SDA_PIN, config::I2C_SCL_PIN);
  Wire.setClock(config::I2C_FREQUENCY_HZ);
  Wire.setTimeOut(100);               // <-- IMPORTANT (ms): prevents I2C hang -> WDT reset

  if (!rtc.begin()) {
    Serial0.println("[rtc] DS3231 not found");
    rtc_ready = false;
    Serial0.println("[time] RTC unavailable, will try NTP fallback when Wi-Fi is up");
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
  DateTime now;
  const char* time_source = "NONE";
  if (!getCurrentDateTime(now, time_source)) {
    static unsigned long last_warn = 0;
    if (millis() - last_warn > 5000) {
      last_warn = millis();
      Serial0.println("[schedule] SKIP: No valid time source (RTC/NTP unavailable)");
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
    Serial0.printf("[schedule] Current time (%s): %s %02u:%02u (DOW=%u)\n",
                   time_source, days[dow_backend], hour, minute, dow_backend);
  }

  // Debug: log schedule checking (every 5 seconds)
  static unsigned long last_schedule_debug = 0;
  bool debug_window = false;
  if (millis() - last_schedule_debug > 5000) {
    last_schedule_debug = millis();
    debug_window = true;
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
      uint32_t last_drop_ts = getLastDropTs(s.id);

      if (debug_window) {
        long late_s_dbg = (now_ts >= ideal_ts) ? (long)(now_ts - ideal_ts) : -((long)(ideal_ts - now_ts));
        Serial0.printf(
            "[schedule] eval slot=%d id=%d cfg=%u %02u:%02u repeat=%s ideal=%s last_drop_ts=%lu late_s=%ld\n",
            c.slot_number,
            s.id,
            s.day_of_week,
            s.hour,
            s.minute,
            s.repeat ? "true" : "false",
            formatISO8601(ideal).c_str(),
            (unsigned long)last_drop_ts,
            late_s_dbg);
      }

      if (now_ts < ideal_ts) {
        // Not due yet (we do NOT do early catch-up)
        if (debug_window) Serial0.println("[schedule] reason=not_due_yet");
        continue;
      }

      static unsigned long last_skip_log_ms = 0;
      auto logSkipIfNeeded = [&](const char* reason) {
        unsigned long now_ms = millis();
        if (now_ms - last_skip_log_ms < 10000) return;
        last_skip_log_ms = now_ms;
        Serial0.printf(
            "[schedule] SKIP: %s slot=%d schedule=%d repeat=%s now=%s ideal=%s last_drop_ts=%lu\n",
            reason,
            c.slot_number,
            s.id,
            s.repeat ? "true" : "false",
            formatISO8601(now).c_str(),
            formatISO8601(ideal).c_str(),
            (unsigned long)last_drop_ts);
      };

      if (!s.repeat && last_drop_ts != 0) {
        // One-shot schedule already executed at least once
        logSkipIfNeeded("already handled non-repeat");
        continue;
      }

      if (last_drop_ts >= ideal_ts) {
        // Already dropped for (or after) this week's ideal time
        logSkipIfNeeded("already handled this occurrence");
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

void runMotorSelfTestIfEnabled() {
#if ENABLE_MOTOR_SELF_TEST
  static unsigned long last_test_ms = 0;
  unsigned long now = millis();
  if (now - last_test_ms < 10000) return;
  last_test_ms = now;
  Serial0.printf("[selftest] Rotating slot %d for hardware check\n", MOTOR_SELF_TEST_SLOT);
  dispenseStepForSlot(MOTOR_SELF_TEST_SLOT);
#endif
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
  logCurrentTimeIfNeeded();
  if (!rtc_ready) ensureNtpTime();
  pollConfigIfNeeded();
  runMotorSelfTestIfEnabled();
  checkSchedulesAndDispense();

  delay(50);
}

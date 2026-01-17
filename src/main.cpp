#include <Arduino.h>
#include <WiFi.h>

#include "backend_client.h"
#include "config.h"
#include "schedule_store.h"

ScheduleStore schedule_store;
BackendClient backend_client;

unsigned long last_poll_ms = 0;

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
  // Fetch immediately on boot.
  pollConfigIfNeeded();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  pollConfigIfNeeded();
  delay(50);
}


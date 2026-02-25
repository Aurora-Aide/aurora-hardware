#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "schedule_store.h"

class BackendClient {
 public:
  BackendClient();

  bool fetchConfig(ScheduleStore& store);
  bool postEvent(const String& status, const String& occurred_at_iso8601,
                 int container_slot, int schedule_id);

 private:
  WiFiClient plain_client_;
  WiFiClientSecure secure_client_;

  Preferences prefs_;
  bool prefs_ready_ = false;
  String device_secret_;
  bool is_paired_ = false;  // Track pairing status to avoid redundant pairing attempts

  void ensurePrefs();
  bool ensurePaired();
  bool pairDevice();

  void loadSecret();
  void saveSecret(const String& secret);

  String configUrl() const;
  String eventsUrl() const;
  String pairUrl() const;

  bool parseConfigPayload(const String& payload, ScheduleStore& store);

  static bool isHttpsBaseUrl();
  bool beginHttp(HTTPClient& http, const String& url);
  String deviceSecret() const;
};

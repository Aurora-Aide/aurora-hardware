#pragma once

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "config.h"
#include "schedule_store.h"

class BackendClient {
 public:
  BackendClient();

  // Fetch config/schedules for the configured dispenser.
  // Returns true on success and updates the store.
  bool fetchConfig(ScheduleStore& store);

  // Post an event back to the backend (completed/missed). Optional for now.
  bool postEvent(const String& status, const String& occurred_at_iso8601,
                 int container_slot = -1, int schedule_id = -1);

 private:
  bool parseConfigPayload(const String& payload, ScheduleStore& store);
  String configUrl() const;
  String eventsUrl() const;

  WiFiClientSecure secure_client_;
};


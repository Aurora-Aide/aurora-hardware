#include "backend_client.h"

#include <Arduino.h>

BackendClient::BackendClient() {
  secure_client_.setTimeout(config::HTTP_TIMEOUT_MS / 1000);
  if (strlen(config::ROOT_CA_PEM) > 0) {
    secure_client_.setCACert(config::ROOT_CA_PEM);
  } else {
    // WARNING: insecure; fine for local testing only.
    secure_client_.setInsecure();
  }
}

String BackendClient::configUrl() const {
  String url = config::BACKEND_BASE_URL;
  if (!url.endsWith("/")) url += "/";
  url += "dispensers/devices/";
  url += config::DEVICE_SERIAL_ID;
  url += "/config/";
  return url;
}

String BackendClient::eventsUrl() const {
  String url = config::BACKEND_BASE_URL;
  if (!url.endsWith("/")) url += "/";
  url += "dispensers/devices/";
  url += config::DEVICE_SERIAL_ID;
  url += "/events/";
  return url;
}

bool BackendClient::parseConfigPayload(const String& payload, ScheduleStore& store) {
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[backend] JSON parse error: %s\n", err.c_str());
    return false;
  }

  int64_t version = doc["schedule_version"] | 0;
  JsonArray containers_json = doc["containers"].as<JsonArray>();
  std::vector<ContainerSchedules> containers;
  containers.reserve(containers_json.size());

  for (JsonObject c : containers_json) {
    ContainerSchedules cs;
    cs.slot_number = c["slot_number"] | -1;
    cs.pill_name = String((const char*)c["pill_name"] | "");

    JsonArray schedules_json = c["schedules"].as<JsonArray>();
    cs.schedules.reserve(schedules_json.size());
    for (JsonObject s : schedules_json) {
      ScheduleEntry entry;
      entry.id = s["id"] | -1;
      entry.day_of_week = s["day_of_week"] | 0;
      entry.hour = s["hour"] | 0;
      entry.minute = s["minute"] | 0;
      entry.repeat = s["repeat"] | true;
      cs.schedules.push_back(entry);
    }
    containers.push_back(std::move(cs));
  }

  store.applyConfig(version, std::move(containers));
  return true;
}

bool BackendClient::fetchConfig(ScheduleStore& store) {
  HTTPClient http;
  http.setTimeout(config::HTTP_TIMEOUT_MS);

  String url = configUrl();
  Serial.printf("[backend] GET %s\n", url.c_str());

  if (!http.begin(secure_client_, url)) {
    Serial.println("[backend] Failed to begin HTTP connection");
    return false;
  }

  http.addHeader("X-Device-Secret", config::DEVICE_SECRET);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[backend] GET failed, code: %d\n", code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  bool ok = parseConfigPayload(payload, store);
  if (ok) {
    Serial.printf("[backend] Updated schedules to version %lld\n", static_cast<long long>(store.version()));
  }
  return ok;
}

bool BackendClient::postEvent(const String& status, const String& occurred_at_iso8601,
                              int container_slot, int schedule_id) {
  HTTPClient http;
  http.setTimeout(config::HTTP_TIMEOUT_MS);

  String url = eventsUrl();
  if (!http.begin(secure_client_, url)) {
    Serial.println("[backend] Failed to begin HTTP connection for events");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Secret", config::DEVICE_SECRET);

  StaticJsonDocument<256> doc;
  doc["status"] = status;
  doc["occurred_at"] = occurred_at_iso8601;
  if (container_slot >= 0) doc["container_slot"] = container_slot;
  if (schedule_id >= 0) doc["schedule_id"] = schedule_id;

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  if (code != HTTP_CODE_NO_CONTENT) {
    Serial.printf("[backend] Event POST failed, code: %d\n", code);
    http.end();
    return false;
  }

  http.end();
  return true;
}


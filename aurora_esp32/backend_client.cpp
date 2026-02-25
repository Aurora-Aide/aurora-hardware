#include "backend_client.h"
#include <Arduino.h>
#include <WiFi.h>

static void printHttpResult(int code, const String& payload) {
  Serial0.printf("[http] code=%d\n", code);
  Serial0.printf("[http] body[0..200]=%s\n", payload.substring(0, 200).c_str());
}

BackendClient::BackendClient() {
  plain_client_.setTimeout(config::HTTP_TIMEOUT_MS);
  secure_client_.setTimeout(config::HTTP_TIMEOUT_MS);

  // Only configure TLS if we actually use https://
  if (isHttpsBaseUrl()) {
    if (strlen(config::ROOT_CA_PEM) > 0) {
      secure_client_.setCACert(config::ROOT_CA_PEM);
    } else {
    }
  }
}

bool BackendClient::isHttpsBaseUrl() {
  return strncmp(config::BACKEND_BASE_URL, "https://", 8) == 0;
}

bool BackendClient::beginHttp(HTTPClient& http, const String& url) {
  if (isHttpsBaseUrl()) return http.begin(secure_client_, url);
  return http.begin(plain_client_, url);
}

void BackendClient::ensurePrefs() {
  if (prefs_ready_) return;
  prefs_.begin("aurora", false);
  prefs_ready_ = true;
  loadSecret();
}

String BackendClient::configUrl() const {
  String url = config::BACKEND_BASE_URL;
  if (!url.endsWith("/")) url += "/";
  url += "api/devices/";
  url += config::DEVICE_SERIAL_ID;
  url += "/config/";
  return url;
}

String BackendClient::eventsUrl() const {
  String url = config::BACKEND_BASE_URL;
  if (!url.endsWith("/")) url += "/";
  url += "api/devices/";
  url += config::DEVICE_SERIAL_ID;
  url += "/events/";
  return url;
}

String BackendClient::pairUrl() const {
  String url = config::BACKEND_BASE_URL;
  if (!url.endsWith("/")) url += "/";
  url += "api/devices/";
  url += config::DEVICE_SERIAL_ID;
  url += "/pair/";
  return url;
}

void BackendClient::loadSecret() {
  if (!prefs_ready_) return;
  device_secret_ = prefs_.getString("device_secret", "");
  if (device_secret_.length() > 0) {
    is_paired_ = true;
    Serial0.println("[backend] Loaded device secret from NVS (already paired)");
  } else {
    is_paired_ = false;
    Serial0.println("[backend] No device secret in NVS (not paired)");
  }
}

void BackendClient::saveSecret(const String& secret) {
  ensurePrefs();
  device_secret_ = secret;
  prefs_.putString("device_secret", secret);
}

String BackendClient::deviceSecret() const {
  return device_secret_;
}

bool BackendClient::ensurePaired() {
  ensurePrefs();
  
  if (device_secret_.length() > 0) {
    is_paired_ = true;
    return true;
  }

  // No secret => keep trying to pair (backend might get reset later)
  Serial0.println("[backend] Device not paired, attempting to pair...");
  bool success = pairDevice();
  if (success) is_paired_ = true;
  return success;
}

bool BackendClient::pairDevice() {
  // Prevent redundant pairing attempts
  if (is_paired_ && device_secret_.length() > 0) {
    Serial0.println("[backend] Already paired, skipping pair request");
    return true;
  }
  
  Serial0.println("[backend] pair ENTER");
  Serial0.flush();

  HTTPClient http;
  http.setTimeout(config::HTTP_TIMEOUT_MS);

  String url = pairUrl();
  Serial0.printf("[backend] POST %s\n", url.c_str());

  if (!beginHttp(http, url)) {
    Serial0.println("[backend] http.begin FAILED (pair)");
    return false;
  }

  int code = http.POST("");
  String payload = http.getString();
  http.end();

  printHttpResult(code, payload);

  // Handle 409 Conflict - device already paired on backend
  if (code == HTTP_CODE_CONFLICT || code == 409) {
    Serial0.println("[backend] Device already paired on backend (409 Conflict)");
    Serial0.println("[backend] WARNING: Secret not in NVS - cannot authenticate!");
    Serial0.println("[backend] SOLUTION: Reset device pairing on backend OR manually restore the secret in NVS");
    return false;  // Return false because we don't have the secret
  }

  if (code != HTTP_CODE_OK) return false;

  StaticJsonDocument<256> doc;
  auto err = deserializeJson(doc, payload);
  if (err) {
    Serial0.printf("[backend] Pair JSON parse error: %s\n", err.c_str());
    return false;
  }

  const char* secret = doc["device_secret"] | "";
  if (strlen(secret) == 0) {
    Serial0.println("[backend] Pairing response missing device_secret");
    return false;
  }

  saveSecret(String(secret));
  is_paired_ = true;
  Serial0.println("[backend] Pairing successful; secret stored");
  return true;
}

bool BackendClient::parseConfigPayload(const String& payload, ScheduleStore& store) {
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial0.printf("[backend] JSON parse error: %s\n", err.c_str());
    return false;
  }

  int64_t version = doc["schedule_version"] | 0;
  JsonArray containers_json = doc["containers"].as<JsonArray>();

  std::vector<ContainerSchedules> containers;
  containers.reserve(containers_json.size());

  for (JsonObject c : containers_json) {
    ContainerSchedules cs;
    cs.slot_number = c["slot_number"] | -1;
    cs.pill_name = String(c["pill_name"] | "");

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
  Serial0.println("[backend] fetchConfig ENTER");
  Serial0.flush();

  if (WiFi.status() != WL_CONNECTED) {
    Serial0.println("[backend] wifi not connected");
    return false;
  }

  if (!ensurePaired()) {
    Serial0.println("[backend] Not paired; cannot fetch config");
    Serial0.println("[backend] ERROR: Cannot fetch schedules without device secret!");
    Serial0.println("[backend] SOLUTION: Reset device pairing on backend or restore secret to NVS");
    return false;
  }

  // Double-check we have a secret before making authenticated request
  if (device_secret_.length() == 0) {
    Serial0.println("[backend] ERROR: No device secret available for authentication");
    return false;
  }

  HTTPClient http;
  http.setTimeout(config::HTTP_TIMEOUT_MS);

  String url = configUrl();
  Serial0.printf("[backend] GET %s\n", url.c_str());

  if (!beginHttp(http, url)) {
    Serial0.println("[backend] http.begin FAILED");
    return false;
  }

  http.addHeader("X-Device-Secret", deviceSecret());

  int code = http.GET();
  String payload = http.getString();
  http.end();

  printHttpResult(code, payload);

  if (code != HTTP_CODE_OK) return false;

  bool ok = parseConfigPayload(payload, store);
  Serial0.printf("[backend] parse ok=%s\n", ok ? "true" : "false");
  return ok;
}

bool BackendClient::postEvent(const String& status, const String& occurred_at_iso8601,
                              int container_slot, int schedule_id) {
  if (!ensurePaired()) {
    Serial0.println("[backend] Not paired; cannot post event");
    return false;
  }

  HTTPClient http;
  http.setTimeout(config::HTTP_TIMEOUT_MS);

  String url = eventsUrl();
  Serial0.printf("[backend] POST event %s\n", url.c_str());

  if (!beginHttp(http, url)) {
    Serial0.println("[backend] Failed to begin HTTP connection for events");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Secret", deviceSecret());

  StaticJsonDocument<256> doc;
  doc["status"] = status;
  doc["occurred_at"] = occurred_at_iso8601;
  if (container_slot >= 0) doc["container_slot"] = container_slot;
  if (schedule_id >= 0) doc["schedule_id"] = schedule_id;

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  String payload = http.getString();
  http.end();

  printHttpResult(code, payload);

  return (code == HTTP_CODE_NO_CONTENT);
}

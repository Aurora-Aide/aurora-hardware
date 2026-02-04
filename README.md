Aurora Hardware (ESP32-S3)
==========================

This sketch targets the ESP32-S3 (e.g., ESP32-S3-WROOM-1) and talks to the backend device APIs:

- `GET /dispensers/devices/<serial_id>/config/` with header `X-Device-Secret`
- `POST /dispensers/devices/<serial_id>/events/` with header `X-Device-Secret`

Behavior
--------
- Connect to Wi-Fi, then to the backend.
- On boot, fetch config (containers, schedules, schedule_version) for the configured `DEVICE_SERIAL_ID`.
- Poll for new schedules every 5 seconds; if a new version is returned, replace the in-memory schedule list.
- Store schedules in RAM; prints to Serial for visibility.

Files
-----
- `src/config.h` – edit your Wi-Fi credentials, backend URL, serial ID, and device secret.
- `src/schedule_store.*` – simple in-RAM schedule storage.
- `src/backend_client.*` – HTTPS/HTTP client for config/events endpoints.
- `src/main.cpp` – wiring, Wi-Fi handling, and 5s polling loop.

Notes
-----
- Uses Arduino core for ESP32 (WiFi, HTTPClient, ArduinoJson). Ensure ArduinoJson is installed.
- If using HTTPS, provide a root CA in `config.h` or disable cert validation for local testing only.


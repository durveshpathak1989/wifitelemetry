# Test Quad WiFiTelemetry Library

## Explain It Simply

This module makes the drone share information over Wi-Fi. A browser or ground station can ask for sensor values, tuning values, logs, and update pages.

This library provides the ESP32 SoftAP telemetry, tuning, timing, spectrum, flight-log, and OTA HTTP endpoints.

## Pin Map

WiFiTelemetry does not use external GPIO pins. It uses the ESP32 WiFi radio and receives data through callback providers registered by the main sketch.

## Main INO Integration Example

```cpp
#include "TelemetryWiFi.h"

TelemetryPacket provideTelemetry();
String provideTimingJson();

void setup() {
    telemetryWiFi.setTelemetryProvider(provideTelemetry);
    telemetryWiFi.setTimingProvider(provideTimingJson);
    telemetryWiFi.begin("ESP32-DRONE", "12345678");
}

void loop() {
    telemetryWiFi.update();
}
```


## Common Endpoints

| Endpoint | Use |
| --- | --- |
| `GET /telemetry` | Current flight state as JSON |
| `POST /tune` | Apply PID, notch, and AHRS tuning while disarmed |
| `GET /timing` | IMU/control-loop jitter stats |
| `GET /spectrum` | Vibration spectrum for notch tuning |
| `GET /flightlog.csv` | Buffered flight log |

## Why These Data Types

Callbacks keep telemetry ownership in the main sketch, so the WiFi task does not directly touch control-loop internals. JSON strings are used at the HTTP boundary because the browser/GCS side consumes text, while the flight loop keeps numeric values as native `float`, `uint32_t`, and `bool`.

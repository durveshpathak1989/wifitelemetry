/*
 * Name: TelemetryWiFi.cpp
 * Use: Implementation of WiFi telemetry, tuning, OTA, log, and timing HTTP endpoints.
 * Version: 4.0.0
 * Created by: Durvesh Pathak dp676@cornell.edu
 */

/**
 * ============================================================
 *  TelemetryWiFi.cpp — v2.4  (timing endpoints added)
 * ============================================================
 */

#include "TelemetryWiFi.h"
#include "../DebugConfig/DebugConfig.h"

TelemetryWiFi telemetryWiFi(80);

static void appendJsonFloat(String& out, const __FlashStringHelper* key, double value, uint8_t decimals)
{
    char buf[28];
    dtostrf(value, 0, decimals, buf);
    out += key;
    out += buf;
}

static void appendJsonUInt(String& out, const __FlashStringHelper* key, uint32_t value)
{
    char buf[12];
    ultoa(value, buf, 10);
    out += key;
    out += buf;
}

static void appendJsonInt(String& out, const __FlashStringHelper* key, int32_t value)
{
    char buf[12];
    ltoa(value, buf, 10);
    out += key;
    out += buf;
}

static void appendJsonBool(String& out, const __FlashStringHelper* key, bool value)
{
    out += key;
    out += value ? F("true") : F("false");
}

TelemetryWiFi::TelemetryWiFi(uint16_t port)
    : _server(port),
      _provider(nullptr),
      _tuneHandler(nullptr),
      _otaAllowedProvider(nullptr),
      _otaInProgress(false),
      _otaRejected(false),
      _otaError(),
      _timingProvider(nullptr),
      _timingCsvProvider(nullptr),
      _timingResetHandler(nullptr),
      _spectrumProvider(nullptr),
      _flightLogCount(nullptr),
      _flightLogHeader(nullptr),
      _flightLogRow(nullptr),
      _flightLogReset(nullptr),
      _requestCount(0)
{
    memset(_logBuf, 0, sizeof(_logBuf));
}

bool TelemetryWiFi::begin(const char* ssid, const char* password,
                          IPAddress localIp, IPAddress gateway, IPAddress subnet)
{
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    if (!WiFi.softAPConfig(localIp, gateway, subnet)) {
        DBG_PRINTLN(F("[WiFi] softAPConfig failed")); return false;
    }
    if (!WiFi.softAP(ssid, password)) {
        DBG_PRINTLN(F("[WiFi] softAP failed")); return false;
    }
    _setupRoutes();
    _server.begin();
    DBG_PRINTF("[WiFi] SSID=%s  PASS=%s\n", ssid, password);
    DBG_PRINTF("[WiFi] http://%s/\n", WiFi.softAPIP().toString().c_str());
    return true;
}

void TelemetryWiFi::setTelemetryProvider(bool (*p)(TelemetryPacket& out)) { _provider             = p; }
void TelemetryWiFi::setTuneHandler(bool (*h)(const TunePacket& in))       { _tuneHandler          = h; }
void TelemetryWiFi::setOtaAllowedProvider(bool (*p)())                      { _otaAllowedProvider   = p; }
void TelemetryWiFi::setTimingProvider(String (*p)())                       { _timingProvider       = p; }
void TelemetryWiFi::setTimingCsvProvider(String (*p)())                    { _timingCsvProvider    = p; }
void TelemetryWiFi::setTimingResetHandler(void (*h)())                     { _timingResetHandler   = h; }
void TelemetryWiFi::setSpectrumProvider(String (*p)())                         { _spectrumProvider     = p; }
void TelemetryWiFi::update()                                               { _server.handleClient(); }

void TelemetryWiFi::setFlightLogCountProvider(uint16_t (*p)())     { _flightLogCount  = p; }
void TelemetryWiFi::setFlightLogHeaderProvider(String (*p)())      { _flightLogHeader = p; }
void TelemetryWiFi::setFlightLogRowProvider(String (*p)(uint16_t)) { _flightLogRow    = p; }
void TelemetryWiFi::setFlightLogResetHandler(void (*h)())          { _flightLogReset  = h; }

IPAddress TelemetryWiFi::ip()           const { return WiFi.softAPIP(); }
uint32_t  TelemetryWiFi::requestCount() const { return _requestCount; }

void TelemetryWiFi::pushLog(const char* line)
{
    DBG_PRINTLN(line);
    portENTER_CRITICAL(&_logMux);
    uint8_t slot;
    if (_logCount < WIFI_LOG_CAPACITY) {
        slot = (_logHead + _logCount) % WIFI_LOG_CAPACITY;
        _logCount++;
    } else {
        slot = _logHead;
        _logHead = (_logHead + 1) % WIFI_LOG_CAPACITY;
    }
    _logBuf[slot].seq = ++_logWriteSeq;
    strncpy(_logBuf[slot].text, line, WIFI_LOG_LINE_LEN - 1);
    _logBuf[slot].text[WIFI_LOG_LINE_LEN - 1] = '\0';
    portEXIT_CRITICAL(&_logMux);
}

void TelemetryWiFi::_setupRoutes()
{
    _server.on("/",               HTTP_GET,     [this]() { _handleRoot(); });
    _server.on("/telemetry",      HTTP_GET,     [this]() { _handleTelemetry(); });
    _server.on("/telemetry",      HTTP_OPTIONS, [this]() { _handleOptions(); });
    _server.on("/tune",           HTTP_POST,    [this]() { _handleTune(); });
    _server.on("/tune",           HTTP_OPTIONS, [this]() { _handleOptions(); });
    _server.on("/update",         HTTP_GET,     [this]() { _handleOtaPage(); });
    _server.on("/update",         HTTP_POST,    [this]() { _handleOtaFinish(); }, [this]() { _handleOtaUpload(); });
    _server.on("/update",         HTTP_OPTIONS, [this]() { _handleOptions(); });
    _server.on("/log",            HTTP_GET,     [this]() { _handleLog(); });
    _server.on("/log",            HTTP_OPTIONS, [this]() { _handleOptions(); });
    // v2.4 timing endpoints
    _server.on("/timing",         HTTP_GET,     [this]() { _handleTiming(); });
    _server.on("/timing",         HTTP_OPTIONS, [this]() { _handleOptions(); });
    _server.on("/timing/reset",   HTTP_POST,    [this]() { _handleTimingReset(); });
    _server.on("/timing/reset",   HTTP_OPTIONS, [this]() { _handleOptions(); });
    _server.on("/timing/csv",     HTTP_GET,     [this]() { _handleTimingCsv(); });
    _server.on("/timing/csv",     HTTP_OPTIONS, [this]() { _handleOptions(); });
    _server.on("/spectrum",       HTTP_GET,     [this]() { _handleSpectrum(); });
    _server.on("/spectrum",       HTTP_OPTIONS, [this]() { _handleOptions(); });
    _server.on("/flightlog/csv",   HTTP_GET,     [this]() { _handleFlightLogCsv(); });
    _server.on("/flightlog/csv",   HTTP_OPTIONS, [this]() { _handleOptions(); });
    _server.on("/flightlog/reset", HTTP_POST,    [this]() { _handleFlightLogReset(); });
    _server.on("/flightlog/reset", HTTP_OPTIONS, [this]() { _handleOptions(); });
    _server.onNotFound(                         [this]() { _handleNotFound(); });
}

void TelemetryWiFi::_sendCorsHeaders()
{
    _server.sendHeader("Access-Control-Allow-Origin",  "*");
    _server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    _server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    _server.sendHeader("Cache-Control", "no-store");
}

void TelemetryWiFi::_handleRoot()
{
    _sendCorsHeaders();
    _server.send(200, "text/plain",
        "ESP32 Drone Telemetry v2.4\n"
        "GET  /telemetry        — full state JSON\n"
        "POST /tune             — apply runtime tuning levers (disarmed only)\n"
        "GET/POST /update       — Web OTA firmware update (safe/disarmed only)\n"
        "GET  /log?since=N      — calibration log lines\n"
        "GET  /timing           — IMU jitter stats (Welford)\n"
        "POST /timing/reset     — reset jitter stats\n"
        "GET  /timing/csv       — download raw period_us ring buffer\n"
        "GET  /spectrum         — onboard vibration spectrum JSON\n");
}

void TelemetryWiFi::_handleTelemetry()
{
    _requestCount++;
    _sendCorsHeaders();
    if (!_provider) {
        _server.send(503, "application/json", "{\"ok\":false,\"error\":\"no provider\"}");
        return;
    }
    TelemetryPacket p; memset(&p, 0, sizeof(p));
    if (!_provider(p)) {
        _server.send(503, "application/json", "{\"ok\":false,\"error\":\"unavailable\"}");
        return;
    }
    _server.send(200, "application/json", _jsonFromPacket(p));
}

void TelemetryWiFi::_handleTiming()
{
    _sendCorsHeaders();
    if (!_timingProvider) {
        _server.send(503, "application/json", "{\"ok\":false,\"error\":\"no timing provider\"}");
        return;
    }
    _server.send(200, "application/json", _timingProvider());
}

void TelemetryWiFi::_handleTimingReset()
{
    _sendCorsHeaders();
    if (_timingResetHandler) _timingResetHandler();
    _server.send(200, "application/json", "{\"ok\":true,\"msg\":\"timing stats reset\"}");
}

void TelemetryWiFi::_handleTimingCsv()
{
    _sendCorsHeaders();
    if (!_timingCsvProvider) {
        _server.send(503, "text/plain", "no csv provider");
        return;
    }
    _server.sendHeader("Content-Disposition", "attachment; filename=\"timing.csv\"");
    _server.send(200, "text/csv", _timingCsvProvider());
}


void TelemetryWiFi::_handleSpectrum()
{
    _requestCount++;
    _sendCorsHeaders();
    if (!_spectrumProvider) {
        _server.send(503, "application/json", "{\"ok\":false,\"error\":\"no spectrum provider\"}");
        return;
    }
    _server.send(200, "application/json", _spectrumProvider());
}

void TelemetryWiFi::_handleFlightLogReset()
{
    _sendCorsHeaders();
    if (_flightLogReset) _flightLogReset();
    _server.send(200, "application/json", "{\"ok\":true,\"msg\":\"flight log reset\"}");
}

void TelemetryWiFi::_handleFlightLogCsv()
{
    _sendCorsHeaders();
    if (!_flightLogCount || !_flightLogHeader || !_flightLogRow) {
        _server.send(503, "text/plain", "no flightlog provider");
        return;
    }
    // Chunked streaming — never builds the whole CSV in RAM.
    _server.sendHeader("Content-Disposition", "attachment; filename=\"flightlog.csv\"");
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "text/csv", "");
    _server.sendContent(_flightLogHeader());     // also freezes the buffer (count() does)
    uint16_t n = _flightLogCount();
    for (uint16_t i = 0; i < n; i++) {
        _server.sendContent(_flightLogRow(i));
    }
    _server.sendContent("");                     // terminate chunked transfer
}

void TelemetryWiFi::_handleLog()
{
    _sendCorsHeaders();
    uint32_t since = 0;
    if (_server.hasArg("since")) since = (uint32_t)_server.arg("since").toInt();

    portENTER_CRITICAL(&_logMux);
    const uint32_t writeSeq = _logWriteSeq;
    const uint8_t  count    = _logCount;
    const uint8_t  head     = _logHead;
    struct Snap { uint32_t seq; char text[WIFI_LOG_LINE_LEN]; };
    Snap snap[WIFI_LOG_CAPACITY];
    uint8_t snapCount = 0;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t idx = (head + i) % WIFI_LOG_CAPACITY;
        if (_logBuf[idx].seq > since) {
            snap[snapCount].seq = _logBuf[idx].seq;
            strncpy(snap[snapCount].text, _logBuf[idx].text, WIFI_LOG_LINE_LEN - 1);
            snap[snapCount].text[WIFI_LOG_LINE_LEN - 1] = '\0';
            snapCount++;
        }
    }
    portEXIT_CRITICAL(&_logMux);

    String json;
    json.reserve(snapCount * (WIFI_LOG_LINE_LEN + 20) + 60);
    json += "{\"ok\":true,\"nextSeq\":"; json += String(writeSeq); json += ",\"lines\":[";
    for (uint8_t i = 0; i < snapCount; i++) {
        if (i > 0) json += ',';
        json += "{\"seq\":"; json += String(snap[i].seq); json += ",\"text\":\"";
        for (const char* p = snap[i].text; *p; p++) {
            if      (*p == '"')  json += "\\\"";
            else if (*p == '\\') json += "\\\\";
            else if (*p == '\n') json += "\\n";
            else if (*p == '\r') {}
            else                 json += *p;
        }
        json += "\"}";
    }
    json += "]}";
    _server.send(200, "application/json", json);
}


bool TelemetryWiFi::_isOtaAllowed() const
{
    return _otaAllowedProvider ? _otaAllowedProvider() : false;
}

void TelemetryWiFi::_handleOtaPage()
{
    _sendCorsHeaders();
    const bool allowed = _isOtaAllowed();
    String html;
    html.reserve(1700);
    html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>ESP32 Drone OTA</title><style>body{font-family:system-ui;background:#0b0b0f;color:#eee;margin:24px}"
              ".card{max-width:620px;background:#1c1c1e;border:1px solid #333;border-radius:14px;padding:20px}"
              "input,button{font-size:15px;margin-top:10px}button{padding:10px 16px;border-radius:8px;border:0;background:#0a84ff;color:white}"
              ".bad{color:#ff453a}.ok{color:#30d158}.warn{color:#ff9f0a}code{color:#64d2ff}</style></head><body><div class='card'>");
    html += F("<h2>ESP32 Drone OTA Update</h2>");
    html += allowed ? F("<p class='ok'>OTA unlocked: drone is DISARMED and motors are inactive.</p>")
                    : F("<p class='bad'>OTA locked. Disarm the drone, lower throttle, and stop motors before uploading firmware.</p>");
    html += F("<p class='warn'>Use only the compiled <code>.bin</code> for this same board/partition scheme. Keep USB available as recovery.</p>");
    if (allowed) {
        html += F("<form method='POST' action='/update' enctype='multipart/form-data'>"
                  "<input type='file' name='firmware' accept='.bin,.bin.gz' required><br>"
                  "<button type='submit'>Upload and Reboot</button></form>");
    }
    html += F("<p><a style='color:#64d2ff' href='/telemetry'>telemetry</a></p></div></body></html>");
    _server.send(allowed ? 200 : 423, "text/html", html);
}

void TelemetryWiFi::_handleOtaUpload()
{
    HTTPUpload& upload = _server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        _otaInProgress = true;
        _otaRejected = false;
        _otaError = "";

        if (!_isOtaAllowed()) {
            _otaRejected = true;
            _otaError = "OTA rejected: disarm, lower throttle, and stop motors first";
            DBG_PRINTLN("[OTA] Rejected — unsafe state");
            return;
        }

        DBG_PRINTF("[OTA] Start: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            _otaRejected = true;
            _otaError = "Update.begin failed";
            Update.printError(Serial);
            return;
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (_otaRejected) return;
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            _otaRejected = true;
            _otaError = "flash write failed";
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_END) {
        if (_otaRejected) return;
        if (Update.end(true)) {
            DBG_PRINTF("[OTA] Success: %u bytes\n", upload.totalSize);
        } else {
            _otaRejected = true;
            _otaError = "Update.end failed";
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED) {
        _otaRejected = true;
        _otaError = "upload aborted";
        Update.abort();
        DBG_PRINTLN("[OTA] Aborted");
    }
}

void TelemetryWiFi::_handleOtaFinish()
{
    _sendCorsHeaders();
    _otaInProgress = false;

    if (_otaRejected || Update.hasError()) {
        String msg = "{\"ok\":false,\"error\":\"";
        msg += (_otaError.length() ? _otaError : String("OTA update failed"));
        msg += "\"}";
        _server.send(_otaError.startsWith("OTA rejected") ? 423 : 500, "application/json", msg);
        return;
    }

    _server.send(200, "text/html", "<!doctype html><html><body style='font-family:system-ui;background:#111;color:#eee'><h2>OTA complete</h2><p>Rebooting ESP32...</p></body></html>");
    delay(250);
    ESP.restart();
}

void TelemetryWiFi::_handleTune()
{
    _sendCorsHeaders();
    if (!_tuneHandler) {
        _server.send(503, "application/json", "{\"ok\":false,\"error\":\"no handler\"}");
        return;
    }
    String body = _server.arg("plain");
    if (body.length() == 0) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
        return;
    }
    TunePacket t; memset(&t, 0, sizeof(t));
    auto tryF = [&](const char* key, bool& has, float& val) {
        float tmp; if (_jsonGetFloat(body, key, tmp)) { has = true; val = tmp; }
    };
    auto tryB = [&](const char* key, bool& has, bool& val) {
        bool tmp; if (_jsonGetBool(body, key, tmp)) { has = true; val = tmp; }
    };
    // Pilot command limits
    tryF("max_angle_deg",           t.has_max_angle_deg,           t.max_angle_deg);
    tryF("max_rate_dps",            t.has_max_rate_dps,            t.max_rate_dps);
    tryF("max_pitch_rate_dps",      t.has_max_pitch_rate_dps,      t.max_pitch_rate_dps);

    // PID output authority limits before motor mixing
    tryF("roll_output_limit",       t.has_roll_output_limit,       t.roll_output_limit);
    tryF("pitch_output_limit",      t.has_pitch_output_limit,      t.pitch_output_limit);
    tryF("yaw_output_limit",        t.has_yaw_output_limit,        t.yaw_output_limit);

    // Throttle shaping + motor output limits
    tryF("throttle_expo",           t.has_throttle_expo,           t.throttle_expo);
    tryF("throttle_up_rate_per_sec",   t.has_throttle_up_rate_per_sec,   t.throttle_up_rate_per_sec);
    tryF("throttle_down_rate_per_sec", t.has_throttle_down_rate_per_sec, t.throttle_down_rate_per_sec);
    tryF("motor_idle",              t.has_motor_idle,              t.motor_idle);
    tryF("motor_max",               t.has_motor_max,               t.motor_max);
    tryF("throttle_cut",            t.has_throttle_cut,            t.throttle_cut);
    tryF("idle_ramp_end",           t.has_idle_ramp_end,           t.idle_ramp_end);
    tryF("pid_ilimit",              t.has_pid_ilimit,              t.pid_ilimit);

    // Inner rate loop PID
    tryF("pid_roll_kp",             t.has_pid_roll_kp,             t.pid_roll_kp);
    tryF("pid_roll_ki",             t.has_pid_roll_ki,             t.pid_roll_ki);
    tryF("pid_roll_kd",             t.has_pid_roll_kd,             t.pid_roll_kd);
    tryF("pid_pitch_kp",            t.has_pid_pitch_kp,            t.pid_pitch_kp);
    tryF("pid_pitch_ki",            t.has_pid_pitch_ki,            t.pid_pitch_ki);
    tryF("pid_pitch_kd",            t.has_pid_pitch_kd,            t.pid_pitch_kd);
    tryF("pid_yaw_kp",              t.has_pid_yaw_kp,              t.pid_yaw_kp);
    tryF("pid_yaw_ki",              t.has_pid_yaw_ki,              t.pid_yaw_ki);
    tryF("pid_yaw_kd",              t.has_pid_yaw_kd,              t.pid_yaw_kd);

    // Outer angle loop PID
    tryF("pid_angle_roll_kp",       t.has_pid_angle_roll_kp,       t.pid_angle_roll_kp);
    tryF("pid_angle_roll_ki",       t.has_pid_angle_roll_ki,       t.pid_angle_roll_ki);
    tryF("pid_angle_roll_kd",       t.has_pid_angle_roll_kd,       t.pid_angle_roll_kd);
    tryF("pid_angle_pitch_kp",      t.has_pid_angle_pitch_kp,      t.pid_angle_pitch_kp);
    tryF("pid_angle_pitch_ki",      t.has_pid_angle_pitch_ki,      t.pid_angle_pitch_ki);
    tryF("pid_angle_pitch_kd",      t.has_pid_angle_pitch_kd,      t.pid_angle_pitch_kd);

    // Outer yaw heading-hold loop
    tryF("pid_angle_yaw_kp",        t.has_pid_angle_yaw_kp,        t.pid_angle_yaw_kp);
    tryF("yaw_deadband",            t.has_yaw_deadband,            t.yaw_deadband);
    tryF("yaw_max_rate_dps",        t.has_yaw_max_rate_dps,        t.yaw_max_rate_dps);
    // Compatibility alias used by some GCS config panels
    if (!t.has_yaw_max_rate_dps) { tryF("max_yaw_rate_dps", t.has_yaw_max_rate_dps, t.yaw_max_rate_dps); }

    // AHRS
    tryF("mahony_kp",               t.has_mahony_kp,               t.mahony_kp);
    tryF("mahony_ki",               t.has_mahony_ki,               t.mahony_ki);

    // Motor vibration notch filter. Aliases support camelCase GCS controls.
    tryB("notch_enable",            t.has_notch_enable,            t.notch_enable);
    if (!t.has_notch_enable) { tryB("notchEnable", t.has_notch_enable, t.notch_enable); }
    tryF("notch_freq_hz",           t.has_notch_freq_hz,           t.notch_freq_hz);
    if (!t.has_notch_freq_hz) { tryF("notchFreqHz", t.has_notch_freq_hz, t.notch_freq_hz); }
    tryF("notch_q",                 t.has_notch_q,                 t.notch_q);
    if (!t.has_notch_q) { tryF("notchQ", t.has_notch_q, t.notch_q); }

    // Attitude EKF tuning. Aliases support camelCase GCS controls.
    tryF("ekf_angle_q",              t.has_ekf_angle_q,              t.ekf_angle_q);
    if (!t.has_ekf_angle_q) { tryF("ekfAngleQ", t.has_ekf_angle_q, t.ekf_angle_q); }
    tryF("ekf_bias_q",               t.has_ekf_bias_q,               t.ekf_bias_q);
    if (!t.has_ekf_bias_q) { tryF("ekfBiasQ", t.has_ekf_bias_q, t.ekf_bias_q); }
    tryF("ekf_accel_r",              t.has_ekf_accel_r,              t.ekf_accel_r);
    if (!t.has_ekf_accel_r) { tryF("ekfAccelR", t.has_ekf_accel_r, t.ekf_accel_r); }
    tryF("ekf_mag_r",                t.has_ekf_mag_r,                t.ekf_mag_r);
    if (!t.has_ekf_mag_r) { tryF("ekfMagR", t.has_ekf_mag_r, t.ekf_mag_r); }
    tryF("ekf_mag_declination_deg",  t.has_ekf_mag_declination_deg,  t.ekf_mag_declination_deg);
    if (!t.has_ekf_mag_declination_deg) { tryF("ekfMagDeclinationDeg", t.has_ekf_mag_declination_deg, t.ekf_mag_declination_deg); }
    tryF("ekf_mag_yaw_offset_deg",   t.has_ekf_mag_yaw_offset_deg,   t.ekf_mag_yaw_offset_deg);
    if (!t.has_ekf_mag_yaw_offset_deg) { tryF("ekfMagYawOffsetDeg", t.has_ekf_mag_yaw_offset_deg, t.ekf_mag_yaw_offset_deg); }
    tryF("ekf_mag_yaw_sign",         t.has_ekf_mag_yaw_sign,         t.ekf_mag_yaw_sign);
    if (!t.has_ekf_mag_yaw_sign) { tryF("ekfMagYawSign", t.has_ekf_mag_yaw_sign, t.ekf_mag_yaw_sign); }

    bool accepted = _tuneHandler(t);
    if (accepted) {
        _server.send(200, "application/json", "{\"ok\":true,\"accepted\":true}");
    } else {
        _server.send(423, "application/json", "{\"ok\":false,\"accepted\":false,\"error\":\"tune rejected by firmware; check armed state and serial log\"}");
    }
}

void TelemetryWiFi::_handleOptions() { _sendCorsHeaders(); _server.send(204); }
void TelemetryWiFi::_handleNotFound() {
    _sendCorsHeaders();
    _server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
}

// ─────────────────────────────────────────────────────────────
//  JSON serialiser
// ─────────────────────────────────────────────────────────────
String TelemetryWiFi::_jsonFromPacket(const TelemetryPacket& p) const
{
#define JF(key, value, decimals) appendJsonFloat(j, F(key), (value), (decimals))
#define JU(key, value) appendJsonUInt(j, F(key), (uint32_t)(value))
#define JI(key, value) appendJsonInt(j, F(key), (int32_t)(value))
#define JB(key, value) appendJsonBool(j, F(key), (value))
    String j;
    j.reserve(6500);
    j += F("{\"ok\":true");
    JU(",\"tick\":", p.tick);
    j += F(",\"mode\":\"");
    j += p.mode ? p.mode : "UNKNOWN";
    j += '"';
    JB(",\"armed\":", p.armed);
    JB(",\"rcValid\":", p.rc_valid);
    JB(",\"imuValid\":", p.imu_valid);
    JB(",\"magValid\":", p.mag_valid);
    JU(",\"ahrsFilterMode\":", p.ahrs_filter_mode);
    j += F(",\"ahrsFilter\":\"");
    j += p.ahrs_filter ? p.ahrs_filter : "EKF";
    j += '"';
    // Raw estimator values. These remain untouched by software level-zero trim.
    JF(",\"roll\":", p.roll_deg, 2);
    JF(",\"pitch\":", p.pitch_deg, 2);
    JF(",\"yaw\":", p.yaw_deg, 2);
    JF(",\"q0\":", p.q0, 6);
    JF(",\"q1\":", p.q1, 6);
    JF(",\"q2\":", p.q2, 6);
    JF(",\"q3\":", p.q3, 6);

    // PID/control attitude after level-zero offset.
    JF(",\"rollCtrl\":", p.roll_ctrl_deg, 2);
    JF(",\"pitchCtrl\":", p.pitch_ctrl_deg, 2);
    JF(",\"yawCtrl\":", p.yaw_ctrl_deg, 2);

    // Captured post-AHRS offsets. Flight page can display AHRS vs control attitude.
    JF(",\"rollOffset\":", p.roll_offset_deg, 2);
    JF(",\"pitchOffset\":", p.pitch_offset_deg, 2);
    JF(",\"yawOffset\":", p.yaw_offset_deg, 2);
    JF(",\"ax\":", p.ax_g, 4);
    JF(",\"ay\":", p.ay_g, 4);
    JF(",\"az\":", p.az_g, 4);
    JF(",\"gx\":", p.gx_dps, 3);
    JF(",\"gy\":", p.gy_dps, 3);
    JF(",\"gz\":", p.gz_dps, 3);
    JF(",\"mx\":", p.mx_uT, 3);
    JF(",\"my\":", p.my_uT, 3);
    JF(",\"mz\":", p.mz_uT, 3);
    JF(",\"imuTempC\":", p.imu_temp_c, 2);
    JF(",\"thr\":", p.throttle, 3);
    JF(",\"rcRoll\":", p.rc_roll, 3);
    JF(",\"rcPitch\":", p.rc_pitch, 3);
    JF(",\"rcYaw\":", p.rc_yaw, 3);
    JF(",\"rcHz\":", p.rc_hz, 1);
    JF(",\"motFL\":", p.motor_fl, 3);
    JF(",\"motFR\":", p.motor_fr, 3);
    JF(",\"motRL\":", p.motor_rl, 3);
    JF(",\"motRR\":", p.motor_rr, 3);
    JF(",\"motorFLPreSat\":", p.motor_fl_pre_sat, 4);
    JF(",\"motorFRPreSat\":", p.motor_fr_pre_sat, 4);
    JF(",\"motorRLPreSat\":", p.motor_rl_pre_sat, 4);
    JF(",\"motorRRPreSat\":", p.motor_rr_pre_sat, 4);
    JF(",\"batteryVoltageV\":", p.battery_voltage_v, 3);
    JU(",\"loopPeriodUs\":", p.loop_period_us);
    JI(",\"loopJitterUs\":", p.loop_jitter_us);
    JU(",\"imuReadUs\":", p.imu_read_us);
    JU(",\"rcReadUs\":", p.rc_read_us);
    JU(",\"ahrsUpdateUs\":", p.ahrs_update_us);
    JU(",\"controlUpdateUs\":", p.control_update_us);
    JU(",\"motorWriteUs\":", p.motor_write_us);
    JU(",\"wifiServiceUs\":", p.wifi_service_us);
    JU(",\"onboardLogWriteUs\":", p.onboard_log_write_us);
    JU(",\"missedLoopCount\":", p.missed_loop_count);
    JF(",\"rpmFL\":", p.rpm_fl, 0);
    JF(",\"rpmFR\":", p.rpm_fr, 0);
    JF(",\"rpmRL\":", p.rpm_rl, 0);
    JF(",\"rpmRR\":", p.rpm_rr, 0);
    JF(",\"cmdRpmFL\":", p.cmd_rpm_fl, 0);
    JF(",\"cmdRpmFR\":", p.cmd_rpm_fr, 0);
    JF(",\"cmdRpmRL\":", p.cmd_rpm_rl, 0);
    JF(",\"cmdRpmRR\":", p.cmd_rpm_rr, 0);
    JF(",\"actualRpmFL\":", p.actual_rpm_fl, 0);
    JF(",\"actualRpmFR\":", p.actual_rpm_fr, 0);
    JF(",\"actualRpmRL\":", p.actual_rpm_rl, 0);
    JF(",\"actualRpmRR\":", p.actual_rpm_rr, 0);
    JB(",\"rpmActualValid\":", p.rpm_actual_valid);
    JF(",\"bmpTempC\":", p.bmp_temp_c, 2);
    JF(",\"bmpPressureHpa\":", p.bmp_pressure_hpa, 2);
    JF(",\"bmpAltitudeM\":", p.bmp_altitude_m, 2);
    JB(",\"bmpValid\":", p.bmp_valid);
    JF(",\"cpuCore0\":", p.cpu_core0_pct, 1);
    JF(",\"cpuCore1\":", p.cpu_core1_pct, 1);
    JB(",\"cpuValid\":", p.cpu_valid);
    JU(",\"rcCh1Us\":", p.rc_ch1_us);
    JU(",\"rcCh2Us\":", p.rc_ch2_us);
    JU(",\"rcCh3Us\":", p.rc_ch3_us);
    JU(",\"rcCh4Us\":", p.rc_ch4_us);
    JU(",\"rcCh5Us\":", p.rc_ch5_us);
    JU(",\"rcCh6Us\":", p.rc_ch6_us);
    JU(",\"rcCh7Us\":", p.rc_ch7_us);
    JU(",\"rcCh8Us\":", p.rc_ch8_us);
    JU(",\"rcCh9Us\":", p.rc_ch9_us);
    JU(",\"rcCh10Us\":", p.rc_ch10_us);
    JU(",\"rcFailsafeCount\":", p.rc_failsafe_count);
    JB(",\"angleModeActive\":", p.angle_mode_active);
    JB(",\"acroModeActive\":", p.acro_mode_active);
    JU(",\"modeSwitchRawUs\":", p.mode_switch_raw_us);
    JU(",\"armSwitchRawUs\":", p.arm_switch_raw_us);
    JU(",\"auxTune1RawUs\":", p.aux_tune1_raw_us);
    JU(",\"auxTune2RawUs\":", p.aux_tune2_raw_us);
    JB(",\"angleLoopEnabled\":", p.angle_loop_enabled);
    JB(",\"rateLoopEnabled\":", p.rate_loop_enabled);
    JF(",\"targetYawDeg\":", p.target_yaw_deg, 3);
    JF(",\"actualRollRateDps\":", p.actual_roll_rate_dps, 3);
    JF(",\"actualPitchRateDps\":", p.actual_pitch_rate_dps, 3);
    JF(",\"actualYawRateDps\":", p.actual_yaw_rate_dps, 3);
    JF(",\"angleRollP\":", p.angle_roll_p, 8);
    JF(",\"angleRollI\":", p.angle_roll_i, 8);
    JF(",\"angleRollD\":", p.angle_roll_d, 8);
    JF(",\"anglePitchP\":", p.angle_pitch_p, 8);
    JF(",\"anglePitchI\":", p.angle_pitch_i, 8);
    JF(",\"anglePitchD\":", p.angle_pitch_d, 8);
    JF(",\"rollP\":", p.rate_roll_p, 8);
    JF(",\"rollI\":", p.rate_roll_i, 8);
    JF(",\"rollD\":", p.rate_roll_d, 8);
    JF(",\"pitchP\":", p.rate_pitch_p, 8);
    JF(",\"pitchI\":", p.rate_pitch_i, 8);
    JF(",\"pitchD\":", p.rate_pitch_d, 8);
    JF(",\"yawP\":", p.rate_yaw_p, 8);
    JF(",\"yawI\":", p.rate_yaw_i, 8);
    JF(",\"yawD\":", p.rate_yaw_d, 8);
    JF(",\"angleRollIterm\":", p.angle_roll_iterm, 8);
    JF(",\"anglePitchIterm\":", p.angle_pitch_iterm, 8);
    JU(",\"pidResetCount\":", p.pid_reset_count);
    JU(",\"modeTransitionCount\":", p.mode_transition_count);
    JU(",\"lastModeChangeMs\":", p.last_mode_change_ms);
    JU(",\"armingTransitionCount\":", p.arming_transition_count);
    JU(",\"lastArmChangeMs\":", p.last_arm_change_ms);
    JB(",\"throttleLow\":", p.throttle_low);
    JF(",\"controlAuthorityRemaining\":", p.control_authority_remaining, 6);
    JB(",\"rollOutputLimited\":", p.roll_output_limited);
    JB(",\"pitchOutputLimited\":", p.pitch_output_limited);
    JB(",\"yawOutputLimited\":", p.yaw_output_limited);
    JB(",\"rateOutputLimited\":", p.rate_output_limited);
    JF(",\"magNormUt\":", p.mag_norm_uT, 3);
    JB(",\"ekfMagUsed\":", p.ekf_mag_used);
    JF(",\"accelRollDeg\":", p.accel_roll_deg, 3);
    JF(",\"accelPitchDeg\":", p.accel_pitch_deg, 3);
    JF(",\"gyroRollDeg\":", p.gyro_roll_deg, 3);
    JF(",\"gyroPitchDeg\":", p.gyro_pitch_deg, 3);
    JF(",\"gyroYawDeg\":", p.gyro_yaw_deg, 3);
    JF(",\"targetRollDeg\":", p.target_roll_deg, 3);
    JF(",\"targetPitchDeg\":", p.target_pitch_deg, 3);
    JF(",\"targetRollRateDps\":", p.target_roll_rate_dps, 3);
    JF(",\"targetPitchRateDps\":", p.target_pitch_rate_dps, 3);
    JF(",\"targetYawRateDps\":", p.target_yaw_rate_dps, 3);
    JF(",\"rollAngleErrorDeg\":", p.roll_angle_error_deg, 3);
    JF(",\"pitchAngleErrorDeg\":", p.pitch_angle_error_deg, 3);
    JF(",\"rollRateErrorDps\":", p.roll_rate_error_dps, 3);
    JF(",\"pitchRateErrorDps\":", p.pitch_rate_error_dps, 3);
    JF(",\"yawRateErrorDps\":", p.yaw_rate_error_dps, 3);
    JF(",\"yawErrorDeg\":", p.yaw_error_deg, 3);
    JF(",\"pidRollOut\":", p.pid_roll_out, 6);
    JF(",\"pidPitchOut\":", p.pid_pitch_out, 6);
    JF(",\"pidYawOut\":", p.pid_yaw_out, 6);
    JF(",\"ekfBgxDps\":", p.ekf_bgx_dps, 6);
    JF(",\"ekfBgyDps\":", p.ekf_bgy_dps, 6);
    JF(",\"ekfBgzDps\":", p.ekf_bgz_dps, 6);
    JB(",\"motorSaturated\":", p.motor_saturated);
    // Runtime tuning values serialized at 8 dp where useful so very small
    // gains like 0.00001 survive the telemetry round trip.
    JF(",\"maxAngleDeg\":", p.max_angle_deg, 3);
    JF(",\"maxRateDps\":", p.max_rate_dps, 3);
    JF(",\"maxPitchRateDps\":", p.max_pitch_rate_dps, 3);
    JF(",\"rollOutputLimit\":", p.roll_output_limit, 6);
    JF(",\"pitchOutputLimit\":", p.pitch_output_limit, 6);
    JF(",\"yawOutputLimit\":", p.yaw_output_limit, 6);
    JF(",\"throttleExpo\":", p.throttle_expo, 6);
    JF(",\"throttleUpRatePerSec\":", p.throttle_up_rate_per_sec, 6);
    JF(",\"throttleDownRatePerSec\":", p.throttle_down_rate_per_sec, 6);
    JF(",\"motorIdle\":", p.motor_idle, 6);
    JF(",\"motorMax\":", p.motor_max, 6);
    JF(",\"throttleCut\":", p.throttle_cut, 6);
    JF(",\"idleRampEnd\":", p.idle_ramp_end, 6);
    JF(",\"pidRollKp\":", p.pid_roll_kp, 8);
    JF(",\"pidRollKi\":", p.pid_roll_ki, 8);
    JF(",\"pidRollKd\":", p.pid_roll_kd, 8);
    JF(",\"pidPitchKp\":", p.pid_pitch_kp, 8);
    JF(",\"pidPitchKi\":", p.pid_pitch_ki, 8);
    JF(",\"pidPitchKd\":", p.pid_pitch_kd, 8);
    JF(",\"pidYawKp\":", p.pid_yaw_kp, 8);
    JF(",\"pidYawKi\":", p.pid_yaw_ki, 8);
    JF(",\"pidYawKd\":", p.pid_yaw_kd, 8);
    JF(",\"pidAngleRollKp\":", p.pid_angle_roll_kp, 8);
    JF(",\"pidAngleRollKi\":", p.pid_angle_roll_ki, 8);
    JF(",\"pidAngleRollKd\":", p.pid_angle_roll_kd, 8);
    JF(",\"pidAnglePitchKp\":", p.pid_angle_pitch_kp, 8);
    JF(",\"pidAnglePitchKi\":", p.pid_angle_pitch_ki, 8);
    JF(",\"pidAnglePitchKd\":", p.pid_angle_pitch_kd, 8);
    JF(",\"pidAngleYawKp\":", p.pid_angle_yaw_kp, 8);
    JF(",\"yawDeadband\":", p.yaw_deadband, 6);
    JF(",\"yawMaxRateDps\":", p.yaw_max_rate_dps, 3);
    // Compatibility alias for config panels that read maxYawRateDps.
    JF(",\"maxYawRateDps\":", p.yaw_max_rate_dps, 3);
    JF(",\"mahonyKp\":", p.mahony_kp, 8);
    JF(",\"mahonyKi\":", p.mahony_ki, 8);
    JF(",\"madgwickBeta\":", p.madgwick_beta, 8);
    JB(",\"notchEnable\":", p.notch_enable);
    JF(",\"notchFreqHz\":", p.notch_freq_hz, 3);
    JF(",\"notchQ\":", p.notch_q, 3);
    JF(",\"ekfAngleQ\":", p.ekf_angle_q, 9);
    JF(",\"ekfBiasQ\":", p.ekf_bias_q, 12);
    JF(",\"ekfAccelR\":", p.ekf_accel_r, 6);
    JF(",\"ekfMagR\":", p.ekf_mag_r, 6);
    JF(",\"ekfMagDeclinationDeg\":", p.ekf_mag_declination_deg, 3);
    JF(",\"ekfMagYawOffsetDeg\":", p.ekf_mag_yaw_offset_deg, 3);
    JF(",\"ekfMagYawSign\":", p.ekf_mag_yaw_sign, 1);
    JU(",\"tuneRequestSeq\":", p.tune_request_seq);
    JU(",\"tuneApplySeq\":", p.tune_apply_seq);
    JU(",\"tuneRejectSeq\":", p.tune_reject_seq);
    JB(",\"tuneDirty\":", p.tune_dirty);
    JU(",\"clients\":", WiFi.softAPgetStationNum());
    JU(",\"requests\":", _requestCount);
    JB(",\"gpsValid\":", p.gps_valid);
    JF(",\"gpsLat\":", p.gps_lat, 6);
    JF(",\"gpsLon\":", p.gps_lon, 6);
    JF(",\"gpsAltM\":", p.gps_altitude_m, 2);
    JF(",\"gpsSpeedKmh\":", p.gps_speed_kmh, 2);
    JF(",\"gpsCourse\":", p.gps_course_deg, 1);
    JU(",\"gpsSats\":", p.gps_satellites);
    JF(",\"gpsHdop\":", p.gps_hdop, 2);
    JU(",\"gpsQuality\":", p.gps_fix_quality);
    JU(",\"gpsHour\":", p.gps_hour);
    JU(",\"gpsMin\":", p.gps_minute);
    JU(",\"gpsSec\":", p.gps_second);
    j += '}';
#undef JB
#undef JI
#undef JU
#undef JF
    return j;
}

bool TelemetryWiFi::_jsonGetFloat(const String& body, const char* key, float& out) const
{
    String pat = "\""; pat += key; pat += "\"";
    int idx = body.indexOf(pat);
    if (idx < 0) return false;
    int pos = idx + pat.length();
    while (pos < (int)body.length() && (body[pos]==' '||body[pos]==':')) pos++;
    String num = "";
    while (pos < (int)body.length()) {
        char c = body[pos];
        if (c=='-'||c=='+'||c=='.'||c=='e'||c=='E'||(c>='0'&&c<='9')) { num+=c; pos++; }
        else break;
    }
    if (!num.length()) return false;
    out = num.toFloat();
    return true;
}


bool TelemetryWiFi::_jsonGetBool(const String& body, const char* key, bool& out) const
{
    String pat = "\""; pat += key; pat += "\"";
    int idx = body.indexOf(pat);
    if (idx < 0) return false;
    int pos = idx + pat.length();
    while (pos < (int)body.length() && (body[pos]==' '||body[pos]==':')) pos++;

    if (body.substring(pos, pos + 4).equalsIgnoreCase("true"))  { out = true;  return true; }
    if (body.substring(pos, pos + 5).equalsIgnoreCase("false")) { out = false; return true; }

    float f = 0.0f;
    if (_jsonGetFloat(body, key, f)) { out = (f > 0.5f || f < -0.5f); return true; }
    return false;
}

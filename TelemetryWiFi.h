/*
 * Name: TelemetryWiFi.h
 * Use: Declaration for WiFi telemetry, tuning, OTA, log, and timing HTTP endpoints.
 * Version: 4.0.0
 * Created by: Durvesh Pathak dp676@cornell.edu
 */

/**
 * ============================================================
 *  TelemetryWiFi.h — ESP32 Wi-Fi Hotspot Telemetry Subsystem
 *  v2.4 — adds /timing, /timing/reset, /timing/csv endpoints
 * ============================================================
 */

#pragma once
#ifndef TELEMETRY_WIFI_H
#define TELEMETRY_WIFI_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "../iFly/FlySkyiBUS.h"

#define WIFI_LOG_CAPACITY   60
#define WIFI_LOG_LINE_LEN  140

// ─────────────────────────────────────────────────────────────
//  Telemetry packet
// ─────────────────────────────────────────────────────────────
struct TelemetryPacket {
    uint32_t tick;
    const char* mode;
    bool armed;
    bool rc_valid;

    // Sensor/estimator health used by GCS status badges
    bool imu_valid;
    bool mag_valid;
    uint8_t ahrs_filter_mode;   // 0=EKF, 1=Mahony, 2=Madgwick
    const char* ahrs_filter;

    // Raw estimator attitude. This is EKF/Mahony/Madgwick output BEFORE level-zero offset.
    float roll_deg, pitch_deg, yaw_deg;
    float q0, q1, q2, q3;

    // Post-AHRS level-zero corrected attitude used by PID/control
    float roll_ctrl_deg, pitch_ctrl_deg, yaw_ctrl_deg;

    // Captured software level-zero offsets, subtracted after AHRS
    float roll_offset_deg, pitch_offset_deg, yaw_offset_deg;
    float ax_g, ay_g, az_g;
    float gx_dps, gy_dps, gz_dps;
    float mx_uT, my_uT, mz_uT;
    float imu_temp_c;
    float raw_ax_g, raw_ay_g, raw_az_g;
    float raw_gx_dps, raw_gy_dps, raw_gz_dps;
    float filt_ax_g, filt_ay_g, filt_az_g;
    float filt_gx_dps, filt_gy_dps, filt_gz_dps;
    float mag_norm_uT;

    // Diagnostic attitude sources and PID corrections
    float accel_roll_deg, accel_pitch_deg;
    float gyro_roll_deg, gyro_pitch_deg, gyro_yaw_deg;
    float roll_angle_error_deg, pitch_angle_error_deg;
    bool  ekf_mag_used;
    float ekf_bgx_dps, ekf_bgy_dps, ekf_bgz_dps;
    float target_roll_deg, target_pitch_deg, target_yaw_deg;
    float target_roll_rate_dps, target_pitch_rate_dps, target_yaw_rate_dps;
    float roll_rate_error_dps, pitch_rate_error_dps, yaw_rate_error_dps;
    float yaw_error_deg;
    float pid_roll_out, pid_pitch_out, pid_yaw_out;
    bool  angle_mode_active, acro_mode_active;
    uint16_t rc_ch1_us, rc_ch2_us, rc_ch3_us, rc_ch4_us, rc_ch5_us;
    uint16_t rc_ch6_us, rc_ch7_us, rc_ch8_us, rc_ch9_us, rc_ch10_us;
    uint32_t rc_failsafe_count;
    uint16_t mode_switch_raw_us, arm_switch_raw_us;
    uint16_t aux_tune1_raw_us, aux_tune2_raw_us;
    bool  angle_loop_enabled, rate_loop_enabled;
    float actual_roll_rate_dps, actual_pitch_rate_dps, actual_yaw_rate_dps;
    float angle_roll_p, angle_roll_i, angle_roll_d;
    float angle_pitch_p, angle_pitch_i, angle_pitch_d;
    float rate_roll_p, rate_roll_i, rate_roll_d;
    float rate_pitch_p, rate_pitch_i, rate_pitch_d;
    float rate_yaw_p, rate_yaw_i, rate_yaw_d;
    float angle_roll_iterm, angle_pitch_iterm;
    uint32_t pid_reset_count;
    uint32_t mode_transition_count, last_mode_change_ms;
    uint32_t arming_transition_count, last_arm_change_ms;
    bool  throttle_low;
    float control_authority_remaining;
    bool  roll_output_limited, pitch_output_limited, yaw_output_limited, rate_output_limited;

    float throttle, rc_roll, rc_pitch, rc_yaw, rc_hz;

    float motor_fl, motor_fr, motor_rl, motor_rr;
    bool  motor_saturated;
    // Commanded/estimated RPM from motor output × KV × Vbat.
    float rpm_fl,   rpm_fr,   rpm_rl,   rpm_rr;
    float cmd_rpm_fl, cmd_rpm_fr, cmd_rpm_rl, cmd_rpm_rr;

    // Actual RPM is only valid if ESC telemetry/RPM sensing is later added.
    float actual_rpm_fl, actual_rpm_fr, actual_rpm_rl, actual_rpm_rr;
    bool  rpm_actual_valid;

    float bmp_temp_c, bmp_pressure_hpa, bmp_altitude_m;
    float bmp_vertical_speed_mps;
    bool  bmp_valid;

    float cpu_core0_pct, cpu_core1_pct;
    bool  cpu_valid;

    // Runtime tuning values reported back to GCS/TestScreen
    // Pilot command limits
    float max_angle_deg;
    float max_rate_dps;
    float max_pitch_rate_dps;

    // PID output authority limits before motor mixing
    float roll_output_limit;
    float pitch_output_limit;
    float yaw_output_limit;

    // Throttle shaping + motor output limits
    float throttle_expo;
    float throttle_up_rate_per_sec;
    float throttle_down_rate_per_sec;
    float motor_idle;
    float motor_max;
    float throttle_cut;
    float idle_ramp_end;

    // Shared PID integrator clamp
    float pid_ilimit;

    // Inner rate loop PID
    float pid_roll_kp,  pid_roll_ki,  pid_roll_kd;
    float pid_pitch_kp, pid_pitch_ki, pid_pitch_kd;
    float pid_yaw_kp,   pid_yaw_ki,   pid_yaw_kd;

    // Outer angle loop PID
    float pid_angle_roll_kp,  pid_angle_roll_ki,  pid_angle_roll_kd;
    float pid_angle_pitch_kp, pid_angle_pitch_ki, pid_angle_pitch_kd;

    // Outer yaw heading-hold loop
    float pid_angle_yaw_kp;
    float yaw_deadband;
    float yaw_max_rate_dps;

    // AHRS / estimator selection
    float mahony_kp, mahony_ki;
    float madgwick_beta;

    // Motor vibration notch filter
    bool  notch_enable;
    float notch_freq_hz;
    float notch_q;

    // Attitude EKF tuning
    float ekf_angle_q;
    float ekf_bias_q;
    float ekf_accel_r;
    float ekf_mag_r;
    float ekf_mag_declination_deg;
    float ekf_mag_yaw_offset_deg;
    float ekf_mag_yaw_sign;

    // Tune transaction diagnostics
    uint32_t tune_request_seq;
    uint32_t tune_apply_seq;
    uint32_t tune_reject_seq;
    bool     tune_dirty;

    // GPS block
    double  gps_lat;
    double  gps_lon;
    float   gps_altitude_m;
    float   gps_speed_kmh;
    float   gps_course_deg;
    uint8_t gps_satellites;
    float   gps_hdop;
    uint8_t gps_fix_quality;
    bool    gps_valid;
    uint8_t gps_hour, gps_minute, gps_second;
};

// ─────────────────────────────────────────────────────────────
//  Tune packet
// ─────────────────────────────────────────────────────────────
struct TunePacket {
    // Pilot command limits
    bool has_max_angle_deg;
    bool has_max_rate_dps;
    bool has_max_pitch_rate_dps;

    // PID output authority limits before motor mixing
    bool has_roll_output_limit;
    bool has_pitch_output_limit;
    bool has_yaw_output_limit;

    // Throttle shaping + motor output limits
    bool has_throttle_expo;
    bool has_throttle_up_rate_per_sec;
    bool has_throttle_down_rate_per_sec;
    bool has_motor_idle;
    bool has_motor_max;
    bool has_throttle_cut;
    bool has_idle_ramp_end;

    // Shared PID integrator clamp
    bool has_pid_ilimit;

    // Inner rate loop PID
    bool has_pid_roll_kp,  has_pid_roll_ki,  has_pid_roll_kd;
    bool has_pid_pitch_kp, has_pid_pitch_ki, has_pid_pitch_kd;
    bool has_pid_yaw_kp,   has_pid_yaw_ki,   has_pid_yaw_kd;

    // Outer angle loop PID
    bool has_pid_angle_roll_kp,  has_pid_angle_roll_ki,  has_pid_angle_roll_kd;
    bool has_pid_angle_pitch_kp, has_pid_angle_pitch_ki, has_pid_angle_pitch_kd;

    // Outer yaw heading-hold loop
    bool has_pid_angle_yaw_kp;
    bool has_yaw_deadband;
    bool has_yaw_max_rate_dps;

    // AHRS
    bool has_mahony_kp, has_mahony_ki;
    bool has_ahrs_filter_mode;
    bool has_madgwick_beta;

    // Motor vibration notch filter
    bool has_notch_enable;
    bool has_notch_freq_hz;
    bool has_notch_q;

    // Attitude EKF tuning
    bool has_ekf_angle_q;
    bool has_ekf_bias_q;
    bool has_ekf_accel_r;
    bool has_ekf_mag_r;
    bool has_ekf_mag_declination_deg;
    bool has_ekf_mag_yaw_offset_deg;
    bool has_ekf_mag_yaw_sign;

    float max_angle_deg;
    float max_rate_dps;
    float max_pitch_rate_dps;

    float roll_output_limit;
    float pitch_output_limit;
    float yaw_output_limit;

    float throttle_expo;
    float throttle_up_rate_per_sec;
    float throttle_down_rate_per_sec;
    float motor_idle;
    float motor_max;
    float throttle_cut;
    float idle_ramp_end;
    float pid_ilimit;

    float pid_roll_kp,  pid_roll_ki,  pid_roll_kd;
    float pid_pitch_kp, pid_pitch_ki, pid_pitch_kd;
    float pid_yaw_kp,   pid_yaw_ki,   pid_yaw_kd;

    float pid_angle_roll_kp,  pid_angle_roll_ki,  pid_angle_roll_kd;
    float pid_angle_pitch_kp, pid_angle_pitch_ki, pid_angle_pitch_kd;

    float pid_angle_yaw_kp;
    float yaw_deadband;
    float yaw_max_rate_dps;

    float mahony_kp, mahony_ki;
    float ahrs_filter_mode;
    float madgwick_beta;

    bool  notch_enable;
    float notch_freq_hz;
    float notch_q;

    float ekf_angle_q;
    float ekf_bias_q;
    float ekf_accel_r;
    float ekf_mag_r;
    float ekf_mag_declination_deg;
    float ekf_mag_yaw_offset_deg;
    float ekf_mag_yaw_sign;
};

// ─────────────────────────────────────────────────────────────
//  TelemetryWiFi class
// ─────────────────────────────────────────────────────────────
class TelemetryWiFi {
public:
    explicit TelemetryWiFi(uint16_t port = 80);

    bool begin(const char* ssid     = "ESP32-DRONE",
               const char* password = "12345678",
               IPAddress   localIp  = IPAddress(192, 168, 4, 1),
               IPAddress   gateway  = IPAddress(192, 168, 4, 1),
               IPAddress   subnet   = IPAddress(255, 255, 255, 0));

    void update();

    // Providers / handlers registered by the main sketch
    void setTelemetryProvider(bool (*provider)(TelemetryPacket& out));
    void setTuneHandler(bool (*handler)(const TunePacket& in));
    void setOtaAllowedProvider(bool (*provider)());

    // v2.4 — timing endpoint providers
    void setTimingProvider(String (*provider)());
    void setTimingCsvProvider(String (*provider)());
    void setTimingResetHandler(void (*handler)());
    void setSpectrumProvider(String (*provider)());

    void pushLog(const char* line);
    void pushLog(const String& line) { pushLog(line.c_str()); }

    // High-speed flight-log endpoints (chunked CSV)
    void setFlightLogCountProvider(uint16_t (*provider)());
    void setFlightLogHeaderProvider(String (*provider)());
    void setFlightLogRowProvider(String (*provider)(uint16_t));
    void setFlightLogResetHandler(void (*handler)());

    IPAddress ip()           const;
    uint32_t  requestCount() const;

private:
    WebServer  _server;
    bool     (*_provider)(TelemetryPacket& out);
    bool     (*_tuneHandler)(const TunePacket& in);
    bool     (*_otaAllowedProvider)();
    bool      _otaInProgress;
    bool      _otaRejected;
    String    _otaError;

    // v2.4 timing callbacks
    String   (*_timingProvider)();
    String   (*_timingCsvProvider)();
    void     (*_timingResetHandler)();
    String   (*_spectrumProvider)();

    uint32_t   _requestCount;

    struct LogEntry {
        uint32_t seq;
        char     text[WIFI_LOG_LINE_LEN];
    };
    LogEntry  _logBuf[WIFI_LOG_CAPACITY];
    uint32_t  _logWriteSeq = 0;
    uint8_t   _logHead     = 0;
    uint8_t   _logCount    = 0;
    portMUX_TYPE _logMux   = portMUX_INITIALIZER_UNLOCKED;

    void _setupRoutes();
    void _sendCorsHeaders();
    void _handleRoot();
    void _handleTelemetry();
    void _handleTune();
    void _handleOtaPage();
    void _handleOtaUpload();
    void _handleOtaFinish();
    bool _isOtaAllowed() const;
    void _handleLog();
    void _handleTiming();
    void _handleTimingReset();
    void _handleTimingCsv();
    void _handleSpectrum();
    void _handleFlightLogCsv();
    void _handleFlightLogReset();
    void _handleOptions();
    void _handleNotFound();

    // flight-log callbacks
    uint16_t (*_flightLogCount)();
    String   (*_flightLogHeader)();
    String   (*_flightLogRow)(uint16_t);
    void     (*_flightLogReset)();

    String _jsonFromPacket(const TelemetryPacket& p) const;
    bool   _jsonGetFloat(const String& body, const char* key, float& out) const;
    bool   _jsonGetBool (const String& body, const char* key, bool& out) const;
};

extern TelemetryWiFi telemetryWiFi;

#endif // TELEMETRY_WIFI_H

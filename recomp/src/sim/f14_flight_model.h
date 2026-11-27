#pragma once

#include <cstdint>

namespace f14 {

struct F14ControlInput {
    float stick_roll = 0.0f;    // -1.0 (left) .. +1.0 (right)
    float stick_pitch = 0.0f;   // -1.0 (nose down) .. +1.0 (nose up / pull back)
    float rudder = 0.0f;        // -1.0 (left) .. +1.0 (right)
    float throttle = 0.75f;     // 0.0 (idle) .. 0.80 (MIL) .. 1.0 (MAX AB)
    bool has_analog_throttle = false;
    bool speedbrake = false;
    bool flaps_down = false;
    bool gear_down = false;
    bool hook_down = false;
};

struct F14FlightModelConfig {
    bool enabled = false;           // Toggleable live in F12 UI (A/B compare vs original Fleet Defender!)
    bool engine_f110_ge400 = false; // false = F-14A twin TF30-P-414A (41,800 lbf AB), true = F-14B/D twin F110-GE-400 (55,200 lbf AB)
    bool auto_wing_sweep = true;    // Authentic F-14 CADC automatic wing sweep (20 deg .. 68 deg vs Mach)
    float manual_sweep_deg = 20.0f; // Manual wing sweep override (20..68 deg) when auto_wing_sweep == false
    bool aoa_limiter = false;       // Optional soft AOA warning/limiter (authentic F-14A has NO hard AOA limiter!)
    float max_roll_rate_dps = 210.0f;
    float stick_pitch_sens = 1.0f;
    float stick_roll_sens = 1.0f;
};

struct F14SimState {
    bool initialized = false;
    // World position in Fleet Defender native 20-bit coords (X=East, Y=South, Alt in feet)
    double world_x = 0.0;
    double world_y = 0.0;
    double alt_ft = 5000.0;
    double ground_alt_ft = 0.0;

    // Velocity in North-East-Up ft/s
    double vn_fps = 500.0;
    double ve_fps = 0.0;
    double vu_fps = 0.0;

    // Euler attitude (degrees): yaw 0..360 (0=N, 90=E, 270=W), pitch -89.5..+89.5, roll -180..+180
    double yaw_deg = 0.0;
    double pitch_deg = 0.0;
    double roll_deg = 0.0;

    // Persisted orthonormal body basis in NEU (gimbal-lock-free through vertical loops)
    double fx = 1.0, fy = 0.0, fz = 0.0;
    double rx = 0.0, ry = 1.0, rz = 0.0;
    double ux = 0.0, uy = 0.0, uz = 1.0;

    // Live F-14 aerodynamic outputs
    double tas_fps = 500.0;
    double kias = 300.0;
    double mach = 0.45;
    double aoa_deg = 4.0;
    double beta_deg = 0.0;
    double g_load = 1.0;
    double max_g = 1.0;
    double wing_sweep_deg = 20.0;   // CADC live wing sweep (20..68 deg)
    double roll_rate_dps = 0.0;
    double pitch_rate_dps = 0.0;
    double turn_rate_dps = 0.0;
    double ps_fps = 0.0;
    double thrust_lbf = 16000.0;
    double drag_lbf = 16000.0;
    double throttle_cmd = 0.75;
    double rpm_pct = 85.0;
    double weight_lbs = 58000.0;    // Typical F-14A combat weight (empty ~40,100 + fuel + missiles)
    double fp_yaw_deg = 0.0;
    double fp_pitch_deg = 0.0;
};

class F14FlightModel {
public:
    F14FlightModel() = default;

    void sync_from_game(double wx, double wy, float alt_ft, float agl_ft,
                        float yaw_deg, float pitch_deg, float roll_deg,
                        float kias, float weight_lbs, float throttle_01);
    void reset() { state_.initialized = false; }

    // Step authentic 6-DOF F-14A/B Tomcat physics by dt_sec (sub-stepped at 240 Hz)
    void step(double dt_sec, const F14ControlInput& in);

    F14FlightModelConfig& config() { return cfg_; }
    const F14FlightModelConfig& config() const { return cfg_; }
    const F14SimState& state() const { return state_; }
    bool is_initialized() const { return state_.initialized; }

private:
    F14FlightModelConfig cfg_;
    F14SimState state_;

    void substep(double dt, const F14ControlInput& in);
};

} // namespace f14

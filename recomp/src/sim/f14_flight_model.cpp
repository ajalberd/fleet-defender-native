#include "sim/f14_flight_model.h"

#include <algorithm>
#include <cmath>

namespace f14 {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kG = 32.174; // ft/s^2

// Standard US 1976 Atmosphere (altitude in ft) -> density ratio sigma & speed of sound (ft/s)
void isa_atmosphere(double alt_ft, double& out_rho_slug_ft3, double& out_sigma, double& out_a_fps) {
    double h = std::clamp(alt_ft, 0.0, 65000.0);
    double temp_r = (h <= 36089.0) ? (518.67 - 0.00356616 * h) : 389.97;
    double p_ratio = (h <= 36089.0)
        ? std::pow(temp_r / 518.67, 5.25588)
        : 0.223361 * std::exp(-0.0000480634 * (h - 36089.0));
    out_sigma = p_ratio * (518.67 / temp_r);
    out_rho_slug_ft3 = 0.0023769 * out_sigma;
    out_a_fps = std::sqrt(1.4 * 1716.49 * temp_r);
}

// Compute F-14 CADC scheduled wing sweep (20 deg subsonic -> 68 deg supersonic)
double cadc_wing_sweep_deg(double mach) {
    if (mach <= 0.65) return 20.0;
    if (mach <= 0.90) return 20.0 + (mach - 0.65) / 0.25 * 25.0; // 20 -> 45 deg
    if (mach <= 1.25) return 45.0 + (mach - 0.90) / 0.35 * 23.0; // 45 -> 68 deg
    return 68.0;
}

void basis_from_euler(double yaw_deg, double pitch_deg, double roll_deg,
                      double& fx, double& fy, double& fz,
                      double& rx, double& ry, double& rz,
                      double& ux, double& uy, double& uz) {
    double cy = std::cos(yaw_deg * kDegToRad), sy = std::sin(yaw_deg * kDegToRad);
    double cp = std::cos(pitch_deg * kDegToRad), sp = std::sin(pitch_deg * kDegToRad);
    double cr = std::cos(roll_deg * kDegToRad), sr = std::sin(roll_deg * kDegToRad);

    fx = cp * cy;
    fy = cp * sy;
    fz = sp;

    // Horizontal right vector before bank = (-sy, cy, 0), vertical up before bank = (-sp*cy, -sp*sy, cp)
    double hr_x = -sy, hr_y = cy, hr_z = 0.0;
    double hu_x = -sp * cy, hu_y = -sp * sy, hu_z = cp;

    rx = hr_x * cr - hu_x * sr;
    ry = hr_y * cr - hu_y * sr;
    rz = hr_z * cr - hu_z * sr;

    ux = hr_x * sr + hu_x * cr;
    uy = hr_y * sr + hu_y * cr;
    uz = hr_z * sr + hu_z * cr;
}

void orthonormalize(double& fx, double& fy, double& fz,
                    double& rx, double& ry, double& rz,
                    double& ux, double& uy, double& uz) {
    auto norm = [](double& x, double& y, double& z) {
        double l = std::sqrt(x * x + y * y + z * z);
        if (l > 1e-12) { x /= l; y /= l; z /= l; }
    };
    norm(fx, fy, fz);
    // In (North, East, Up): F x R = U, U x F = R
    rx = uy * fz - uz * fy;
    ry = uz * fx - ux * fz;
    rz = ux * fy - uy * fx;
    norm(rx, ry, rz);
    // U = F x R
    ux = fy * rz - fz * ry;
    uy = fz * rx - fx * rz;
    uz = fx * ry - fy * rx;
    norm(ux, uy, uz);
}

void rotate_basis_around(double ax, double ay, double az, double rad,
                         double& x, double& y, double& z) {
    double c = std::cos(rad), s = std::sin(rad), ic = 1.0 - c;
    double dot = ax * x + ay * y + az * z;
    double cx = ay * z - az * y;
    double cy = az * x - ax * z;
    double cz = ax * y - ay * x;
    x = x * c + cx * s + ax * dot * ic;
    y = y * c + cy * s + ay * dot * ic;
    z = z * c + cz * s + az * dot * ic;
}

} // namespace

void F14FlightModel::sync_from_game(double wx, double wy, float alt_ft, float agl_ft,
                                    float yaw_deg, float pitch_deg, float roll_deg,
                                    float kias, float weight_lbs, float throttle_01) {
    state_.initialized = true;
    state_.world_x = wx;
    state_.world_y = wy;
    state_.alt_ft = std::max(0.0, static_cast<double>(alt_ft));
    state_.ground_alt_ft = std::max(0.0, static_cast<double>(alt_ft - agl_ft));
    state_.yaw_deg = yaw_deg;
    state_.pitch_deg = std::clamp(static_cast<double>(pitch_deg), -89.5, 89.5);
    state_.roll_deg = roll_deg;
    state_.weight_lbs = std::clamp(static_cast<double>(weight_lbs), 42000.0, 74349.0);
    state_.throttle_cmd = std::clamp(static_cast<double>(throttle_01), 0.0, 1.0);

    basis_from_euler(state_.yaw_deg, state_.pitch_deg, state_.roll_deg,
                     state_.fx, state_.fy, state_.fz,
                     state_.rx, state_.ry, state_.rz,
                     state_.ux, state_.uy, state_.uz);

    double rho = 0.0023769, sigma = 1.0, a_fps = 1116.4;
    isa_atmosphere(state_.alt_ft, rho, sigma, a_fps);
    double v_kias = std::max(60.0, static_cast<double>(kias));
    double tas = (v_kias * 1.68781) / std::sqrt(std::max(0.08, sigma));
    state_.tas_fps = tas;
    state_.kias = v_kias;
    state_.mach = tas / a_fps;
    state_.wing_sweep_deg = cfg_.auto_wing_sweep ? cadc_wing_sweep_deg(state_.mach) : cfg_.manual_sweep_deg;

    state_.vn_fps = state_.fx * tas;
    state_.ve_fps = state_.fy * tas;
    state_.vu_fps = state_.fz * tas;
    state_.aoa_deg = 3.0;
    state_.beta_deg = 0.0;
    state_.g_load = 1.0;
    state_.max_g = 1.0;
}

void F14FlightModel::step(double dt_sec, const F14ControlInput& in) {
    if (!state_.initialized) return;
    dt_sec = std::clamp(dt_sec, 0.0, 0.05);
    constexpr double kSubDt = 1.0 / 240.0;
    int steps = std::max(1, static_cast<int>(std::ceil(dt_sec / kSubDt)));
    double h = dt_sec / static_cast<double>(steps);
    for (int i = 0; i < steps; ++i) {
        substep(h, in);
    }
}

void F14FlightModel::substep(double dt, const F14ControlInput& in) {
    double rho = 0.0023769, sigma = 1.0, a_fps = 1116.4;
    isa_atmosphere(state_.alt_ft, rho, sigma, a_fps);

    double tas = std::sqrt(state_.vn_fps * state_.vn_fps +
                           state_.ve_fps * state_.ve_fps +
                           state_.vu_fps * state_.vu_fps);
    tas = std::max(tas, 1.0);
    double mach = tas / a_fps;
    double qbar = 0.5 * rho * tas * tas; // lbf/ft^2

    // 1. Variable-sweep wing scheduling (CADC 20..68 deg)
    double target_sweep = cfg_.auto_wing_sweep ? cadc_wing_sweep_deg(mach) : std::clamp(static_cast<double>(cfg_.manual_sweep_deg), 20.0, 68.0);
    if (in.flaps_down || in.gear_down) target_sweep = 20.0;
    double sweep_rate = 7.5; // deg/sec hydraulic sweep rate
    double d_sweep = std::clamp(target_sweep - state_.wing_sweep_deg, -sweep_rate * dt, sweep_rate * dt);
    state_.wing_sweep_deg = std::clamp(state_.wing_sweep_deg + d_sweep, 20.0, 68.0);
    double sweep_frac = (state_.wing_sweep_deg - 20.0) / 48.0; // 0 = 20 deg unswept, 1 = 68 deg swept

    // 2. Body-frame velocity components
    double vf = state_.vn_fps * state_.fx + state_.ve_fps * state_.fy + state_.vu_fps * state_.fz;
    double vr = state_.vn_fps * state_.rx + state_.ve_fps * state_.ry + state_.vu_fps * state_.rz;
    double vu = state_.vn_fps * state_.ux + state_.ve_fps * state_.uy + state_.vu_fps * state_.uz;

    double aoa_rad = std::atan2(-vu, std::max(10.0, vf));
    double beta_rad = std::atan2(vr, std::max(10.0, tas));
    double aoa_deg = aoa_rad * kRadToDeg;
    double beta_deg = beta_rad * kRadToDeg;

    // 3. F-14 Tomcat Aerodynamics (S = 565 ft^2, variable AR from 7.28 @ 20 deg to 2.6 @ 68 deg)
    constexpr double kWingArea = 565.0;
    double cl_alpha = (4.85 - 1.55 * sweep_frac) / kRadToDeg; // per deg
    double cl_max = (1.55 - 0.35 * sweep_frac) + (in.flaps_down ? 0.45 : 0.0);
    double cl = std::clamp(0.08 + cl_alpha * aoa_deg + (in.flaps_down ? 0.35 : 0.0), -1.1, cl_max);

    // Transonic wave drag rises around M=0.88..1.15, reduced when wings are swept to 68 deg!
    double cd0 = 0.021 - 0.004 * sweep_frac;
    if (mach > 0.85) {
        double dm = std::min(mach - 0.85, 0.35);
        cd0 += (0.042 - 0.020 * sweep_frac) * dm * dm * 6.0;
    }
    if (in.speedbrake) cd0 += 0.055;
    if (in.gear_down) cd0 += 0.045;
    if (in.flaps_down) cd0 += 0.035;

    double ar = 7.28 - 4.4 * sweep_frac;
    double e_oswald = 0.80 - 0.12 * sweep_frac;
    double k_ind = 1.0 / (kPi * ar * e_oswald);
    double cd = cd0 + k_ind * cl * cl;

    double lift_lbf = qbar * kWingArea * cl;
    double drag_lbf = qbar * kWingArea * cd;
    double side_lbf = -qbar * kWingArea * 0.65 * beta_rad;

    // 4. Twin Engine Propulsion (F-14A TF30-P-414A vs F-14B/D F110-GE-400)
    double thr_cmd = in.has_analog_throttle ? std::clamp(static_cast<double>(in.throttle), 0.0, 1.0) : state_.throttle_cmd;
    state_.throttle_cmd = thr_cmd;

    double sl_mil = cfg_.engine_f110_ge400 ? 33200.0 : 24700.0; // Twin engines total MIL (lbf)
    double sl_ab  = cfg_.engine_f110_ge400 ? 55200.0 : 41800.0; // Twin engines total MAX AB (lbf)
    double ram_factor = 1.0 + 0.32 * mach * mach;
    double alt_lapse = std::pow(sigma, 0.72);
    double avail_mil = sl_mil * alt_lapse * ram_factor;
    double avail_ab  = sl_ab  * alt_lapse * (1.0 + 0.45 * mach * mach);

    double target_thrust = (thr_cmd <= 0.80)
        ? (avail_mil * (0.06 + 0.94 * (thr_cmd / 0.80)))
        : (avail_mil + (avail_ab - avail_mil) * ((thr_cmd - 0.80) / 0.20));

    // TF30 spools slightly slower than F110
    double spool_tau = cfg_.engine_f110_ge400 ? 0.85 : 1.45;
    state_.thrust_lbf += (target_thrust - state_.thrust_lbf) * std::min(1.0, dt / spool_tau);
    state_.drag_lbf = drag_lbf;
    state_.rpm_pct = (thr_cmd <= 0.80) ? (62.0 + (thr_cmd / 0.80) * 38.0) : (100.0 + ((thr_cmd - 0.80) / 0.20) * 8.0);

    // 5. Rotational control (differential tailerons + spoilers + twin rudders)
    double q_auth = std::clamp(qbar / 180.0, 0.15, 1.0);
    double max_roll = static_cast<double>(cfg_.max_roll_rate_dps) * (1.0 - 0.22 * sweep_frac) * q_auth;
    double cmd_roll_dps = static_cast<double>(in.stick_roll) * cfg_.stick_roll_sens * max_roll;
    state_.roll_rate_dps += (cmd_roll_dps - state_.roll_rate_dps) * std::min(1.0, dt * 10.0);

    // Pitch authority + natural static stability
    double max_pitch_dps = 24.0 * q_auth;
    double cmd_pitch_dps = static_cast<double>(in.stick_pitch) * cfg_.stick_pitch_sens * max_pitch_dps;
    if (cfg_.aoa_limiter && aoa_deg > 22.0 && cmd_pitch_dps > 0.0) {
        cmd_pitch_dps *= std::max(0.0, 1.0 - (aoa_deg - 22.0) / 6.0);
    }
    // Natural weathervane pitch stability toward 1G trim AOA
    double trim_aoa = std::clamp((state_.weight_lbs / std::max(1.0, qbar * kWingArea) - 0.08) / cl_alpha, -2.0, 16.0);
    double pitch_stab_dps = (trim_aoa - aoa_deg) * 1.4;
    double target_pitch_dps = cmd_pitch_dps + pitch_stab_dps * (1.0 - std::min(1.0, static_cast<double>(std::abs(in.stick_pitch))));
    state_.pitch_rate_dps += (target_pitch_dps - state_.pitch_rate_dps) * std::min(1.0, dt * 8.0);

    double yaw_rate_dps = static_cast<double>(in.rudder) * 28.0 * q_auth - beta_deg * 2.2;

    // Rotate orthonormal body basis in (N, E, U):
    // +dp (nose up) rotates F->+U via -dp around R; +dr (right wing down) via -dr around F; +dy (nose right) via +dy around U
    double dp = state_.pitch_rate_dps * kDegToRad * dt;
    double dr = state_.roll_rate_dps * kDegToRad * dt;
    double dy = yaw_rate_dps * kDegToRad * dt;

    rotate_basis_around(state_.rx, state_.ry, state_.rz, -dp, state_.fx, state_.fy, state_.fz);
    rotate_basis_around(state_.rx, state_.ry, state_.rz, -dp, state_.ux, state_.uy, state_.uz);
    rotate_basis_around(state_.fx, state_.fy, state_.fz, -dr, state_.rx, state_.ry, state_.rz);
    rotate_basis_around(state_.fx, state_.fy, state_.fz, -dr, state_.ux, state_.uy, state_.uz);
    rotate_basis_around(state_.ux, state_.uy, state_.uz, +dy, state_.fx, state_.fy, state_.fz);
    rotate_basis_around(state_.ux, state_.uy, state_.uz, +dy, state_.rx, state_.ry, state_.rz);
    orthonormalize(state_.fx, state_.fy, state_.fz, state_.rx, state_.ry, state_.rz, state_.ux, state_.uy, state_.uz);

    // 6. Translational equations of motion in NEU
    double mass = state_.weight_lbs / kG;
    double v_inv = 1.0 / tas;
    double vx_u = state_.vn_fps * v_inv, vy_u = state_.ve_fps * v_inv, vz_u = state_.vu_fps * v_inv;

    // Lift direction perpendicular to velocity in the body F-U plane
    double lx = state_.ux - (state_.ux * vx_u + state_.uy * vy_u + state_.uz * vz_u) * vx_u;
    double ly = state_.uy - (state_.ux * vx_u + state_.uy * vy_u + state_.uz * vz_u) * vy_u;
    double lz = state_.uz - (state_.ux * vx_u + state_.uy * vy_u + state_.uz * vz_u) * vz_u;
    double ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    if (ll > 1e-9) { lx /= ll; ly /= ll; lz /= ll; }

    double fn = state_.thrust_lbf * state_.fx - drag_lbf * vx_u + lift_lbf * lx + side_lbf * state_.rx;
    double fe = state_.thrust_lbf * state_.fy - drag_lbf * vy_u + lift_lbf * ly + side_lbf * state_.ry;
    double fu = state_.thrust_lbf * state_.fz - drag_lbf * vz_u + lift_lbf * lz + side_lbf * state_.rz - state_.weight_lbs;

    state_.vn_fps += (fn / mass) * dt;
    state_.ve_fps += (fe / mass) * dt;
    state_.vu_fps += (fu / mass) * dt;

    // Integrate position (Fleet Defender 20-bit coords: exact 4.0 ft per world unit!)
    constexpr double kWorldUnitsPerFt = 1.0 / 4.0;
    state_.world_x += state_.ve_fps * kWorldUnitsPerFt * dt;
    state_.world_y -= state_.vn_fps * kWorldUnitsPerFt * dt;
    state_.alt_ft = std::max(state_.ground_alt_ft, state_.alt_ft + state_.vu_fps * dt);

    // Update derived outputs
    state_.tas_fps = std::sqrt(state_.vn_fps * state_.vn_fps + state_.ve_fps * state_.ve_fps + state_.vu_fps * state_.vu_fps);
    state_.mach = state_.tas_fps / a_fps;
    state_.kias = (state_.tas_fps * std::sqrt(std::max(0.08, sigma))) / 1.68781;
    state_.aoa_deg = aoa_deg;
    state_.beta_deg = beta_deg;
    state_.g_load = std::clamp(lift_lbf / state_.weight_lbs, -3.5, 9.5);
    state_.max_g = std::max(state_.max_g, state_.g_load);
    state_.ps_fps = ((state_.thrust_lbf - drag_lbf) * state_.tas_fps) / state_.weight_lbs;

    // Extract Euler angles from body basis
    state_.pitch_deg = std::clamp(std::asin(std::clamp(state_.fz, -1.0, 1.0)) * kRadToDeg, -89.5, 89.5);
    double yaw = std::atan2(state_.fy, state_.fx) * kRadToDeg;
    if (yaw < 0.0) yaw += 360.0;
    state_.yaw_deg = yaw;

    double cy = std::cos(yaw * kDegToRad), sy = std::sin(yaw * kDegToRad);
    double hr_x = -sy, hr_y = cy;
    double r_dot_hr = state_.rx * hr_x + state_.ry * hr_y;
    double u_dot_hr = state_.ux * hr_x + state_.uy * hr_y;
    state_.roll_deg = std::atan2(u_dot_hr, r_dot_hr) * kRadToDeg;

    double horiz_v = std::sqrt(state_.vn_fps * state_.vn_fps + state_.ve_fps * state_.ve_fps);
    state_.fp_pitch_deg = std::atan2(state_.vu_fps, std::max(1.0, horiz_v)) * kRadToDeg;
    double fpy = std::atan2(state_.ve_fps, state_.vn_fps) * kRadToDeg;
    if (fpy < 0.0) fpy += 360.0;
    state_.fp_yaw_deg = fpy;
}

} // namespace f14

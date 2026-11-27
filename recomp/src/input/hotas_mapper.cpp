#include "input/hotas_mapper.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace f14 {

const char* flight_axis_name(FlightAxis a) {
    switch (a) {
        case FlightAxis::Roll: return "Roll (Aileron)";
        case FlightAxis::Pitch: return "Pitch (Elevator)";
        case FlightAxis::Throttle: return "Throttle";
        case FlightAxis::Yaw: return "Yaw (Rudder)";
        case FlightAxis::RadarElev: return "Radar Antenna Elev";
        default: return "Unknown";
    }
}

HotasMapper::HotasMapper() {
    axis_out_[static_cast<int>(FlightAxis::Throttle)] = 0.5f;
    init_default_commands();
}

HotasMapper::~HotasMapper() {
    shutdown();
}

void HotasMapper::init_default_commands() {
    button_bindings_.clear();
    // Authentic Fleet Defender (F-14 Tomcat) keyboard commands (verified against CMDS.C & KEYCODES.H)
    button_bindings_.push_back({"Fire Selected Weapon (Space)", 0x39, ' '});
    button_bindings_.push_back({"Master Arm Toggle (M)", 0x32, 'm'});
    button_bindings_.push_back({"Cycle Weapon (Enter)", 0x1C, '\r'});
    button_bindings_.push_back({"Uncage Sidewinder (U)", 0x16, 'u'});
    button_bindings_.push_back({"Dispense Chaff (C)", 0x2E, 'c'});
    button_bindings_.push_back({"Dispense Flare (F)", 0x21, 'f'});
    button_bindings_.push_back({"ECM Jammer (J)", 0x24, 'j'});
    button_bindings_.push_back({"AWG-9 Radar Lock (L)", 0x26, 'l'});
    button_bindings_.push_back({"AWG-9 Break Lock (K)", 0x25, 'k'});
    button_bindings_.push_back({"AWG-9 Acquire Target (Backspace)", 0x0E, '\b'});
    button_bindings_.push_back({"AWG-9 Radar Power (R)", 0x13, 'r'});
    button_bindings_.push_back({"AWG-9 Search Mode (Delete)", 0x53, 0});
    button_bindings_.push_back({"AWG-9 Azimuth / Scan (Insert)", 0x52, 0});
    button_bindings_.push_back({"AWG-9 ACM Special Mode (End)", 0x4F, 0});
    button_bindings_.push_back({"AWG-9 Radar Bars (Home)", 0x47, 0});
    button_bindings_.push_back({"AWG-9 Antenna Elev Up (PgUp)", 0x49, 0});
    button_bindings_.push_back({"AWG-9 Antenna Elev Dn (PgDn)", 0x51, 0});
    button_bindings_.push_back({"AWG-9 IFF Interrogate (I)", 0x17, 'i'});
    button_bindings_.push_back({"Toggle Pilot / RIO Seat (')", 0x28, '\''});
    button_bindings_.push_back({"RIO Tactical Map (/)", 0x35, '/'});
    button_bindings_.push_back({"Full Afterburner (A)", 0x1E, 'a'});
    button_bindings_.push_back({"Throttle Up (=)", 0x0D, '='});
    button_bindings_.push_back({"Throttle Down (-)", 0x0C, '-'});
    button_bindings_.push_back({"Airbrake / Brakes (B)", 0x30, 'b'});
    button_bindings_.push_back({"Landing Gear (G)", 0x22, 'g'});
    button_bindings_.push_back({"Tailhook / Arresting Hook (H)", 0x23, 'h'});
    button_bindings_.push_back({"Autopilot (P)", 0x19, 'p'});
    button_bindings_.push_back({"VDI Mode Toggle (V)", 0x2F, 'v'});
    button_bindings_.push_back({"HUD Declutter (D)", 0x20, 'd'});
    button_bindings_.push_back({"Rudder Left (,)", 0x33, ','});
    button_bindings_.push_back({"Rudder Right (.)", 0x34, '.'});
    button_bindings_.push_back({"View Forward Cockpit (F1)", 0x3B, 0});
    button_bindings_.push_back({"View HUD Only / Frontal (F2)", 0x3C, 0});
    button_bindings_.push_back({"View Carrier LSO (F3)", 0x3D, 0});
    button_bindings_.push_back({"View Carrier Tower (F4)", 0x3E, 0});
    button_bindings_.push_back({"View External Chase (F5)", 0x3F, 0});
    button_bindings_.push_back({"View Pilot Free-Look (F6)", 0x40, 0});
    button_bindings_.push_back({"View Missile Cam (F7)", 0x41, 0});
    button_bindings_.push_back({"View Padlock Threat (F8)", 0x42, 0});
    button_bindings_.push_back({"View Tactical 1 (F9)", 0x43, 0});
    button_bindings_.push_back({"View Tactical 2 / Enemy (F10)", 0x44, 0});
    button_bindings_.push_back({"Cockpit Look Left (KP4)", 0x4B, 0});
    button_bindings_.push_back({"Cockpit Look Right (KP6)", 0x4D, 0});
    button_bindings_.push_back({"Cockpit Look Rear-Left (KP1)", 0x4F, 0});
    button_bindings_.push_back({"Cockpit Look Rear-Right (KP3)", 0x51, 0});
    button_bindings_.push_back({"Cockpit Look Down / Instruments (KP2)", 0x50, 0});

    pinky_shift_.action_name = "Shift / Pinky Modifier";
}

void HotasMapper::init() {
    refresh_devices();
}

void HotasMapper::shutdown() {
    for (auto& d : devices_) {
        if (d.handle) {
            SDL_CloseJoystick(d.handle);
            d.handle = nullptr;
        }
    }
    devices_.clear();
    raw_stuck_buttons_.clear();
}

void HotasMapper::refresh_devices() {
    shutdown();
    SDL_UpdateJoysticks();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (!ids) return;

    for (int i = 0; i < count; ++i) {
        SDL_Joystick* joy = SDL_OpenJoystick(ids[i]);
        if (!joy) continue;

        HotasDeviceInfo info;
        info.instance_id = ids[i];
        info.handle = joy;
        const char* name = SDL_GetJoystickName(joy);
        info.name = name ? name : "Unknown HID Joystick";

        char guid_str[64]{};
        SDL_GUIDToString(SDL_GetJoystickGUID(joy), guid_str, sizeof(guid_str));
        info.guid = guid_str;

        info.num_axes = SDL_GetNumJoystickAxes(joy);
        info.num_buttons = SDL_GetNumJoystickButtons(joy);
        info.num_hats = SDL_GetNumJoystickHats(joy);
        info.axis_values.assign(info.num_axes, 0.0f);
        info.button_states.assign(info.num_buttons, false);
        info.hat_states.assign(info.num_hats, 0);

        // Snapshot live initial button states so always-ON HOTAS toggle switches are recognized immediately!
        std::vector<bool> initial_btns(info.num_buttons, false);
        for (int b = 0; b < info.num_buttons; ++b) {
            bool pressed = SDL_GetJoystickButton(joy, b);
            info.button_states[b] = pressed;
            initial_btns[b] = pressed;
        }
        for (int a = 0; a < info.num_axes; ++a) {
            Sint16 raw = SDL_GetJoystickAxis(joy, a);
            info.axis_values[a] = std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f);
        }
        for (int h = 0; h < info.num_hats; ++h) {
            info.hat_states[h] = SDL_GetJoystickHat(joy, h);
        }

        raw_stuck_buttons_.push_back(std::move(initial_btns));
        devices_.push_back(std::move(info));
    }
    SDL_free(ids);
}

void HotasMapper::handle_sdl_event(const SDL_Event& ev) {
    if (ev.type == SDL_EVENT_JOYSTICK_ADDED || ev.type == SDL_EVENT_JOYSTICK_REMOVED) {
        refresh_devices();
    }
}

void HotasMapper::snapshot_button_hat_baselines() {
    SDL_UpdateJoysticks();
    capture_baseline_buttons_.clear();
    capture_baseline_hats_.clear();
    for (auto& d : devices_) {
        std::vector<bool> bsnap(d.num_buttons, false);
        std::vector<uint8_t> hsnap(d.num_hats, SDL_HAT_CENTERED);
        if (d.handle) {
            for (int b = 0; b < d.num_buttons; ++b) {
                bool p = SDL_GetJoystickButton(d.handle, b);
                d.button_states[b] = p;
                bsnap[b] = p;
            }
            for (int h = 0; h < d.num_hats; ++h) {
                uint8_t hv = SDL_GetJoystickHat(d.handle, h);
                d.hat_states[h] = hv;
                hsnap[h] = hv;
            }
        }
        capture_baseline_buttons_.push_back(std::move(bsnap));
        capture_baseline_hats_.push_back(std::move(hsnap));
    }
}

void HotasMapper::start_axis_capture(FlightAxis axis) {
    cancel_capture();
    SDL_UpdateJoysticks();
    capturing_axis_ = static_cast<int>(axis);
    capture_baseline_axes_.clear();
    for (auto& d : devices_) {
        if (d.handle) {
            for (int a = 0; a < d.num_axes; ++a) {
                Sint16 raw = SDL_GetJoystickAxis(d.handle, a);
                d.axis_values[a] = std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f);
            }
        }
        capture_baseline_axes_.push_back(d.axis_values);
    }
}

void HotasMapper::start_button_capture(int binding_idx) {
    cancel_capture();
    snapshot_button_hat_baselines();
    capturing_button_ = binding_idx;
}

void HotasMapper::start_pinky_capture() {
    cancel_capture();
    snapshot_button_hat_baselines();
    capturing_pinky_ = true;
}

void HotasMapper::cancel_capture() {
    capturing_axis_ = -1;
    capturing_button_ = -1;
    capturing_pinky_ = false;
}

const HotasDeviceInfo* HotasMapper::find_device_by_guid(const std::string& guid) const {
    if (guid.empty()) return nullptr;
    for (const auto& d : devices_) {
        if (d.guid == guid) return &d;
    }
    return nullptr;
}

float HotasMapper::apply_axis_curve(float raw, const AxisBinding& b) const {
    float v = b.invert ? -raw : raw;
    float av = std::fabs(v);
    if (av < b.deadzone) return 0.0f;
    float norm = (av - b.deadzone) / std::max(0.001f, 1.0f - b.deadzone);
    float shaped = norm * (1.0f - b.curve) + (norm * norm * norm) * b.curve;
    return (v < 0.0f) ? -shaped : shaped;
}

bool HotasMapper::is_source_pressed(const ButtonBinding& b) const {
    if (b.source == InputSourceType::None) return false;
    const HotasDeviceInfo* d = find_device_by_guid(b.device_guid);
    if (!d) return false;
    if (b.source == InputSourceType::Button) {
        if (b.index >= 0 && b.index < static_cast<int>(d->button_states.size())) {
            return d->button_states[b.index];
        }
    } else if (b.source == InputSourceType::Hat) {
        if (b.index >= 0 && b.index < static_cast<int>(d->hat_states.size())) {
            return (d->hat_states[b.index] & b.hat_mask) != 0;
        }
    }
    return false;
}

void HotasMapper::update(const KeyInjectCallback& key_cb) {
    SDL_UpdateJoysticks();

    // 1. Read raw states from all connected devices
    for (size_t di = 0; di < devices_.size(); ++di) {
        auto& d = devices_[di];
        if (!d.handle) continue;
        for (int a = 0; a < d.num_axes; ++a) {
            Sint16 raw = SDL_GetJoystickAxis(d.handle, a);
            d.axis_values[a] = std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f);
        }
        for (int b = 0; b < d.num_buttons; ++b) {
            d.button_states[b] = SDL_GetJoystickButton(d.handle, b);
        }
        for (int h = 0; h < d.num_hats; ++h) {
            d.hat_states[h] = SDL_GetJoystickHat(d.handle, h);
        }
    }

    // 2. Handle active Press-to-Bind capture
    if (capturing_axis_ >= 0) {
        for (size_t di = 0; di < devices_.size(); ++di) {
            if (di >= capture_baseline_axes_.size()) break;
            for (int a = 0; a < devices_[di].num_axes; ++a) {
                if (a >= static_cast<int>(capture_baseline_axes_[di].size())) break;
                if (std::fabs(devices_[di].axis_values[a] - capture_baseline_axes_[di][a]) > 0.35f) {
                    auto& ab = axis_bindings_[capturing_axis_];
                    ab.device_guid = devices_[di].guid;
                    ab.device_name = devices_[di].name;
                    ab.axis_index = a;
                    axis_bindings_changed_ = true;
                    cancel_capture();
                    return;
                }
            }
        }
        return;
    }

    if (capturing_button_ >= 0 || capturing_pinky_) {
        ButtonBinding* target = capturing_pinky_ ? &pinky_shift_ : &button_bindings_[capturing_button_];
        for (size_t di = 0; di < devices_.size(); ++di) {
            const auto& d = devices_[di];
            if (di >= capture_baseline_buttons_.size() || di >= capture_baseline_hats_.size()) continue;
            auto& base_btns = capture_baseline_buttons_[di];
            auto& base_hats = capture_baseline_hats_[di];

            for (int b = 0; b < d.num_buttons && b < static_cast<int>(base_btns.size()); ++b) {
                bool cur = d.button_states[b];
                if (cur && !base_btns[b]) {
                    // Rising edge delta (newly pressed button — ignores HOTAS toggle switches held ON by default!)
                    target->source = InputSourceType::Button;
                    target->device_guid = d.guid;
                    target->device_name = d.name;
                    target->index = b;
                    target->hat_mask = 0;
                    target->prev_pressed = true; // Latch so releasing doesn't fire a spurious keypress
                    cancel_capture();
                    return;
                } else if (!cur && base_btns[b]) {
                    // Toggle switch that started ON was flipped OFF -> clear baseline so flipping it back ON can bind
                    base_btns[b] = false;
                }
            }
            for (int h = 0; h < d.num_hats && h < static_cast<int>(base_hats.size()); ++h) {
                uint8_t cur = d.hat_states[h];
                if (cur != SDL_HAT_CENTERED && cur != base_hats[h]) {
                    target->source = InputSourceType::Hat;
                    target->device_guid = d.guid;
                    target->device_name = d.name;
                    target->index = h;
                    target->hat_mask = cur;
                    target->prev_pressed = true;
                    cancel_capture();
                    return;
                } else if (cur == SDL_HAT_CENTERED) {
                    base_hats[h] = SDL_HAT_CENTERED;
                }
            }
        }
        return;
    }

    // 3. Evaluate analog flight axes
    for (int i = 0; i < static_cast<int>(FlightAxis::Count); ++i) {
        const auto& ab = axis_bindings_[i];
        const HotasDeviceInfo* d = find_device_by_guid(ab.device_guid);
        if (d && ab.axis_index >= 0 && ab.axis_index < d->num_axes) {
            float raw = d->axis_values[ab.axis_index];
            if (static_cast<FlightAxis>(i) == FlightAxis::Throttle) {
                float v = ab.invert ? -raw : raw;
                axis_out_[i] = std::clamp((v + 1.0f) * 0.5f, 0.0f, 1.0f);
            } else {
                axis_out_[i] = apply_axis_curve(raw, ab);
            }
        } else {
            axis_out_[i] = (static_cast<FlightAxis>(i) == FlightAxis::Throttle) ? 0.5f : 0.0f;
        }
    }

    // 4. Evaluate pinky shift layer & button bindings -> key injection
    pinky_active_ = is_source_pressed(pinky_shift_);
    gameport_buttons_ = 0;

    bool any_gp_bound = false;
    for (size_t i = 0; i < button_bindings_.size(); ++i) {
        auto& bb = button_bindings_[i];
        if (i < 4 && bb.source != InputSourceType::None) any_gp_bound = true;
        bool raw_pressed = is_source_pressed(bb);
        bool active = raw_pressed && (bb.require_pinky_shift == pinky_active_ || pinky_shift_.source == InputSourceType::None);

        if (i < 4 && active) {
            gameport_buttons_ |= (1u << i);
        }

        if (active != bb.prev_pressed) {
            bb.prev_pressed = active;
            if (key_cb && bb.dos_scancode != 0) {
                key_cb(bb.dos_scancode, bb.dos_ascii, active);
            }
        }
    }

    // If user hasn't bound any of the first 4 buttons yet, pass any NEWLY pressed non-toggle HOTAS button
    // as gameport Button 1 (so Falcon's joystick calibration screen responds to any stick trigger immediately!)
    if (!any_gp_bound) {
        for (size_t di = 0; di < devices_.size() && di < raw_stuck_buttons_.size(); ++di) {
            const auto& d = devices_[di];
            auto& stuck = raw_stuck_buttons_[di];
            for (int b = 0; b < d.num_buttons && b < static_cast<int>(stuck.size()); ++b) {
                if (!d.button_states[b] && stuck[b]) {
                    stuck[b] = false;
                } else if (d.button_states[b] && !stuck[b]) {
                    gameport_buttons_ |= 1u;
                }
            }
        }
    }

    // Simulated gameport button pulse (from F12 "Simulate Joy Btn 1")
    if (sim_joy_btn_frames_ > 0) {
        gameport_buttons_ |= 1u;
        --sim_joy_btn_frames_;
    }
}

bool HotasMapper::is_axis_active(FlightAxis a) const {
    const auto& ab = axis_bindings_[static_cast<int>(a)];
    if (ab.axis_index < 0) return false;
    const HotasDeviceInfo* d = find_device_by_guid(ab.device_guid);
    return d != nullptr && ab.axis_index < d->num_axes;
}

bool HotasMapper::has_any_axis_bound() const {
    return is_axis_active(FlightAxis::Roll) || is_axis_active(FlightAxis::Pitch) ||
           is_axis_active(FlightAxis::Yaw)  || is_axis_active(FlightAxis::Throttle);
}

uint8_t HotasMapper::recommended_gameport_mode() const {
    bool has_stick = is_axis_active(FlightAxis::Roll) || is_axis_active(FlightAxis::Pitch);
    bool has_yaw = is_axis_active(FlightAxis::Yaw);
    bool has_thr = is_axis_active(FlightAxis::Throttle);
    if (!has_stick && !has_yaw && !has_thr) return 0;
    if (has_yaw && has_thr) return 4; // Roll + Pitch + Rudder + Throttle (0x0F)
    if (has_thr) return 3;            // Roll + Pitch + Throttle (0x0B)
    if (has_yaw) return 2;            // Roll + Pitch + Rudder (0x07)
    return 1;                         // Roll + Pitch (0x03)
}

uint8_t HotasMapper::gameport_axis(int idx) const {
    float v = 0.0f;
    if (idx == 0) v = axis_out_[static_cast<int>(FlightAxis::Roll)];
    else if (idx == 1) v = axis_out_[static_cast<int>(FlightAxis::Pitch)];
    else if (idx == 2) v = axis_out_[static_cast<int>(FlightAxis::Yaw)];
    // Falcon 3.0 negates Axis 3 (DX) in 0x2C583: raw 254 (+2048) = 0% IDLE, raw 127 (0) = MIL, raw 1 (-2048) = AB5!
    else if (idx == 3) v = 1.0f - axis_out_[static_cast<int>(FlightAxis::Throttle)] * 2.0f;
    int scaled = static_cast<int>((v + 1.0f) * 127.5f);
    return static_cast<uint8_t>(std::clamp(scaled, 1, 254));
}

bool HotasMapper::save_profile(const std::string& path) const {
    nlohmann::json j;
    for (int i = 0; i < static_cast<int>(FlightAxis::Count); ++i) {
        const auto& ab = axis_bindings_[i];
        j["axes"][std::to_string(i)] = {
            {"device_guid", ab.device_guid},
            {"device_name", ab.device_name},
            {"axis_index", ab.axis_index},
            {"invert", ab.invert},
            {"deadzone", ab.deadzone},
            {"curve", ab.curve}
        };
    }
    j["pinky_shift"] = {
        {"source", static_cast<int>(pinky_shift_.source)},
        {"device_guid", pinky_shift_.device_guid},
        {"device_name", pinky_shift_.device_name},
        {"index", pinky_shift_.index},
        {"hat_mask", pinky_shift_.hat_mask}
    };
    for (const auto& bb : button_bindings_) {
        j["buttons"].push_back({
            {"action_name", bb.action_name},
            {"source", static_cast<int>(bb.source)},
            {"device_guid", bb.device_guid},
            {"device_name", bb.device_name},
            {"index", bb.index},
            {"hat_mask", bb.hat_mask},
            {"require_pinky_shift", bb.require_pinky_shift}
        });
    }
    std::ofstream out(path);
    if (!out) return false;
    out << j.dump(2);
    return true;
}

bool HotasMapper::load_profile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }
    if (j.contains("axes")) {
        for (int i = 0; i < static_cast<int>(FlightAxis::Count); ++i) {
            std::string k = std::to_string(i);
            if (!j["axes"].contains(k)) continue;
            const auto& a = j["axes"][k];
            auto& ab = axis_bindings_[i];
            ab.device_guid = a.value("device_guid", "");
            ab.device_name = a.value("device_name", "");
            ab.axis_index = a.value("axis_index", -1);
            ab.invert = a.value("invert", false);
            ab.deadzone = a.value("deadzone", 0.04f);
            ab.curve = a.value("curve", 0.0f);
        }
    }
    if (j.contains("pinky_shift")) {
        const auto& p = j["pinky_shift"];
        pinky_shift_.source = static_cast<InputSourceType>(p.value("source", 0));
        pinky_shift_.device_guid = p.value("device_guid", "");
        pinky_shift_.device_name = p.value("device_name", "");
        pinky_shift_.index = p.value("index", -1);
        pinky_shift_.hat_mask = p.value("hat_mask", 0);
    }
    if (j.contains("buttons") && j["buttons"].is_array()) {
        for (const auto& jb : j["buttons"]) {
            std::string name = jb.value("action_name", "");
            for (auto& bb : button_bindings_) {
                if (bb.action_name == name) {
                    bb.source = static_cast<InputSourceType>(jb.value("source", 0));
                    bb.device_guid = jb.value("device_guid", "");
                    bb.device_name = jb.value("device_name", "");
                    bb.index = jb.value("index", -1);
                    bb.hat_mask = jb.value("hat_mask", 0);
                    bb.require_pinky_shift = jb.value("require_pinky_shift", false);
                    break;
                }
            }
        }
    }
    return true;
}

} // namespace f14

#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace f14 {

enum class FlightAxis : int {
    Roll = 0,
    Pitch = 1,
    Throttle = 2,
    Yaw = 3,
    RadarElev = 4,
    Count = 5
};

const char* flight_axis_name(FlightAxis a);

struct AxisBinding {
    std::string device_guid;
    std::string device_name;
    int axis_index = -1;
    bool invert = false;
    float deadzone = 0.04f;
    float curve = 0.0f; // 0 = linear, 0..1 = cubic softening near center
};

enum class InputSourceType {
    None = 0,
    Button = 1,
    Hat = 2
};

struct ButtonBinding {
    std::string action_name;
    uint8_t dos_scancode = 0;
    uint8_t dos_ascii = 0;
    bool shift_mod = false;
    bool ctrl_mod = false;
    bool alt_mod = false;

    InputSourceType source = InputSourceType::None;
    std::string device_guid;
    std::string device_name;
    int index = -1;     // button index or hat index
    uint8_t hat_mask = 0; // SDL_HAT_UP / DOWN / LEFT / RIGHT
    bool require_pinky_shift = false;

    bool prev_pressed = false;
};

struct HotasDeviceInfo {
    SDL_JoystickID instance_id = 0;
    SDL_Joystick* handle = nullptr;
    std::string name;
    std::string guid;
    int num_axes = 0;
    int num_buttons = 0;
    int num_hats = 0;
    std::vector<float> axis_values;     // -1.0 .. +1.0
    std::vector<bool> button_states;
    std::vector<uint8_t> hat_states;
};

class HotasMapper {
public:
    using KeyInjectCallback = std::function<void(uint8_t scancode, uint8_t ascii, bool pressed)>;

    HotasMapper();
    ~HotasMapper();

    void init();
    void shutdown();
    void refresh_devices();
    void handle_sdl_event(const SDL_Event& ev);

    // Poll all connected HOTAS devices, compute flight axes, and emit mapped key events
    void update(const KeyInjectCallback& key_cb);

    // Capture helpers for ImGui "Press to Bind"
    void start_axis_capture(FlightAxis axis);
    void start_button_capture(int binding_idx);
    void start_pinky_capture();
    void cancel_capture();
    bool is_capturing() const { return capturing_axis_ >= 0 || capturing_button_ >= 0 || capturing_pinky_; }
    int capturing_axis() const { return capturing_axis_; }
    int capturing_button() const { return capturing_button_; }
    bool capturing_pinky() const { return capturing_pinky_; }

    // Computed flight axis outputs (-1.0..+1.0, Throttle 0.0..1.0)
    float axis_value(FlightAxis a) const { return axis_out_[static_cast<int>(a)]; }

    // Gameport 4-axis emulation values (0..255) & 4 buttons bitmask
    uint8_t gameport_axis(int idx) const;
    uint8_t gameport_buttons() const { return gameport_buttons_; }
    bool is_axis_active(FlightAxis a) const; // Bound AND its USB device is currently connected!
    bool has_any_axis_bound() const;
    uint8_t recommended_gameport_mode() const; // 0=none, 1=Roll+Pitch, 2=+Yaw, 3=+Throttle, 4=All 4 axes
    void pulse_gameport_button(int frames = 8) { sim_joy_btn_frames_ = std::max(sim_joy_btn_frames_, frames); }
    bool consume_axis_bindings_changed() {
        bool c = axis_bindings_changed_;
        axis_bindings_changed_ = false;
        return c;
    }

    // Profile JSON persistence
    bool save_profile(const std::string& path) const;
    bool load_profile(const std::string& path);

    const std::vector<HotasDeviceInfo>& devices() const { return devices_; }
    std::array<AxisBinding, static_cast<int>(FlightAxis::Count)>& axis_bindings() { return axis_bindings_; }
    std::vector<ButtonBinding>& button_bindings() { return button_bindings_; }
    ButtonBinding& pinky_shift_binding() { return pinky_shift_; }
    bool pinky_shift_active() const { return pinky_active_; }

private:
    std::vector<HotasDeviceInfo> devices_;
    std::array<AxisBinding, static_cast<int>(FlightAxis::Count)> axis_bindings_{};
    std::array<float, static_cast<int>(FlightAxis::Count)> axis_out_{};
    std::vector<ButtonBinding> button_bindings_;
    ButtonBinding pinky_shift_;
    bool pinky_active_ = false;
    uint8_t gameport_buttons_ = 0;
    int sim_joy_btn_frames_ = 0;
    bool axis_bindings_changed_ = false;

    int capturing_axis_ = -1;
    int capturing_button_ = -1;
    bool capturing_pinky_ = false;
    std::vector<std::vector<float>> capture_baseline_axes_;
    std::vector<std::vector<bool>> capture_baseline_buttons_;
    std::vector<std::vector<uint8_t>> capture_baseline_hats_;
    // Tracks buttons that were already held at device connect/init so throttle toggle switches never trigger raw gameport!
    std::vector<std::vector<bool>> raw_stuck_buttons_;

    void init_default_commands();
    void snapshot_button_hat_baselines();
    const HotasDeviceInfo* find_device_by_guid(const std::string& guid) const;
    float apply_axis_curve(float raw, const AxisBinding& b) const;
    bool is_source_pressed(const ButtonBinding& b) const;
};

} // namespace f14

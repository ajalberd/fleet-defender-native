#pragma once

#include "audio/audio_engine.h"
#include "dos/dos_machine.h"
#include "dos/f14_dos_config.h"
#include "input/hotas_mapper.h"
#include "net/net_udp.h"
#include "render/modern_renderer.h"
#include "sim/f14_flight_model.h"
#include "ui/quick_keys.h"
#include "ui/win_helpers.h"

#include <functional>
#include <string>
#include <vector>

namespace f14 {

struct LauncherDecision {
    bool launch = false;
    bool quit = false;
    std::string exe_name = "F14.COM";
};

class OverlayUI {
public:
    OverlayUI(DosMachine& machine, AudioEngine& audio, HotasMapper& hotas,
              NetUdp& net, ModernRenderer& renderer, F14FlightModel& flight_model);

    void draw(const FlightTelemetry& tel);

    // Draws the full-window tabbed Startup GUI Launcher before booting the DOS executable.
    LauncherDecision draw_launcher(std::string& game_dir,
                                   std::string& rom_dir,
                                   bool& show_launcher_on_startup,
                                   const std::string& external_chart_path);

    void set_save_settings_callback(std::function<bool()> cb, std::string settings_path = "f14_settings.json") {
        save_settings_cb_ = std::move(cb);
        settings_path_ = std::move(settings_path);
    }
    void set_quit_callback(std::function<void()> cb) { quit_cb_ = std::move(cb); }
    void set_resize_window_callback(std::function<void(int, int)> cb) { resize_window_cb_ = std::move(cb); }
    void set_show_launcher_ptr(bool* ptr) { show_launcher_ptr_ = ptr; }
    void set_start_fullscreen_ptr(bool* ptr) { start_fullscreen_ptr_ = ptr; }
    void set_game_dir(const std::string& dir);
    void set_external_chart_path(std::string path) { external_chart_path_ = std::move(path); }
    void set_hotas_profile_path(std::string path) { hotas_path_ = std::move(path); }
    void sync_rom_dir_buf(const std::string& rom_dir);

    bool save_dos_config_if_dirty();

    bool visible() const { return visible_; }
    void toggle() { visible_ = !visible_; }
    void set_visible(bool v) { visible_ = v; }

private:
    DosMachine& machine_;
    AudioEngine& audio_;
    HotasMapper& hotas_;
    NetUdp& net_;
    ModernRenderer& renderer_;
    F14FlightModel& flight_model_;

    bool visible_ = false;
    bool show_hotas_ = false;
    bool show_audio_ = false;
    bool show_render_ = true;
    bool show_flight_model_ = false;
    bool show_dos_config_ = false;
    bool show_multiplayer_ = false;
    bool show_debugger_ = false;
    bool show_quick_keys_ = false;

    // Launcher state
    int launcher_selected_card_ = 0;
    char launcher_game_buf_[512]{};
    char launcher_rom_buf_[512]{};
    bool launcher_bufs_init_ = false;
    std::string launcher_status_;
    QuickKeysState quick_keys_state_;

    // Original Fleet Defender Gold DOS config store (CONFIG.F14 + JOY.F14)
    F14DosConfigStore dos_cfg_;
    bool dos_cfg_dirty_ = false;
    std::string dos_cfg_status_;

    char rom_dir_buf_[512]{};
    char join_ip_buf_[128] = "127.0.0.1";
    int net_port_ = 19940;
    std::string profile_status_;
    std::string settings_status_;
    std::string settings_path_ = "f14_settings.json";
    std::string hotas_path_ = "f14_hotas.json";
    std::string game_dir_;
    std::string external_chart_path_;
    std::vector<ManualEntry> manuals_;
    bool* show_launcher_ptr_ = nullptr;
    bool* start_fullscreen_ptr_ = nullptr;
    std::function<bool()> save_settings_cb_;
    std::function<void()> quit_cb_;
    std::function<void(int, int)> resize_window_cb_;

    // Shared inner panel builders (used by BOTH Startup Launcher tabs & F12 floating windows)
    void draw_render_contents(const FlightTelemetry* tel, bool in_launcher);
    void draw_audio_contents(bool in_launcher, std::string* launcher_rom_dir = nullptr);
    void draw_hotas_contents(bool in_launcher);
    void draw_flight_model_contents(bool in_launcher);
    void draw_dos_config_contents(bool in_launcher);

    // Floating F12 window wrappers
    void draw_hotas_window();
    void draw_audio_window();
    void draw_render_vr_window(const FlightTelemetry& tel);
    void draw_flight_model_window(const FlightTelemetry& tel);
    void draw_dos_config_window();
    void draw_multiplayer_window();
    void draw_debugger_window();
    void draw_quick_keys_window();
};

} // namespace f14

#include "audio/audio_engine.h"
#include "dos/dos_machine.h"
#include "input/hotas_mapper.h"
#include "net/net_udp.h"
#include "render/modern_renderer.h"
#include "sim/f14_flight_model.h"
#include "ui/overlay_ui.h"
#include "ui/win_helpers.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

uint8_t sdl_scancode_to_dos(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_ESCAPE: return 0x01;
        case SDL_SCANCODE_1: return 0x02; case SDL_SCANCODE_2: return 0x03;
        case SDL_SCANCODE_3: return 0x04; case SDL_SCANCODE_4: return 0x05;
        case SDL_SCANCODE_5: return 0x06; case SDL_SCANCODE_6: return 0x07;
        case SDL_SCANCODE_7: return 0x08; case SDL_SCANCODE_8: return 0x09;
        case SDL_SCANCODE_9: return 0x0A; case SDL_SCANCODE_0: return 0x0B;
        case SDL_SCANCODE_MINUS: return 0x0C; case SDL_SCANCODE_EQUALS: return 0x0D;
        case SDL_SCANCODE_BACKSPACE: return 0x0E; case SDL_SCANCODE_TAB: return 0x0F;
        case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_W: return 0x11;
        case SDL_SCANCODE_E: return 0x12; case SDL_SCANCODE_R: return 0x13;
        case SDL_SCANCODE_T: return 0x14; case SDL_SCANCODE_Y: return 0x15;
        case SDL_SCANCODE_U: return 0x16; case SDL_SCANCODE_I: return 0x17;
        case SDL_SCANCODE_O: return 0x18; case SDL_SCANCODE_P: return 0x19;
        case SDL_SCANCODE_LEFTBRACKET: return 0x1A; case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x1C;
        case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1D;
        case SDL_SCANCODE_A: return 0x1E; case SDL_SCANCODE_S: return 0x1F;
        case SDL_SCANCODE_D: return 0x20; case SDL_SCANCODE_F: return 0x21;
        case SDL_SCANCODE_G: return 0x22; case SDL_SCANCODE_H: return 0x23;
        case SDL_SCANCODE_J: return 0x24; case SDL_SCANCODE_K: return 0x25;
        case SDL_SCANCODE_L: return 0x26; case SDL_SCANCODE_SEMICOLON: return 0x27;
        case SDL_SCANCODE_APOSTROPHE: return 0x28; case SDL_SCANCODE_GRAVE: return 0x29;
        case SDL_SCANCODE_LSHIFT: return 0x2A; case SDL_SCANCODE_BACKSLASH: return 0x2B;
        case SDL_SCANCODE_Z: return 0x2C; case SDL_SCANCODE_X: return 0x2D;
        case SDL_SCANCODE_C: return 0x2E; case SDL_SCANCODE_V: return 0x2F;
        case SDL_SCANCODE_B: return 0x30; case SDL_SCANCODE_N: return 0x31;
        case SDL_SCANCODE_M: return 0x32; case SDL_SCANCODE_COMMA: return 0x33;
        case SDL_SCANCODE_PERIOD: return 0x34; case SDL_SCANCODE_SLASH: return 0x35;
        case SDL_SCANCODE_RSHIFT: return 0x36; case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38;
        case SDL_SCANCODE_SPACE: return 0x39;
        case SDL_SCANCODE_F1: return 0x3B; case SDL_SCANCODE_F2: return 0x3C;
        case SDL_SCANCODE_F3: return 0x3D; case SDL_SCANCODE_F4: return 0x3E;
        case SDL_SCANCODE_F5: return 0x3F; case SDL_SCANCODE_F6: return 0x40;
        case SDL_SCANCODE_F7: return 0x41; case SDL_SCANCODE_F8: return 0x42;
        case SDL_SCANCODE_F9: return 0x43; case SDL_SCANCODE_F10: return 0x44;
        case SDL_SCANCODE_HOME: case SDL_SCANCODE_KP_7: return 0x47;
        case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: return 0x48;
        case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_KP_9: return 0x49;
        case SDL_SCANCODE_KP_MINUS: return 0x4A;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: return 0x4B;
        case SDL_SCANCODE_KP_5: return 0x4C;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: return 0x4D;
        case SDL_SCANCODE_KP_PLUS: return 0x4E;
        case SDL_SCANCODE_END: case SDL_SCANCODE_KP_1: return 0x4F;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: return 0x50;
        case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_KP_3: return 0x51;
        case SDL_SCANCODE_INSERT: case SDL_SCANCODE_KP_0: return 0x52;
        case SDL_SCANCODE_DELETE: case SDL_SCANCODE_KP_PERIOD: return 0x53;
        default: return 0;
    }
}

uint8_t sdl_key_to_ascii(SDL_Keycode key, SDL_Keymod mod) {
    if ((mod & SDL_KMOD_ALT) != 0) return 0;
    bool ctrl  = (mod & SDL_KMOD_CTRL) != 0;
    bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    if (key >= SDLK_A && key <= SDLK_Z) {
        if (ctrl) return static_cast<uint8_t>(1 + (key - SDLK_A));
        return static_cast<uint8_t>((shift ? 'A' : 'a') + (key - SDLK_A));
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        if (!shift) return static_cast<uint8_t>('0' + (key - SDLK_0));
        static constexpr char kShiftedDigits[10] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
        return static_cast<uint8_t>(kShiftedDigits[key - SDLK_0]);
    }
    switch (key) {
        case SDLK_RETURN: case SDLK_KP_ENTER: return 13;
        case SDLK_ESCAPE: return 27;
        case SDLK_BACKSPACE: return 8;
        case SDLK_TAB: return 9;
        case SDLK_SPACE: return ' ';
        case SDLK_MINUS: return shift ? '_' : '-';
        case SDLK_EQUALS: return shift ? '+' : '=';
        case SDLK_LEFTBRACKET: return shift ? '{' : '[';
        case SDLK_RIGHTBRACKET: return shift ? '}' : ']';
        case SDLK_BACKSLASH: return shift ? '|' : '\\';
        case SDLK_SEMICOLON: return shift ? ':' : ';';
        case SDLK_APOSTROPHE: return shift ? '"' : '\'';
        case SDLK_GRAVE: return shift ? '~' : '`';
        case SDLK_KP_PLUS: return '+';
        case SDLK_KP_MINUS: return '-';
        case SDLK_COMMA: return shift ? '<' : ',';
        case SDLK_PERIOD: return shift ? '>' : '.';
        case SDLK_SLASH: return shift ? '?' : '/';
        default: return 0;
    }
}

std::string resolve_f14_dir(const fs::path& base) {
    if (base.empty()) return "";
    std::error_code ec;
    if (fs::exists(base / "F14.EXE", ec) || fs::exists(base / "F14.COM", ec)) return fs::absolute(base, ec).string();
    if (fs::exists(base / "game_data" / "F14.EXE", ec) || fs::exists(base / "game_data" / "F14.COM", ec)) {
        return fs::absolute(base / "game_data", ec).string();
    }
    return "";
}

std::string find_game_dir(const std::string& explicit_dir, int argc, char** argv) {
    if (!explicit_dir.empty()) {
        std::string r = resolve_f14_dir(explicit_dir);
        if (!r.empty()) return r;
    }
    if (argc > 1 && argv[1][0] != '-') {
        std::string r = resolve_f14_dir(argv[1]);
        if (!r.empty()) return r;
    }
    if (const char* env_dir = std::getenv("F14_GAME_DIR"); env_dir && env_dir[0]) {
        std::string r = resolve_f14_dir(env_dir);
        if (!r.empty()) return r;
    }
    std::string det = f14::autodetect_f14_install_dir();
    if (!det.empty()) return det;
    return "";
}

std::string find_rom_dir(const std::string& explicit_rom_dir) {
    if (!explicit_rom_dir.empty()) return explicit_rom_dir;
    return f14::autodetect_rom_dir();
}

fs::path get_exe_dir() {
    std::error_code ec;
#ifdef _WIN32
    char buf[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH) > 0) {
        return fs::path(buf).parent_path();
    }
#endif
    return fs::current_path(ec);
}

fs::path resolve_config_dir(const std::string& game_dir) {
    std::error_code ec;
    if (!game_dir.empty()) {
        fs::path gd = fs::weakly_canonical(fs::path(game_dir), ec);
        if (!ec && !gd.empty()) {
            std::string leaf = gd.filename().string();
            std::transform(leaf.begin(), leaf.end(), leaf.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (leaf == "game_data" && gd.has_parent_path()) {
                return gd.parent_path();
            }
            if (fs::exists(gd / "F14.COM", ec) || fs::exists(gd / "F14.EXE", ec)) {
                return gd;
            }
        }
    }
    return get_exe_dir();
}

std::string find_external_chart_path(const fs::path& cfg_dir, const std::string& game_dir) {
    std::error_code ec;
    fs::path exe_dir = get_exe_dir();
    std::vector<fs::path> search_roots = {
        cfg_dir,
        exe_dir,
        exe_dir.parent_path(),
        exe_dir.parent_path().parent_path(),
    };
    if (!game_dir.empty()) search_roots.push_back(fs::path(game_dir));

    for (const auto& r : search_roots) {
        for (const char* rel : {"docs/F14_Quickstart_Keys.svg", "docs/F14_Quickstart_Keys.png"}) {
            fs::path p = r / rel;
            if (fs::exists(p, ec)) return fs::absolute(p, ec).string();
        }
    }
    return "";
}

bool load_host_settings(const std::string& path,
                        const std::string& explicit_game_dir,
                        const std::string& explicit_rom_dir,
                        std::string& game_dir,
                        f14::AudioEngine& audio,
                        f14::ModernRenderer& renderer,
                        f14::F14FlightModel& flight_model,
                        f14::DosMachine& machine,
                        bool& show_launcher_on_startup,
                        int& init_win_w,
                        int& init_win_h) {
    std::ifstream in(path);
    if (!in) return false;
    try {
        nlohmann::json j;
        in >> j;
        if (j.contains("audio") && j["audio"].is_object()) {
            const auto& a = j["audio"];
            auto& ac = audio.config();
            if (explicit_rom_dir.empty() && a.contains("cm32l_rom_dir") && a["cm32l_rom_dir"].is_string()) {
                std::string saved_rom = a["cm32l_rom_dir"].get<std::string>();
                if (!saved_rom.empty()) ac.cm32l_rom_dir = saved_rom;
            }
            if (a.contains("master_volume")) ac.master_volume = std::clamp(a["master_volume"].get<float>(), 0.0f, 1.5f);
            if (a.contains("mt32_volume")) ac.mt32_volume = std::clamp(a["mt32_volume"].get<float>(), 0.0f, 1.5f);
            if (a.contains("sb16_sfx_volume")) ac.sb16_sfx_volume = std::clamp(a["sb16_sfx_volume"].get<float>(), 0.0f, 1.5f);
            if (a.contains("engine_bus_volume")) ac.engine_bus_volume = std::clamp(a["engine_bus_volume"].get<float>(), 0.0f, 1.5f);
            if (a.contains("weapons_bus_volume")) ac.weapons_bus_volume = std::clamp(a["weapons_bus_volume"].get<float>(), 0.0f, 1.5f);
        }
        if (j.contains("render") && j["render"].is_object()) {
            const auto& r = j["render"];
            auto& vr = renderer.vr_config();
            if (r.contains("mode")) {
                int m = std::clamp(r["mode"].get<int>(), 0, 3);
                renderer.mode() = static_cast<f14::RenderMode>(m);
            }
            if (r.contains("hybrid_fill_widescreen")) vr.hybrid_fill_widescreen = r["hybrid_fill_widescreen"].get<bool>();
            if (r.contains("widescreen_3d_cockpit")) vr.widescreen_3d_cockpit = r["widescreen_3d_cockpit"].get<bool>();
            if (r.contains("enhanced_3d_padlock")) vr.enhanced_3d_padlock = r["enhanced_3d_padlock"].get<bool>();
            if (r.contains("show_clouds")) vr.show_clouds = r["show_clouds"].get<bool>();
            if (r.contains("horizon_fog")) vr.horizon_fog = r["horizon_fog"].get<bool>();
            if (r.contains("wireframe_terrain")) vr.wireframe_terrain = r["wireframe_terrain"].get<bool>();
            if (r.contains("smart_scaling_mode")) {
                vr.smart_scaling_mode = static_cast<f14::SmartScalingMode>(std::clamp(r["smart_scaling_mode"].get<int>(), 0, 3));
            }
            if (r.contains("fov_deg")) vr.fov_deg = std::clamp(r["fov_deg"].get<float>(), 30.0f, 75.0f);
            if (r.contains("widescreen_fov_deg")) vr.widescreen_fov_deg = std::clamp(r["widescreen_fov_deg"].get<float>(), 38.0f, 70.0f);
            if (r.contains("aircraft_scale")) vr.aircraft_scale = std::clamp(r["aircraft_scale"].get<float>(), 0.5f, 2.5f);
            if (r.contains("lighting_mode")) {
                vr.lighting_mode = static_cast<f14::LightingMode>(std::clamp(r["lighting_mode"].get<int>(), 0, 3));
            }
        }
        if (j.contains("flight_model") && j["flight_model"].is_object()) {
            const auto& fm = j["flight_model"];
            auto& fc = flight_model.config();
            if (fm.contains("enabled")) fc.enabled = fm["enabled"].get<bool>();
            if (fm.contains("engine_f110_ge400")) fc.engine_f110_ge400 = fm["engine_f110_ge400"].get<bool>();
            if (fm.contains("auto_wing_sweep")) fc.auto_wing_sweep = fm["auto_wing_sweep"].get<bool>();
            if (fm.contains("manual_sweep_deg")) fc.manual_sweep_deg = std::clamp(fm["manual_sweep_deg"].get<float>(), 20.0f, 68.0f);
            if (fm.contains("aoa_limiter")) fc.aoa_limiter = fm["aoa_limiter"].get<bool>();
        }
        if (j.contains("host") && j["host"].is_object()) {
            const auto& h = j["host"];
            if (explicit_game_dir.empty() && h.contains("game_dir") && h["game_dir"].is_string()) {
                std::string saved_gd = h["game_dir"].get<std::string>();
                std::string resolved = resolve_f14_dir(saved_gd);
                if (!resolved.empty()) game_dir = resolved;
            }
            if (h.contains("show_launcher_on_startup")) show_launcher_on_startup = h["show_launcher_on_startup"].get<bool>();
            if (h.contains("skip_intro")) machine.set_skip_intro(h["skip_intro"].get<bool>());
            if (h.contains("flight_input_mode")) {
                int im = std::clamp(h["flight_input_mode"].get<int>(), 0, 2);
                machine.set_flight_input_mode(static_cast<f14::FlightInputMode>(im));
            }
            if (h.contains("window_w")) init_win_w = std::clamp(h["window_w"].get<int>(), 640, 7680);
            if (h.contains("window_h")) init_win_h = std::clamp(h["window_h"].get<int>(), 480, 4320);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool save_host_settings(const std::string& path,
                        const std::string& game_dir,
                        const f14::AudioEngine& audio,
                        const f14::ModernRenderer& renderer,
                        const f14::F14FlightModel& flight_model,
                        const f14::DosMachine& machine,
                        bool show_launcher_on_startup,
                        int win_w,
                        int win_h) {
    try {
        nlohmann::json j;
        const auto& ac = audio.config();
        j["audio"] = {
            {"cm32l_rom_dir", ac.cm32l_rom_dir},
            {"master_volume", ac.master_volume},
            {"mt32_volume", ac.mt32_volume},
            {"sb16_sfx_volume", ac.sb16_sfx_volume},
            {"engine_bus_volume", ac.engine_bus_volume},
            {"weapons_bus_volume", ac.weapons_bus_volume},
        };
        const auto& vr = renderer.vr_config();
        j["render"] = {
            {"mode", static_cast<int>(renderer.mode())},
            {"hybrid_fill_widescreen", vr.hybrid_fill_widescreen},
            {"widescreen_3d_cockpit", vr.widescreen_3d_cockpit},
            {"enhanced_3d_padlock", vr.enhanced_3d_padlock},
            {"show_clouds", vr.show_clouds},
            {"horizon_fog", vr.horizon_fog},
            {"wireframe_terrain", vr.wireframe_terrain},
            {"smart_scaling_mode", static_cast<int>(vr.smart_scaling_mode)},
            {"fov_deg", vr.fov_deg},
            {"widescreen_fov_deg", vr.widescreen_fov_deg},
            {"aircraft_scale", vr.aircraft_scale},
            {"lighting_mode", static_cast<int>(vr.lighting_mode)},
        };
        const auto& fc = flight_model.config();
        j["flight_model"] = {
            {"enabled", fc.enabled},
            {"engine_f110_ge400", fc.engine_f110_ge400},
            {"auto_wing_sweep", fc.auto_wing_sweep},
            {"manual_sweep_deg", fc.manual_sweep_deg},
            {"aoa_limiter", fc.aoa_limiter},
        };
        j["host"] = {
            {"game_dir", game_dir},
            {"show_launcher_on_startup", show_launcher_on_startup},
            {"skip_intro", machine.skip_intro()},
            {"flight_input_mode", static_cast<int>(machine.flight_input_mode())},
            {"window_w", win_w},
            {"window_h", win_h},
        };
        std::ofstream out(path);
        if (!out) return false;
        out << j.dump(2) << "\n";
        return out.good();
    } catch (...) {
        return false;
    }
}

void write_ppm_rgba(const std::string& path, const uint32_t* rgba, int w, int h) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return;
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int i = 0; i < w * h; ++i) {
        uint32_t px = rgba[i];
        uint8_t rgb[3] = {
            static_cast<uint8_t>(px & 0xFF),
            static_cast<uint8_t>((px >> 8) & 0xFF),
            static_cast<uint8_t>((px >> 16) & 0xFF)
        };
        out.write(reinterpret_cast<const char*>(rgb), 3);
    }
}

} // namespace

int main(int argc, char** argv) {
    bool headless_test = false;
    bool test_fastboot = false;
    bool trace_audio = false;
    bool force_launcher = false;
    bool force_no_launcher = false;
    bool explicit_exe = false;
    uint64_t headless_steps = 0;
    std::string dump_ram_path;
    std::string dump_vga_path;
    std::string explicit_game_dir;
    std::string explicit_rom_dir;
    std::string exe_name = "F14.COM";

    struct ScriptEvent {
        uint64_t at_step = 0;
        std::string kind;
        int a = 0, b = 0;
        bool fired = false;
    };
    std::vector<ScriptEvent> script_events;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless" && i + 1 < argc) { headless_test = true; headless_steps = std::stoull(argv[++i]); }
        else if (arg == "--test-fastboot") { headless_test = true; test_fastboot = true; }
        else if (arg == "--trace-audio") { trace_audio = true; }
        else if (arg == "--launcher") { force_launcher = true; }
        else if (arg == "--no-launcher" || arg == "--play") { force_no_launcher = true; }
        else if (arg == "--game-dir" && i + 1 < argc) { explicit_game_dir = argv[++i]; }
        else if (arg == "--rom-dir" && i + 1 < argc) { explicit_rom_dir = argv[++i]; }
        else if (arg == "--exe" && i + 1 < argc) { exe_name = argv[++i]; explicit_exe = true; }
        else if (arg == "--dump-ram" && i + 1 < argc) { dump_ram_path = argv[++i]; }
        else if (arg == "--dump-vga" && i + 1 < argc) { dump_vga_path = argv[++i]; }
        else if (arg == "--key" && i + 3 < argc) {
            script_events.push_back({std::stoull(argv[i + 1]), "key", std::stoi(argv[i + 2], nullptr, 0), std::stoi(argv[i + 3], nullptr, 0), false});
            i += 3;
        } else if (arg == "--click" && i + 3 < argc) {
            script_events.push_back({std::stoull(argv[i + 1]), "click", std::stoi(argv[i + 2]), std::stoi(argv[i + 3]), false});
            i += 3;
        }
    }

#ifdef _WIN32
    // Attach to parent console only for headless/CLI/trace invocations; double-clicking stays 100% GUI (zero console flash).
    if (headless_test || trace_audio) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            std::freopen("CONOUT$", "w", stdout);
            std::freopen("CONOUT$", "w", stderr);
        }
    }
#endif

    std::string game_dir = find_game_dir(explicit_game_dir, argc, argv);
    std::string rom_dir = find_rom_dir(explicit_rom_dir);

    if (headless_test) {
        if (game_dir.empty()) game_dir = ".";
        f14::AudioEngine audio;
        audio.config().cm32l_rom_dir = rom_dir;
        audio.set_trace_audio(trace_audio);
        if (trace_audio) audio.load_mt32_roms(rom_dir);
        f14::HotasMapper hotas;
        f14::NetUdp net;
        f14::DosMachine machine(audio, hotas, net);
        machine.set_skip_intro(true);
        if (!machine.load_exe(game_dir, exe_name)) {
            std::printf("Failed to load %s from %s\n", exe_name.c_str(), game_dir.c_str());
            return 1;
        }
        if (test_fastboot) {
            auto t0 = std::chrono::steady_clock::now();
            int frames = 0;
            while (machine.is_fast_booting() && frames < 600 && !machine.snapshot().exited) {
                machine.step_realtime(1.0 / 60.0);
                frames++;
            }
            for (int f = 0; f < 5; ++f) machine.step_realtime(1.0 / 60.0);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("Fast-boot finished in %.1f ms (%d host frames, fast_boot_active=%d)\n",
                        ms, frames, machine.is_fast_booting() ? 1 : 0);
        }
        if (headless_steps > 0) {
            uint64_t remaining = headless_steps;
            while (remaining > 0 && !machine.snapshot().exited) {
                uint64_t chunk = remaining > 50000 ? 50000 : remaining;
                uint64_t ran = machine.step(chunk);
                machine.on_frame_tick();
                uint64_t cur = machine.snapshot().total_insns;
                for (auto& se : script_events) {
                    if (!se.fired && cur >= se.at_step) {
                        se.fired = true;
                        if (se.kind == "key") {
                            machine.push_key(static_cast<uint8_t>(se.a), static_cast<uint8_t>(se.b), true);
                            machine.step(80000);
                            machine.push_key(static_cast<uint8_t>(se.a), static_cast<uint8_t>(se.b), false);
                        } else if (se.kind == "click") {
                            machine.set_mouse_state(se.a, se.b, 1);
                            machine.step(60000);
                            machine.set_mouse_state(se.a, se.b, 0);
                        }
                    }
                }
                if (ran == 0) break;
                remaining -= ran;
            }
        }
        auto snap = machine.snapshot();
        auto tel = machine.read_flight_telemetry();
        std::printf("Headless done: proc=%s depth=%d insns=%llu CS:IP=%04X:%04X VGA=0x%02X EMS=%d/%d exited=%d MIDI=%llu SysEx=%llu SB_DAC=%llu\n",
                    snap.active_proc.c_str(), snap.proc_depth,
                    static_cast<unsigned long long>(snap.total_insns),
                    snap.cs, snap.ip, machine.vga().mode(),
                    snap.ems_pages_used, snap.ems_pages_total, snap.exited ? 1 : 0,
                    static_cast<unsigned long long>(audio.midi_bytes_received()),
                    static_cast<unsigned long long>(audio.sysex_messages_received()),
                    static_cast<unsigned long long>(audio.sb_dac_samples_received()));
        if (tel.in_flight) {
            std::printf("Live F-14 Telemetry: pos=(%.0f, %.0f) alt=%.0f ft (AGL %.0f) HDG=%.1f P=%.1f R=%.1f KIAS=%.0f M=%.2f Sweep=%.0fdeg View=0x%02X Entities=%zu Missiles=%zu\n",
                        tel.world_x, tel.world_y, tel.altitude_ft, tel.agl_ft,
                        tel.yaw_deg, tel.pitch_deg, tel.roll_deg, tel.airspeed_kts, tel.mach, tel.wing_sweep_deg,
                        tel.view_mode, tel.entities.size(), tel.missiles.size());
        }
        std::printf("--- Recent DOS Log ---\n");
        const auto& logs = machine.log_lines();
        size_t start_i = logs.size() > 30 ? logs.size() - 30 : 0;
        for (size_t i = start_i; i < logs.size(); ++i) std::printf("  %s\n", logs[i].c_str());

        if (!dump_vga_path.empty()) {
            std::vector<uint32_t> rgba(320 * 400, 0);
            machine.render_vga_rgba_400(rgba.data());
            write_ppm_rgba(dump_vga_path, rgba.data(), 320, 400);
            std::printf("Wrote VGA dump to %s\n", dump_vga_path.c_str());
        }
        if (!dump_ram_path.empty()) {
            std::ofstream out(dump_ram_path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(machine.raw_memory()), 0x100000);
            std::printf("Wrote 1MB RAM dump to %s\n", dump_ram_path.c_str());
        }
        std::fflush(stdout);
        return 0;
    }

    // Interactive SDL3 + OpenGL + Dear ImGui Host
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    fs::path cfg_dir = resolve_config_dir(game_dir);
    std::string settings_path = (cfg_dir / "f14_settings.json").string();
    std::string hotas_path = (cfg_dir / "f14_hotas.json").string();

    f14::AudioEngine audio;
    audio.config().cm32l_rom_dir = rom_dir;
    audio.set_trace_audio(trace_audio);

    f14::HotasMapper hotas;
    f14::NetUdp net;
    f14::DosMachine machine(audio, hotas, net);
    f14::ModernRenderer renderer;
    renderer.mode() = f14::RenderMode::HybridCockpit;
    f14::F14FlightModel flight_model;

    bool show_launcher_on_startup = true;
    int init_win_w = 1280, init_win_h = 720;
    load_host_settings(settings_path, explicit_game_dir, explicit_rom_dir, game_dir,
                       audio, renderer, flight_model, machine, show_launcher_on_startup,
                       init_win_w, init_win_h);
    rom_dir = audio.config().cm32l_rom_dir;

    SDL_Window* window = SDL_CreateWindow("Fleet Defender Gold — Native Host (F11 Render | F12 Settings)",
                                          init_win_w, init_win_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // Keep game folder clean — no imgui.ini litter
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 120");

    f14::OverlayUI ui(machine, audio, hotas, net, renderer, flight_model);
    std::string chart_path = find_external_chart_path(cfg_dir, game_dir);
    ui.set_external_chart_path(chart_path);
    ui.set_show_launcher_ptr(&show_launcher_on_startup);
    ui.set_game_dir(game_dir);
    ui.sync_rom_dir_buf(rom_dir);

    bool running = true;
    ui.set_quit_callback([&]() { running = false; });

    auto do_save_settings = [&]() {
        int cur_w = init_win_w, cur_h = init_win_h;
        SDL_GetWindowSize(window, &cur_w, &cur_h);
        return save_host_settings(settings_path, game_dir, audio, renderer, flight_model, machine,
                                  show_launcher_on_startup, cur_w, cur_h);
    };
    ui.set_save_settings_callback(do_save_settings, settings_path);

    // Decide whether to show the Startup GUI Launcher
    bool has_valid_game = !resolve_f14_dir(game_dir).empty();
    bool run_launcher = force_launcher || (!force_no_launcher && !explicit_exe && (show_launcher_on_startup || !has_valid_game));

    if (run_launcher) {
        bool in_launcher = true;
        while (in_launcher && running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                ImGui_ImplSDL3_ProcessEvent(&ev);
                if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    running = false;
                    in_launcher = false;
                }
            }
            if (!running) break;

            int lw = 1280, lh = 720;
            SDL_GetWindowSize(window, &lw, &lh);
            glViewport(0, 0, lw, lh);
            glClearColor(0.055f, 0.085f, 0.135f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            auto dec = ui.draw_launcher(game_dir, rom_dir, show_launcher_on_startup, chart_path);

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(window);

            if (dec.quit) {
                running = false;
                in_launcher = false;
            } else if (dec.launch) {
                exe_name = dec.exe_name;
                in_launcher = false;
            }
        }
        // Re-resolve config paths if user changed game_dir inside launcher
        if (!game_dir.empty()) {
            std::string resolved = resolve_f14_dir(game_dir);
            if (!resolved.empty()) game_dir = resolved;
        }
        cfg_dir = resolve_config_dir(game_dir);
        settings_path = (cfg_dir / "f14_settings.json").string();
        hotas_path = (cfg_dir / "f14_hotas.json").string();
        chart_path = find_external_chart_path(cfg_dir, game_dir);
        ui.set_external_chart_path(chart_path);
        ui.set_game_dir(game_dir);
        ui.set_hotas_profile_path(hotas_path);
        ui.set_save_settings_callback(do_save_settings, settings_path);
        audio.config().cm32l_rom_dir = rom_dir;
        ui.sync_rom_dir_buf(rom_dir);
        do_save_settings();
    }

    if (running) {
        audio.init();
        hotas.init();
        ui.set_hotas_profile_path(hotas_path);
        if (!hotas.load_profile(hotas_path)) {
            hotas.load_profile("f14_hotas.json");
        }

        if (!machine.load_exe(game_dir, exe_name)) {
            std::fprintf(stderr, "Failed to load %s from %s\n", exe_name.c_str(), game_dir.c_str());
            running = false;
        } else {
            renderer.load_3dz_directory(game_dir);
        }
    }

    GLuint vga_tex = 0, cockpit_tex = 0, hud_tex = 0;
    glGenTextures(1, &vga_tex);
    glGenTextures(1, &cockpit_tex);
    glGenTextures(1, &hud_tex);
    for (GLuint t : {vga_tex, cockpit_tex, hud_tex}) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    std::vector<uint32_t> vga_rgba(320 * 400, 0);
    std::vector<uint32_t> cockpit_rgba(320 * 200, 0);
    std::vector<uint32_t> hud_rgba(320 * 200, 0);

    bool rmb_freelook = false;
    bool prev_in_flight = false;
    bool cockpit_harvested = false;
    bool is_fullscreen = false;
    bool cursor_hidden = false;
    std::string last_loaded_wld;
    auto last_t = std::chrono::steady_clock::now();

    while (running) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_t).count();
        last_t = now;
        dt = std::clamp(dt, 0.001, 0.05);

        int win_w = 1280, win_h = 720;
        SDL_GetWindowSize(window, &win_w, &win_h);

        auto pre_tel = machine.read_flight_telemetry();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            hotas.handle_sdl_event(ev);

            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
                bool pressed = (ev.type == SDL_EVENT_KEY_DOWN);
                if (pressed && !ev.key.repeat && ev.key.scancode == SDL_SCANCODE_F12) {
                    ui.toggle();
                    continue;
                }
                if (pressed && !ev.key.repeat &&
                    (ev.key.scancode == SDL_SCANCODE_RETURN || ev.key.scancode == SDL_SCANCODE_KP_ENTER) &&
                    ((ev.key.mod & SDL_KMOD_ALT) != 0)) {
                    is_fullscreen = !is_fullscreen;
                    SDL_SetWindowFullscreen(window, is_fullscreen);
                    continue;
                }
                if (pressed && !ev.key.repeat && ev.key.scancode == SDL_SCANCODE_F11) {
                    int m = (static_cast<int>(renderer.mode()) + 1) % 4;
                    renderer.mode() = static_cast<f14::RenderMode>(m);
                    continue;
                }
                if (pressed && !ev.key.repeat && ev.key.scancode == SDL_SCANCODE_F1) {
                    renderer.vr_config().head_yaw_deg = 0.0f;
                    renderer.vr_config().head_pitch_deg = 0.0f;
                }
                if (!ImGui::GetIO().WantCaptureKeyboard) {
                    uint8_t dos_sc = sdl_scancode_to_dos(ev.key.scancode);
                    uint8_t ascii = sdl_key_to_ascii(ev.key.key, ev.key.mod);
                    if (dos_sc != 0) machine.push_key(dos_sc, ascii, pressed);
                    if (pressed && pre_tel.in_flight) {
                        if (ev.key.scancode == SDL_SCANCODE_C) { renderer.spawn_chaff(pre_tel); audio.trigger_chaff_sfx(); }
                        else if (ev.key.scancode == SDL_SCANCODE_F) { renderer.spawn_flare(pre_tel); audio.trigger_flare_sfx(); }
                        else if (ev.key.scancode == SDL_SCANCODE_SPACE) { renderer.spawn_gun_tracer(pre_tel); }
                    }
                }
            } else if (!ImGui::GetIO().WantCaptureMouse &&
                       (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                        ev.type == SDL_EVENT_MOUSE_BUTTON_UP)) {
                if (pre_tel.in_flight && renderer.mode() != f14::RenderMode::ClassicVga) {
                    if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_RIGHT) rmb_freelook = true;
                    if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_RIGHT) rmb_freelook = false;
                    if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_MIDDLE) {
                        renderer.vr_config().head_yaw_deg = 0.0f;
                        renderer.vr_config().head_pitch_deg = 0.0f;
                    }
                    if (ev.type == SDL_EVENT_MOUSE_MOTION && rmb_freelook) {
                        renderer.vr_config().head_yaw_deg = std::clamp(renderer.vr_config().head_yaw_deg + ev.motion.xrel * 0.25f, -155.0f, 155.0f);
                        renderer.vr_config().head_pitch_deg = std::clamp(renderer.vr_config().head_pitch_deg - ev.motion.yrel * 0.25f, -45.0f, 82.0f);
                    }
                } else {
                    rmb_freelook = false;
                }
                if (!rmb_freelook) {
                    float mx = 0.0f, my = 0.0f;
                    SDL_MouseButtonFlags btns = SDL_GetMouseState(&mx, &my);
                    float target_aspect = 4.0f / 3.0f;
                    float win_aspect = static_cast<float>(win_w) / static_cast<float>(std::max(1, win_h));
                    float rw = static_cast<float>(win_w), rh = static_cast<float>(win_h), rx = 0.0f, ry = 0.0f;
                    if (win_aspect > target_aspect) { rw = rh * target_aspect; rx = (win_w - rw) * 0.5f; }
                    else { rh = rw / target_aspect; ry = (win_h - rh) * 0.5f; }
                    int vx = static_cast<int>(((mx - rx) / std::max(1.0f, rw)) * 320.0f);
                    int vy = static_cast<int>(((my - ry) / std::max(1.0f, rh)) * 200.0f);
                    uint8_t mask = ((btns & SDL_BUTTON_LMASK) ? 1 : 0) | ((btns & SDL_BUTTON_RMASK) ? 2 : 0);
                    machine.set_mouse_state(vx, vy, mask);
                }
            }
        }

        hotas.update([&](uint8_t sc, uint8_t asc, bool down) { machine.push_key(sc, asc, down); });
        if (hotas.consume_axis_bindings_changed()) {
            machine.apply_auto_joystick_calibration(true);
        }

        machine.step_realtime(dt);

        if (!machine.detected_world_file().empty() && machine.detected_world_file() != last_loaded_wld) {
            last_loaded_wld = machine.detected_world_file();
            renderer.load_world_wld(last_loaded_wld);
        }

        auto tel = machine.read_flight_telemetry();

        bool want_hide_cursor = tel.in_flight && !ui.visible();
        if (want_hide_cursor != cursor_hidden) {
            if (want_hide_cursor) SDL_HideCursor(); else SDL_ShowCursor();
            cursor_hidden = want_hide_cursor;
        }

        if (tel.in_flight && flight_model.config().enabled) {
            if (!flight_model.is_initialized() || !prev_in_flight) {
                float wlbs = 58000.0f, thr01 = 0.65f;
                machine.read_player_fm_extras(wlbs, thr01);
                flight_model.sync_from_game(tel.world_x, tel.world_y, tel.altitude_ft, tel.agl_ft,
                                            tel.yaw_deg, tel.pitch_deg, tel.roll_deg, tel.airspeed_kts, wlbs, thr01);
            }
            f14::F14ControlInput fm_in;
            float hr = hotas.is_axis_active(f14::FlightAxis::Roll) ? hotas.axis_value(f14::FlightAxis::Roll) : 0.0f;
            float hp = hotas.is_axis_active(f14::FlightAxis::Pitch) ? hotas.axis_value(f14::FlightAxis::Pitch) : 0.0f;
            fm_in.stick_roll = (std::abs(hr) > 0.06f) ? hr : machine.kbd_stick_x();
            fm_in.stick_pitch = -((std::abs(hp) > 0.06f) ? hp : machine.kbd_stick_y());
            fm_in.rudder = hotas.is_axis_active(f14::FlightAxis::Yaw) ? hotas.axis_value(f14::FlightAxis::Yaw) : 0.0f;
            fm_in.has_analog_throttle = hotas.is_axis_active(f14::FlightAxis::Throttle);
            fm_in.throttle = fm_in.has_analog_throttle ? hotas.axis_value(f14::FlightAxis::Throttle) : tel.throttle_01;
            flight_model.step(dt, fm_in);
            machine.write_player_flight_state(flight_model.state(), tel.view_mode == 0, fm_in.has_analog_throttle);
            tel = machine.read_flight_telemetry();
        } else if (!tel.in_flight) {
            flight_model.reset();
            cockpit_harvested = false;
        }

        if (tel.in_flight) {
            if (!cockpit_harvested && tel.view_mode == 0 && !tel.back_seat_rio) {
                cockpit_harvested = machine.harvest_cockpit_views(renderer.cockpit_3d());
            } else if ((tel.view_mode == 0 && tel.back_seat_rio && !renderer.cockpit_3d().has_view(f14::Cockpit3D::ViewRioForward)) ||
                       (tel.view_mode == 1 && !renderer.cockpit_3d().has_view(f14::Cockpit3D::ViewLeft)) ||
                       (tel.view_mode == 2 && !renderer.cockpit_3d().has_view(f14::Cockpit3D::ViewRight)) ||
                       (tel.view_mode == 4 && !renderer.cockpit_3d().has_view(f14::Cockpit3D::ViewRearLeft)) ||
                       (tel.view_mode == 5 && !renderer.cockpit_3d().has_view(f14::Cockpit3D::ViewRearRight))) {
                machine.harvest_cockpit_views(renderer.cockpit_3d());
            }
        }

        if (tel.in_flight != prev_in_flight) {
            audio.set_in_flight(tel.in_flight);
            prev_in_flight = tel.in_flight;
        }
        {
            f14::FlightAudioTelemetry atel{};
            atel.in_flight = tel.in_flight;
            atel.paused = machine.paused();
            atel.is_external_cam = tel.is_external_cam;
            atel.airspeed_kts = tel.airspeed_kts;
            machine.read_flight_audio_extras(atel.rpm_pct, atel.throttle_01, atel.ab_stage, atel.g_load);
            audio.update_flight_telemetry(atel);
            audio.update();
        }
        renderer.set_vga_palette(machine.vga().dac_palette());
        machine.render_vga_rgba_400(vga_rgba.data());
        machine.vga().render_overlay_layers_200(cockpit_rgba.data(), hud_rgba.data(), &tel.world_y_min, &tel.world_y_max);

        glBindTexture(GL_TEXTURE_2D, vga_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 320, 400, 0, GL_RGBA, GL_UNSIGNED_BYTE, vga_rgba.data());
        glBindTexture(GL_TEXTURE_2D, cockpit_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 320, 200, 0, GL_RGBA, GL_UNSIGNED_BYTE, cockpit_rgba.data());
        glBindTexture(GL_TEXTURE_2D, hud_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 320, 200, 0, GL_RGBA, GL_UNSIGNED_BYTE, hud_rgba.data());

        renderer.render_frame(win_w, win_h, vga_tex, cockpit_tex, hud_tex, tel, static_cast<float>(dt));

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ui.draw(tel);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
        if (machine.snapshot().exited) running = false;
    }

    do_save_settings();

    hotas.shutdown();
    audio.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

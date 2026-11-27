#include "ui/overlay_ui.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace f14 {

namespace {

bool file_exists_ci(const std::string& dir, const char* filename) {
    if (dir.empty()) return false;
    std::error_code ec;
    return fs::exists(fs::path(dir) / filename, ec);
}

bool dir_has_roms(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    fs::path d(dir);
    if (!fs::exists(d, ec) || !fs::is_directory(d, ec)) return false;
    bool ctrl = fs::exists(d / "CM32L_CONTROL.ROM", ec) || fs::exists(d / "cm32l_control.rom", ec) ||
                fs::exists(d / "MT32_CONTROL.ROM", ec)  || fs::exists(d / "mt32_control.rom", ec);
    bool pcm  = fs::exists(d / "CM32L_PCM.ROM", ec)     || fs::exists(d / "cm32l_pcm.rom", ec) ||
                fs::exists(d / "MT32_PCM.ROM", ec)      || fs::exists(d / "mt32_pcm.rom", ec);
    return ctrl && pcm;
}

} // namespace

OverlayUI::OverlayUI(DosMachine& machine, AudioEngine& audio, HotasMapper& hotas,
                     NetUdp& net, ModernRenderer& renderer, F14FlightModel& flight_model)
    : machine_(machine), audio_(audio), hotas_(hotas), net_(net),
      renderer_(renderer), flight_model_(flight_model) {
    sync_rom_dir_buf(audio_.config().cm32l_rom_dir);
}

void OverlayUI::sync_rom_dir_buf(const std::string& rom_dir) {
    std::snprintf(rom_dir_buf_, sizeof(rom_dir_buf_), "%s", rom_dir.c_str());
    std::snprintf(launcher_rom_buf_, sizeof(launcher_rom_buf_), "%s", rom_dir.c_str());
}

void OverlayUI::set_game_dir(const std::string& dir) {
    game_dir_ = dir;
    manuals_ = discover_local_manuals(game_dir_);
    std::snprintf(launcher_game_buf_, sizeof(launcher_game_buf_), "%s", game_dir_.c_str());
}

LauncherDecision OverlayUI::draw_launcher(std::string& game_dir,
                                          std::string& rom_dir,
                                          bool& show_launcher_on_startup,
                                          const std::string& external_chart_path) {
    LauncherDecision dec;
    if (!launcher_bufs_init_) {
        std::snprintf(launcher_game_buf_, sizeof(launcher_game_buf_), "%s", game_dir.c_str());
        std::snprintf(launcher_rom_buf_, sizeof(launcher_rom_buf_), "%s", rom_dir.c_str());
        manuals_ = discover_local_manuals(game_dir);
        launcher_bufs_init_ = true;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.085f, 0.135f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##F14NativeLauncher", nullptr, wflags);

    bool game_ready = file_exists_ci(game_dir, "F14.COM") || file_exists_ci(game_dir, "F14.EXE");
    bool roms_ready = dir_has_roms(rom_dir);

    // Header bar
    ImGui::TextColored(ImVec4(0.35f, 0.88f, 1.0f, 1.0f), "FLEET DEFENDER GOLD (F-14 TOMCAT) — NATIVE HOST v0.1.0");
    ImGui::SameLine();
    float badge_x = std::max(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - 340.0f);
    ImGui::SetCursorPosX(badge_x);
    if (game_ready) {
        ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "[GAME READY]");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.35f, 1.0f), "[SELECT GAME FOLDER]");
    }
    ImGui::SameLine();
    if (roms_ready) {
        ImGui::TextColored(ImVec4(0.35f, 0.90f, 1.0f, 1.0f), "[CM-32L / MT-32 OK]");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.32f, 1.0f), "[OPL FALLBACK]");
    }

    ImGui::TextColored(ImVec4(0.68f, 0.78f, 0.88f, 1.0f),
                       "Standalone SDL3 + OpenGL HD Renderer, 360° Cockpit, Munt Roland MT-32/CM-32L + SB16 Speech, Multi-HOTAS & 240 Hz F-14 FM");
    ImGui::Separator();
    ImGui::Spacing();

    struct LaunchCard {
        const char* title;
        const char* exe;
        const char* subtitle;
    };
    static const LaunchCard kCards[4] = {
        {"Fleet Defender Gold", "F14.COM",     "Full Campaign, Scramble, Training & Carrier Ops (F14.COM -> F.EXE -> F14.EXE)"},
        {"Scenario Builder",    "F14S.EXE",    "Create custom missions, carrier battle groups & MiG/Su threat packages"},
        {"Direct Flight",       "F14.EXE",     "Jump straight into the F-14 Tomcat cockpit (skips front-end menu)"},
        {"Music Jukebox",       "F14JUKE.EXE", "Play the original Roland MT-32 / CM-32L soundtrack directly"},
    };

    float avail_w = ImGui::GetContentRegionAvail().x;
    float left_w = std::clamp(avail_w * 0.47f, 340.0f, 640.0f);
    float top_pane_h = launcher_show_keys_ ? 295.0f : std::max(360.0f, ImGui::GetContentRegionAvail().y - 54.0f);

    // Left column: Launch Cards
    ImGui::BeginChild("##LauncherCardsPane", ImVec2(left_w, top_pane_h), true);
    ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.38f, 1.0f), "LAUNCH TARGET (Click to select, Double-Click or Enter to Fly)");
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < 4; ++i) {
        bool exists = file_exists_ci(game_dir, kCards[i].exe);
        bool selected = (launcher_selected_card_ == i);
        ImVec4 bg = selected ? ImVec4(0.14f, 0.30f, 0.46f, 1.0f) : ImVec4(0.09f, 0.14f, 0.21f, 0.95f);
        ImVec4 brd = selected ? ImVec4(0.35f, 0.88f, 1.0f, 1.0f) : ImVec4(0.24f, 0.34f, 0.46f, 0.85f);

        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Border, brd);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);

        float card_h = launcher_show_keys_ ? 52.0f : 66.0f;
        ImGui::BeginChild("##card", ImVec2(-1.0f, card_h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextColored(selected ? ImVec4(1.0f, 0.96f, 0.82f, 1.0f) : ImVec4(0.90f, 0.95f, 1.0f, 1.0f),
                           "%s  [%s]", kCards[i].title, kCards[i].exe);
        ImGui::SameLine();
        float st_x = std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowWidth() - 96.0f);
        ImGui::SetCursorPosX(st_x);
        if (exists) {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "READY");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.38f, 1.0f), "NOT FOUND");
        }
        ImGui::TextColored(ImVec4(0.70f, 0.80f, 0.90f, 1.0f), "%s", kCards[i].subtitle);
        ImGui::EndChild();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            launcher_selected_card_ = i;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && exists) {
            dec.launch = true;
            dec.exe_name = kCards[i].exe;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        ImGui::Spacing();
    }

    ImGui::EndChild();

    // Right column: Paths, Quick Settings, Manuals
    ImGui::SameLine();
    ImGui::BeginChild("##LauncherConfigPane", ImVec2(0.0f, top_pane_h), true);
    ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.38f, 1.0f), "INSTALL PATHS & QUICK SETTINGS");
    ImGui::Separator();

    // Game Dir
    ImGui::TextUnformatted("Fleet Defender Gold Folder:");
    ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 182.0f));
    if (ImGui::InputText("##GameDir", launcher_game_buf_, sizeof(launcher_game_buf_))) {
        game_dir = launcher_game_buf_;
        manuals_ = discover_local_manuals(game_dir);
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...##Game")) {
        std::string picked = browse_for_folder("Select Fleet Defender Gold installation folder (containing F14.COM / F14.EXE):");
        if (!picked.empty()) {
            game_dir = picked;
            std::snprintf(launcher_game_buf_, sizeof(launcher_game_buf_), "%s", game_dir.c_str());
            manuals_ = discover_local_manuals(game_dir);
            launcher_status_ = "Selected game folder.";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto##Game")) {
        std::string det = autodetect_f14_install_dir();
        if (!det.empty()) {
            game_dir = det;
            std::snprintf(launcher_game_buf_, sizeof(launcher_game_buf_), "%s", game_dir.c_str());
            manuals_ = discover_local_manuals(game_dir);
            launcher_status_ = "Auto-detected Fleet Defender Gold install!";
        } else {
            launcher_status_ = "Auto-detect did not find F14.COM/F14.EXE — please Browse.";
        }
    }

    // ROM Dir
    ImGui::TextUnformatted("Roland CM-32L / MT-32 ROM Folder (optional):");
    ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 94.0f));
    if (ImGui::InputText("##RomDir", launcher_rom_buf_, sizeof(launcher_rom_buf_))) {
        rom_dir = launcher_rom_buf_;
        audio_.config().cm32l_rom_dir = rom_dir;
        sync_rom_dir_buf(rom_dir);
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...##Rom")) {
        std::string picked = browse_for_folder("Select Roland CM-32L / MT-32 ROM folder:");
        if (!picked.empty()) {
            rom_dir = picked;
            audio_.config().cm32l_rom_dir = rom_dir;
            sync_rom_dir_buf(rom_dir);
            launcher_status_ = dir_has_roms(rom_dir) ? "Valid CM-32L / MT-32 ROMs found!" : "Folder selected (no ROM pair detected yet).";
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Quick settings synced 2-way with f14_settings.json
    const char* modes[] = {"Classic VGA (320x200)", "Modern 3D (Widescreen F-14)", "Hybrid Cockpit (HD 3D + 2D HUD)", "VR Stereo Preview"};
    int m = static_cast<int>(renderer_.mode());
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("Render Mode (F11)", &m, modes, 4)) {
        renderer_.mode() = static_cast<RenderMode>(m);
    }
    auto& vr = renderer_.vr_config();
    ImGui::Checkbox("Fill Widescreen 3D World", &vr.hybrid_fill_widescreen);
    ImGui::SameLine();
    bool si = machine_.skip_intro();
    if (ImGui::Checkbox("Skip Intro (Fast Boot)", &si)) machine_.set_skip_intro(si);

    ImGui::Checkbox("240 Hz Native F-14 Flight Model", &flight_model_.config().enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Show Launcher on Startup", &show_launcher_on_startup);

    // Local Manuals & Reference Bar
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.38f, 1.0f), "MANUALS & QUICK REFERENCE");
    if (manuals_.empty()) {
        ImGui::TextColored(ImVec4(0.62f, 0.72f, 0.82f, 1.0f), "No local PDF manuals detected in selected game folder.");
    } else {
        float used_w = 0.0f;
        float max_w = ImGui::GetContentRegionAvail().x;
        for (size_t i = 0; i < manuals_.size(); ++i) {
            float btn_w = ImGui::CalcTextSize(manuals_[i].label.c_str()).x + 18.0f;
            if (i > 0 && used_w + btn_w < max_w) {
                ImGui::SameLine();
            } else {
                used_w = 0.0f;
            }
            if (ImGui::SmallButton(manuals_[i].label.c_str())) {
                open_shell_path(manuals_[i].path);
            }
            used_w += btn_w + 6.0f;
        }
    }

    if (!launcher_status_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.45f, 0.92f, 0.65f, 1.0f), "%s", launcher_status_.c_str());
    }
    ImGui::EndChild();

    // Expandable Dynamic Resolution-Independent Vector Keyboard Layout
    if (launcher_show_keys_) {
        ImGui::Spacing();
        float kb_h = std::max(210.0f, ImGui::GetContentRegionAvail().y - 48.0f);
        ImGui::BeginChild("##LauncherQuickKeysPane", ImVec2(0.0f, kb_h), true);
        draw_quick_keys_panel(quick_keys_state_, external_chart_path);
        ImGui::EndChild();
    }

    // Bottom Action Bar
    ImGui::Spacing();
    bool sel_ready = file_exists_ci(game_dir, kCards[launcher_selected_card_].exe);
    if (!sel_ready) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.56f, 0.32f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.68f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.48f, 0.28f, 1.0f));
    char launch_lbl[128];
    std::snprintf(launch_lbl, sizeof(launch_lbl), "LAUNCH %s (Enter)", kCards[launcher_selected_card_].title);
    if (ImGui::Button(launch_lbl, ImVec2(280.0f, 34.0f))) {
        dec.launch = true;
        dec.exe_name = kCards[launcher_selected_card_].exe;
    }
    ImGui::PopStyleColor(3);
    if (!sel_ready) ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, launcher_show_keys_ ? ImVec4(0.16f, 0.48f, 0.62f, 1.0f) : ImVec4(0.16f, 0.25f, 0.36f, 1.0f));
    if (ImGui::Button(launcher_show_keys_ ? "Hide Keyboard Layout" : "Quickstart Keyboard Layout", ImVec2(220.0f, 34.0f))) {
        launcher_show_keys_ = !launcher_show_keys_;
    }
    ImGui::PopStyleColor();

    if (!external_chart_path.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Open Vector SVG Chart", ImVec2(175.0f, 34.0f))) {
            open_shell_path(external_chart_path);
        }
    }

    ImGui::SameLine();
    float quit_x = std::max(ImGui::GetCursorPosX() + 12.0f, ImGui::GetWindowWidth() - 124.0f);
    ImGui::SetCursorPosX(quit_x);
    if (ImGui::Button("Quit", ImVec2(100.0f, 34.0f))) {
        dec.quit = true;
    }

    // Keyboard shortcut: Enter launches selected card when not editing text
    if (!ImGui::GetIO().WantTextInput && sel_ready &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        dec.launch = true;
        dec.exe_name = kCards[launcher_selected_card_].exe;
    }
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        dec.quit = true;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return dec;
}

void OverlayUI::draw(const FlightTelemetry& tel) {
    if (!visible_) return;

    if (ImGui::BeginMainMenuBar()) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "FLEET DEFENDER GOLD NATIVE");
        ImGui::Separator();

        if (ImGui::BeginMenu("Game")) {
            if (show_launcher_ptr_) {
                if (ImGui::MenuItem("Show Launcher on Startup", nullptr, *show_launcher_ptr_)) {
                    *show_launcher_ptr_ = !(*show_launcher_ptr_);
                    if (save_settings_cb_) save_settings_cb_();
                }
            }
            bool si = machine_.skip_intro();
            if (ImGui::MenuItem("Skip Startup Intro (Fast Boot)", nullptr, si)) {
                machine_.set_skip_intro(!si);
                if (save_settings_cb_) save_settings_cb_();
            }
            if (ImGui::MenuItem("Quick Keys (Scalable Keyboard)", nullptr, show_quick_keys_)) {
                show_quick_keys_ = !show_quick_keys_;
            }
            if (!manuals_.empty() && ImGui::BeginMenu("Open Local Manual")) {
                for (const auto& m : manuals_) {
                    if (ImGui::MenuItem(m.label.c_str())) {
                        open_shell_path(m.path);
                    }
                }
                ImGui::EndMenu();
            }
            if (!external_chart_path_.empty() && ImGui::MenuItem("Open Vector Keyboard SVG")) {
                open_shell_path(external_chart_path_);
            }
            ImGui::Separator();
            if (save_settings_cb_ && ImGui::MenuItem("Save Settings")) {
                settings_status_ = save_settings_cb_() ? ("Saved " + settings_path_) : "Save settings failed";
            }
            if (quit_cb_ && ImGui::MenuItem("Quit to Desktop")) {
                quit_cb_();
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("3D / VR", "F11", show_render_)) show_render_ = !show_render_;
        if (ImGui::MenuItem("Flight Model", nullptr, show_flight_model_)) show_flight_model_ = !show_flight_model_;
        if (ImGui::MenuItem("HOTAS", nullptr, show_hotas_)) show_hotas_ = !show_hotas_;
        if (ImGui::MenuItem("Audio", nullptr, show_audio_)) show_audio_ = !show_audio_;
        if (ImGui::MenuItem("Quick Keys", nullptr, show_quick_keys_)) show_quick_keys_ = !show_quick_keys_;
        if (ImGui::MenuItem("Net", nullptr, show_multiplayer_)) show_multiplayer_ = !show_multiplayer_;
        if (ImGui::MenuItem("RE Debug", nullptr, show_debugger_)) show_debugger_ = !show_debugger_;
        ImGui::Separator();
        auto snap = machine_.snapshot();
        ImGui::Text("Proc: %s | VGA: 0x%02X | EMS: %d/%d | [F12 Hide]",
                    snap.active_proc.c_str(), machine_.vga().mode(),
                    snap.ems_pages_used, snap.ems_pages_total);
        ImGui::EndMainMenuBar();
    }

    if (show_render_) draw_render_vr_window(tel);
    if (show_flight_model_) draw_flight_model_window(tel);
    if (show_hotas_) draw_hotas_window();
    if (show_audio_) draw_audio_window();
    if (show_quick_keys_) draw_quick_keys_window();
    if (show_multiplayer_) draw_multiplayer_window();
    if (show_debugger_) draw_debugger_window();
}

void OverlayUI::draw_quick_keys_window() {
    ImGui::SetNextWindowSize(ImVec2(1060.0f, 410.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Fleet Defender Gold — Quick Keys (Resize Window to Scale)", &show_quick_keys_)) {
        ImGui::End();
        return;
    }
    draw_quick_keys_panel(quick_keys_state_, external_chart_path_);
    ImGui::End();
}

void OverlayUI::draw_render_vr_window(const FlightTelemetry& tel) {
    if (!ImGui::Begin("3D / VR Renderer", &show_render_)) { ImGui::End(); return; }
    const char* modes[] = {"Classic VGA (320x200)", "Modern 3D (Widescreen F-14)", "Hybrid Cockpit (HD 3D + 2D HUD)", "VR Stereo Preview"};
    int m = static_cast<int>(renderer_.mode());
    if (ImGui::Combo("Render Mode (F11)", &m, modes, 4)) renderer_.mode() = static_cast<RenderMode>(m);

    auto& cfg = renderer_.vr_config();
    ImGui::Checkbox("Hybrid: Fill Widescreen 3D World", &cfg.hybrid_fill_widescreen);
    ImGui::Checkbox("Modern 3D: Widescreen Virtual F-14 Cockpit", &cfg.widescreen_3d_cockpit);
    ImGui::Checkbox("Unclamped 3D Padlock", &cfg.enhanced_3d_padlock);
    ImGui::Checkbox("3D Clouds", &cfg.show_clouds);
    ImGui::Checkbox("Horizon Fog", &cfg.horizon_fog);
    ImGui::Checkbox("Wireframe Terrain", &cfg.wireframe_terrain);

    const char* scale_modes[] = {"Off (1:1)", "Serfoss 2003", "Why485 (FOV + Res)", "Legacy Linear"};
    int sm = static_cast<int>(cfg.smart_scaling_mode);
    if (ImGui::Combo("Smart Scaling", &sm, scale_modes, 4)) cfg.smart_scaling_mode = static_cast<SmartScalingMode>(sm);
    ImGui::SliderFloat("FOV (deg)", &cfg.fov_deg, 30.0f, 75.0f);
    ImGui::SliderFloat("Widescreen FOV (deg)", &cfg.widescreen_fov_deg, 38.0f, 70.0f);
    ImGui::SliderFloat("Smart Scale Mult", &cfg.aircraft_scale, 0.5f, 2.5f);

    const char* light_modes[] = {"Auto from Game", "Day", "Dusk", "Night"};
    int lm = static_cast<int>(cfg.lighting_mode);
    if (ImGui::Combo("Lighting", &lm, light_modes, 4)) cfg.lighting_mode = static_cast<LightingMode>(lm);

    ImGui::Separator();
    ImGui::Text("World: %s | 3DZ Models: %zu | In Flight: %s",
                renderer_.loaded_theater().c_str(), renderer_.loaded_model_count(),
                tel.in_flight ? "YES" : "NO");
    if (tel.in_flight) {
        ImGui::Text("Pos: (%.0f, %.0f) Alt: %.0f ft | HDG: %.1f P: %.1f R: %.1f | %.0f KIAS",
                    tel.world_x, tel.world_y, tel.altitude_ft, tel.yaw_deg, tel.pitch_deg, tel.roll_deg, tel.airspeed_kts);
    }
    ImGui::End();
}

void OverlayUI::draw_flight_model_window(const FlightTelemetry& /*tel*/) {
    if (!ImGui::Begin("F-14 Tomcat 240 Hz Flight Model Mod", &show_flight_model_)) { ImGui::End(); return; }
    auto& cfg = flight_model_.config();
    ImGui::Checkbox("Enable Native 240 Hz F-14 Flight Model", &cfg.enabled);
    ImGui::Checkbox("F-14B/D GE F110-GE-400 Engines (else F-14A TF30)", &cfg.engine_f110_ge400);
    ImGui::Checkbox("CADC Auto Wing Sweep (20-68 deg)", &cfg.auto_wing_sweep);
    if (!cfg.auto_wing_sweep) ImGui::SliderFloat("Manual Sweep (deg)", &cfg.manual_sweep_deg, 20.0f, 68.0f);
    ImGui::Checkbox("Soft AOA Limiter", &cfg.aoa_limiter);
    const auto& st = flight_model_.state();
    ImGui::Separator();
    ImGui::Text("KIAS: %.0f | Mach: %.2f | AOA: %.1f deg | G: %.1f | Sweep: %.0f deg",
                st.kias, st.mach, st.aoa_deg, st.g_load, st.wing_sweep_deg);
    ImGui::Text("Thrust: %.0f lbf | Drag: %.0f lbf | Ps: %.0f ft/s", st.thrust_lbf, st.drag_lbf, st.ps_fps);
    ImGui::End();
}

void OverlayUI::draw_hotas_window() {
    if (!ImGui::Begin("Multi-HOTAS Mapper", &show_hotas_)) { ImGui::End(); return; }
    const char* inp_modes[] = {"Auto Seamless (HOTAS + Keyboard)", "Force HOTAS", "Force Keyboard Only"};
    int im = static_cast<int>(machine_.flight_input_mode());
    if (ImGui::Combo("Flight Input Mode", &im, inp_modes, 3)) {
        machine_.set_flight_input_mode(static_cast<FlightInputMode>(im));
    }
    if (ImGui::Button("Refresh USB Devices")) hotas_.refresh_devices();
    ImGui::SameLine();
    if (ImGui::Button("Save f14_hotas.json")) {
        profile_status_ = hotas_.save_profile(hotas_path_) ? ("Saved " + hotas_path_) : "Save failed";
    }
    ImGui::SameLine();
    if (ImGui::Button("Load f14_hotas.json")) {
        profile_status_ = hotas_.load_profile(hotas_path_) ? ("Loaded " + hotas_path_) : "Load failed";
    }
    if (!profile_status_.empty()) ImGui::TextUnformatted(profile_status_.c_str());

    ImGui::Separator();
    ImGui::Text("Detected Devices (%zu):", hotas_.devices().size());
    for (const auto& d : hotas_.devices()) {
        ImGui::BulletText("%s (%d axes, %d btns, %d hats)", d.name.c_str(), d.num_axes, d.num_buttons, d.num_hats);
    }

    ImGui::Separator();
    auto& ab = hotas_.axis_bindings();
    for (int i = 0; i < static_cast<int>(FlightAxis::Count); ++i) {
        auto fa = static_cast<FlightAxis>(i);
        ImGui::PushID(i);
        bool cap = (hotas_.capturing_axis() == i);
        if (ImGui::Button(cap ? "Move Axis..." : "Bind")) {
            if (cap) hotas_.cancel_capture(); else hotas_.start_axis_capture(fa);
        }
        ImGui::SameLine();
        ImGui::Text("%-10s: %s (ax %d) = %+.2f", flight_axis_name(fa),
                    ab[i].axis_index >= 0 ? ab[i].device_name.c_str() : "Unbound",
                    ab[i].axis_index, hotas_.axis_value(fa));
        ImGui::SameLine();
        ImGui::Checkbox("Inv", &ab[i].invert);
        ImGui::PopID();
    }

    ImGui::Separator();
    auto format_src = [](const ButtonBinding& b) -> std::string {
        if (b.source == InputSourceType::Button) {
            return b.device_name + " Btn " + std::to_string(b.index + 1);
        }
        if (b.source == InputSourceType::Hat) {
            const char* dir = (b.hat_mask == SDL_HAT_UP) ? "Up" :
                              (b.hat_mask == SDL_HAT_DOWN) ? "Down" :
                              (b.hat_mask == SDL_HAT_LEFT) ? "Left" :
                              (b.hat_mask == SDL_HAT_RIGHT) ? "Right" : "Dir";
            return b.device_name + " Hat " + std::to_string(b.index + 1) + " " + dir;
        }
        return "Unbound";
    };
    {
        auto& pk = hotas_.pinky_shift_binding();
        bool cap_pk = hotas_.capturing_pinky();
        if (ImGui::Button(cap_pk ? "Press Pinky..." : "Bind Pinky Shift")) {
            if (cap_pk) hotas_.cancel_capture(); else hotas_.start_pinky_capture();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear##pinky")) { pk.source = InputSourceType::None; pk.index = -1; }
        ImGui::SameLine();
        ImGui::Text("Pinky: %s%s", format_src(pk).c_str(), hotas_.pinky_shift_active() ? " [ACTIVE]" : "");
    }

    ImGui::Text("F-14 Button & POV Hat Bindings:");
    ImGui::BeginChild("HotasButtonsScroll", ImVec2(0, 230), true);
    auto& bb = hotas_.button_bindings();
    for (int i = 0; i < static_cast<int>(bb.size()); ++i) {
        ImGui::PushID(1000 + i);
        bool cap = (hotas_.capturing_button() == i);
        if (ImGui::Button(cap ? "Press..." : "Bind")) {
            if (cap) hotas_.cancel_capture(); else hotas_.start_button_capture(i);
        }
        ImGui::SameLine();
        if (ImGui::Button("X")) { bb[i].source = InputSourceType::None; bb[i].index = -1; }
        ImGui::SameLine();
        ImGui::Checkbox("Shf", &bb[i].require_pinky_shift);
        ImGui::SameLine();
        ImGui::Text("%-24s : %s", bb[i].action_name.c_str(), format_src(bb[i]).c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

void OverlayUI::draw_audio_window() {
    if (!ImGui::Begin("Audio Engine (Roland CM-32L / MT-32 + SB16)", &show_audio_)) { ImGui::End(); return; }
    auto& cfg = audio_.config();
    ImGui::Text("Munt Status: %s", audio_.mt32_status().c_str());
    ImGui::InputText("ROM Folder", rom_dir_buf_, sizeof(rom_dir_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Browse...##AudioRom")) {
        std::string picked = browse_for_folder("Select Roland CM-32L / MT-32 ROM folder:");
        if (!picked.empty()) {
            std::snprintf(rom_dir_buf_, sizeof(rom_dir_buf_), "%s", picked.c_str());
        }
    }
    if (ImGui::Button("Reload & Save ROM Folder")) {
        cfg.cm32l_rom_dir = rom_dir_buf_;
        audio_.load_mt32_roms(cfg.cm32l_rom_dir);
        if (save_settings_cb_) {
            settings_status_ = save_settings_cb_() ? ("Saved to " + settings_path_) : "Save failed";
        }
    }
    if (!settings_status_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "%s", settings_status_.c_str());
    }
    ImGui::Separator();
    ImGui::SliderFloat("Master Volume", &cfg.master_volume, 0.0f, 1.5f);
    ImGui::SliderFloat("MT-32 / CM-32L Music", &cfg.mt32_volume, 0.0f, 1.5f);
    ImGui::SliderFloat("SB16 Speech & SFX", &cfg.sb16_sfx_volume, 0.0f, 1.5f);
    ImGui::SliderFloat("Engine Bus", &cfg.engine_bus_volume, 0.0f, 1.5f);
    ImGui::SliderFloat("Weapons Bus", &cfg.weapons_bus_volume, 0.0f, 1.5f);
    ImGui::Text("MIDI RX: %llu | SysEx: %llu | SB DAC: %llu | %s",
                static_cast<unsigned long long>(audio_.midi_bytes_received()),
                static_cast<unsigned long long>(audio_.sysex_messages_received()),
                static_cast<unsigned long long>(audio_.sb_dac_samples_received()),
                audio_.last_midi_summary().c_str());
    ImGui::End();
}

void OverlayUI::draw_multiplayer_window() {
    if (!ImGui::Begin("Multiplayer (UDP Bridge)", &show_multiplayer_)) { ImGui::End(); return; }
    ImGui::InputText("Peer IP", join_ip_buf_, sizeof(join_ip_buf_));
    ImGui::InputInt("UDP Port", &net_port_);
    if (ImGui::Button("Host")) net_.start_host(static_cast<uint16_t>(net_port_));
    ImGui::SameLine();
    if (ImGui::Button("Join")) net_.join_host(join_ip_buf_, static_cast<uint16_t>(net_port_));
    ImGui::SameLine();
    if (ImGui::Button("Disconnect")) net_.disconnect();
    ImGui::Text("Status: %s", net_.status().c_str());
    ImGui::End();
}

void OverlayUI::draw_debugger_window() {
    if (!ImGui::Begin("Live RE Debugger", &show_debugger_)) { ImGui::End(); return; }
    auto s = machine_.snapshot();
    ImGui::Text("Active Process: %s (depth=%d) | SHA Verified: %s",
                s.active_proc.c_str(), s.proc_depth,
                machine_.loaded_exe_sha_verified() ? "YES" : "NO");
    ImGui::Text("CS:IP=%04X:%04X SS:SP=%04X:%04X DS=%04X ES=%04X", s.cs, s.ip, s.ss, s.sp, s.ds, s.es);
    ImGui::Text("AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X", s.eax & 0xFFFF, s.ebx & 0xFFFF, s.ecx & 0xFFFF, s.edx & 0xFFFF, s.esi & 0xFFFF, s.edi & 0xFFFF);
    ImGui::Text("Insns: %llu | VGA Mode: 0x%02X | EMS: %d/%d",
                static_cast<unsigned long long>(s.total_insns), machine_.vga().mode(),
                s.ems_pages_used, s.ems_pages_total);
    bool p = machine_.paused();
    if (ImGui::Checkbox("Pause CPU", &p)) machine_.set_paused(p);
    ImGui::SameLine();
    bool si = machine_.skip_intro();
    if (ImGui::Checkbox("Skip Intro (PLAYFLC)", &si)) machine_.set_skip_intro(si);
    ImGui::Separator();
    ImGui::BeginChild("LogScroll", ImVec2(0, 260), true);
    for (const auto& line : machine_.log_lines()) {
        ImGui::TextUnformatted(line.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace f14

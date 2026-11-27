#pragma once

#include <SDL3/SDL_opengl.h>

#include "render/cockpit_3d.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace f14 {

enum class RenderMode : int {
    ClassicVga = 0,
    Modern3D = 1,
    HybridCockpit = 2,
    VrStereoPreview = 3
};

enum class LightingMode : int {
    AutoFromGame = 0, // Automatically tracks Fleet Defender's live Day / Dusk / Night VGA palette!
    Day = 1,
    Dusk = 2,
    Night = 3
};

enum class SmartScalingMode : int {
    Off = 0,          // 1:1 true physical geometry (no scaling)
    Serfoss2003 = 1,  // Classic Falcon BMS / Gary Serfoss (2003) quadratic slant-range curve
    Why485FovRes = 2, // Why485 / Chihirobelmo FOV + Viewport-Resolution-aware Smart Scaling (Recommended)
    LegacyLinear = 3  // Simple linear distance boost
};

struct WorldEntity {
    double world_x = 0.0;   // Fleet Defender 20-bit East
    double world_y = 0.0;   // Fleet Defender 20-bit South
    double world_z = 0.0;   // Altitude in feet
    float yaw_deg = 0.0f;
    float pitch_deg = 0.0f;
    float roll_deg = 0.0f;
    uint16_t slot_id = 0;
    uint16_t flags = 0;
    uint16_t type_id = 0;
    std::string model_name; // .3DZ filename if resolved
    std::string label;
    bool is_player = false;
    bool is_ship = false;
    bool is_carrier = false;
    bool is_ground = false;
    bool is_friendly = false;
};

struct WorldMissile {
    uint16_t slot_id = 0;
    double world_x = 0.0;
    double world_y = 0.0;
    double world_z = 0.0;
    float yaw_deg = 0.0f;
    float pitch_deg = 0.0f;
    float roll_deg = 0.0f;
    uint16_t age_ticks = 0;
    uint16_t kind_id = 0;
    bool is_sam = false;
};

struct FlightTelemetry {
    bool in_flight = false;
    bool back_seat_rio = false;

    // Player F-14 6-DOF state
    double world_x = 0.0;
    double world_y = 0.0;
    float altitude_ft = 5000.0f;
    float agl_ft = 5000.0f;
    float yaw_deg = 0.0f;    // 0..360 (0=N, 90=E, 270=W)
    float pitch_deg = 0.0f;  // -90..+90
    float roll_deg = 0.0f;   // -180..+180
    float airspeed_kts = 0.0f;
    float mach = 0.0f;
    float g_load = 1.0f;
    float wing_sweep_deg = 20.0f;
    float throttle_01 = 0.6f;
    bool gear_down = false;
    bool hook_down = false;

    // Weather / Cloud deck telemetry from F14.EXE HORZ.C (DGROUP+0x2AF8/+0x2B00/+0x2B06)
    float cloud_deck_ft = 6500.0f;
    bool above_clouds = false;
    bool overcast = false;

    // Active 3D camera state
    bool has_camera = false;
    bool is_external_cam = false;
    double cam_x = 0.0;
    double cam_y = 0.0;
    float cam_alt_ft = 5000.0f;
    float cam_yaw_deg = 0.0f;
    float cam_pitch_deg = 0.0f;
    float cam_roll_deg = 0.0f;
    int world_y_min = 0;
    int world_y_max = 110;

    // Active Fleet Defender View Mode (0=FRONT, 1=LEFT, 2=RIGHT, 3=DOWN, 4=REARLEFT, 5=REARRIGHT, 0x46=FRONTAL, 0x47=PADLOCK, 0x48=PILOT, 0xC9+=External)
    uint16_t view_mode = 0;
    bool padlock_active = false;
    bool padlock_has_target = false;
    bool padlock_target_is_missile = false;
    double padlock_target_x = 0.0;
    double padlock_target_y = 0.0;
    double padlock_target_z = 0.0;
    float padlock_range_nm = 0.0f;
    char padlock_target_name[16]{};

    std::vector<WorldEntity> entities;     // Live aircraft & ships (carriers, escorts, bandits, wingmen)
    std::vector<WorldEntity> ground_units; // Live radars / SAM sites / ground objects
    std::vector<WorldMissile> missiles;    // Active AIM-54 / AIM-7 / AIM-9 / enemy missiles & SAMs
};

struct VrCameraConfig {
    float fov_deg = 42.0f;
    float ipd_mm = 63.0f;
    float head_yaw_deg = 0.0f;
    float head_pitch_deg = 0.0f;
    float terrain_scale = 1.0f;
    float aircraft_scale = 1.0f;
    SmartScalingMode smart_scaling_mode = SmartScalingMode::Why485FovRes;
    bool smart_scale_ground = true;
    float smooth_rate = 18.0f;
    float hud_cy_trim_px = 0.0f;
    bool match_game_fov = true;
    bool use_original_hud = true;
    bool sync_hud_no_lag = true;
    bool show_vector_hud = false;
    bool show_clouds = true;
    bool show_aircraft = true;
    bool show_ships = true;
    bool show_ground_objects = true;
    bool show_missiles_trails = true;
    float missile_trail_lifetime = 11.0f;
    bool show_explosions = true;
    bool show_contrails = true;
    bool ocean_waves = true;
    bool horizon_fog = true;
    bool wireframe_terrain = false;
    bool enhanced_3d_padlock = true;
    bool virtual_cockpit_freelook = true;
    bool hybrid_fill_widescreen = true;
    bool widescreen_3d_cockpit = true;
    float widescreen_fov_deg = 48.0f;
    LightingMode lighting_mode = LightingMode::AutoFromGame;
};

class ModernRenderer {
public:
    ModernRenderer();
    ~ModernRenderer();

    // Load Fleet Defender Gold native 3D theater (.WLD) and 3D models (.3DZ) from game_data/
    bool load_world_wld(const std::string& wld_path);
    bool load_3dz_directory(const std::string& game_dir);

    void set_vga_palette(const uint8_t* dac_768);
    void update_telemetry(const FlightTelemetry& raw, float dt_sec);

    void render_3d_view(int x, int y, int w, int h, const FlightTelemetry& tel,
                        float eye_offset_m = 0.0f, float cy_vga = 80.0f, bool vga_frustum = false,
                        bool widescreen_cockpit = false, float vga_frustum_aspect = 4.0f / 3.0f);

    void render_vector_hud(int x, int y, int w, int h, const FlightTelemetry& tel);

    void render_frame(int win_w, int win_h, GLuint vga_tex, GLuint vga_cockpit_tex, GLuint vga_hud_tex,
                      const FlightTelemetry& raw_tel, float dt_sec);

    void spawn_chaff(const FlightTelemetry& tel);
    void spawn_flare(const FlightTelemetry& tel);
    void spawn_gun_tracer(const FlightTelemetry& tel);

    Cockpit3D& cockpit_3d() { return cockpit_3d_; }
    const Cockpit3D& cockpit_3d() const { return cockpit_3d_; }

    RenderMode& mode() { return mode_; }
    RenderMode mode() const { return mode_; }
    VrCameraConfig& vr_config() { return vr_cfg_; }
    const VrCameraConfig& vr_config() const { return vr_cfg_; }
    const std::string& loaded_theater() const { return loaded_theater_; }
    const FlightTelemetry& smoothed_telemetry() const { return smooth_tel_; }
    size_t loaded_model_count() const { return models_3dz_.size(); }
    float current_daylight() const { return lit_.daylight; }
    float current_night_amount() const { return lit_.night_amount; }
    float current_sunset_amount() const { return lit_.sunset_amount; }
    float current_display_ppd() const { return last_display_ppd_; }
    float compute_smart_scale(float slant_km, bool is_ground) const;

private:
    struct SceneLighting {
        float daylight = 1.0f;
        float night_amount = 0.0f;
        float sunset_amount = 0.0f;
        float sky_top[3]     = {0.22f, 0.44f, 0.85f};
        float sky_mid[3]     = {0.48f, 0.66f, 0.92f};
        float horizon[3]     = {0.72f, 0.80f, 0.88f};
        float ocean_rgb[3]   = {0.06f, 0.19f, 0.38f};
        float land_rgb[3]    = {0.26f, 0.38f, 0.22f};
        float ground_mul[3]  = {1.0f, 1.0f, 1.0f};
        float cloud_rgb[3]   = {0.95f, 0.96f, 0.98f};
    };

    struct Model3dz {
        struct Vec3 { float x = 0, y = 0, z = 0; };
        struct Face {
            uint8_t color_idx = 15;
            bool textured = false;
            std::vector<uint16_t> verts;
            std::vector<float> u;
            std::vector<float> v;
            Vec3 normal{};
        };
        std::string name;
        std::string tex_pcx_name;
        std::vector<Vec3> verts;
        std::vector<Face> faces;
        float bound_radius = 10.0f;
    };

    struct PcxTexture {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> indices; // width * height 8-bit palette indices
        GLuint gl_tex = 0;
        bool dirty = true;
    };

    struct WorldFx {
        enum class Kind : uint8_t { Chaff = 0, Flare = 1, Tracer = 2 };
        Kind kind = Kind::Flare;
        double wx = 0.0, wy = 0.0, wz = 0.0;
        double vx = 0.0, vy = 0.0, vz = 0.0;
        float age = 0.0f;
        float max_age = 3.5f;
    };

    struct MissileTrailPt {
        double wx = 0.0, wy = 0.0, wz = 0.0;
        float age = 0.0f;
    };

    RenderMode mode_ = RenderMode::ClassicVga;
    VrCameraConfig vr_cfg_;
    SceneLighting lit_{};
    std::string loaded_theater_ = "None";

    // Fleet Defender .WLD 256x256 heightmap + biome map + palette + 3D ground objects
    struct WldGroundObj {
        int16_t num = -1;
        int16_t x = 0;
        int16_t y = 0;
    };
    bool has_wld_ = false;
    std::array<uint8_t, 768> wld_pal_{};
    std::vector<uint8_t> wld_hmap_; // 65536 bytes (256x256 elevation)
    std::vector<uint8_t> wld_bmap_; // 65536 bytes (256x256 biome/group index; 255 = open ocean)
    std::array<std::array<WldGroundObj, 16>, 96> wld_objects_{};
    std::array<int16_t, 32> wld_obj_scales_{};
    std::array<std::string, 32> wld_obj_names_{};

    // Loaded .3DZ models & .PCX textures keyed by uppercase filename
    std::string game_dir_;
    std::unordered_map<std::string, Model3dz> models_3dz_;
    std::unordered_map<std::string, PcxTexture> pcx_textures_;
    GLuint cloud_tex_ = 0;
    GLuint ocean_tex_ = 0;
    GLuint get_or_upload_pcx_texture(const std::string& pcx_name);
    GLuint ensure_cloud_texture();
    GLuint ensure_ocean_texture();
    bool load_pcx_file(const std::string& path, PcxTexture& out);
    void draw_night_stars();
    void draw_padlock_xray_box(int x, int y, int w, int h, const FlightTelemetry& tel,
                               float cam_yaw, float cam_pitch, float cam_roll,
                               float vfov_deg, float horizon_ndc);

    std::array<float, 256 * 3> vga_rgb_{};
    std::array<uint8_t, 768> vga_dac_raw_{};
    bool has_vga_pal_ = false;
    Cockpit3D cockpit_3d_;

    FlightTelemetry smooth_tel_{};
    bool has_smooth_tel_ = false;
    float sim_time_sec_ = 0.0f;
    float last_dt_sec_ = 0.016f;
    float last_display_ppd_ = 28.0f;

    std::vector<WorldFx> world_fx_;
    std::unordered_map<uint16_t, std::vector<MissileTrailPt>> missile_trails_;

    bool parse_3dz_file(const std::string& path, Model3dz& out);
    void update_scene_lighting();
    void draw_vga_quad(int x, int y, int w, int h, GLuint tex,
                       float v_min = 0.0f, float v_max = 1.0f, bool alpha_blend = false);
    void draw_sky_backdrop(float pitch_deg, float roll_deg, float horizon_ndc_y, float vfov_deg, float aspect);
    void draw_ocean_and_wld_terrain(const FlightTelemetry& tel);
    void draw_clouds(const FlightTelemetry& tel);
    void draw_entities_and_ships(const FlightTelemetry& tel);
    void draw_missiles_and_fx(const FlightTelemetry& tel);
    void draw_3dz_model(const Model3dz& m, float scale, float daylight);
    void draw_fallback_jet(float scale, bool friendly, bool is_tomcat);
    void draw_fallback_carrier(float scale);
};

} // namespace f14

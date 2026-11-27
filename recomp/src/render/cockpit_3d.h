#pragma once

#include <SDL3/SDL_opengl.h>

#include <array>
#include <cstdint>

namespace f14 {

// Full 360-degree Retro Pixel-Art 3D F-14 Tomcat Virtual Cockpit & Airframe.
// Combines Fleet Defender's authentic MicroProse F-14 Pilot & RIO cockpit sprites
// harvested live from the running game (zero copyrighted pixels in repo) with matching
// twin-tail variable-sweep F-14 Tomcat airframe geometry (nose radome, canopy bow,
// LERX, variable-sweep wings 20..68 deg, twin vertical stabilizers).
class Cockpit3D {
public:
    static constexpr int kSpriteW = 320;
    static constexpr int kSpriteH = 200;

    enum ViewSlot : int {
        ViewForward = 0,
        ViewLeft = 1,
        ViewRight = 2,
        ViewRearLeft = 3,
        ViewRearRight = 4,
        ViewRioForward = 5,
        ViewCount = 6
    };

    Cockpit3D() = default;
    ~Cockpit3D();

    void set_view_indices(ViewSlot slot, const uint8_t* indices_64k, const uint8_t* alpha_mask_64k = nullptr);
    bool has_view(ViewSlot slot) const {
        int s = static_cast<int>(slot);
        return s >= 0 && s < ViewCount && has_view_[s];
    }
    bool has_any_views() const;
    bool has_all_views() const;
    void clear_views();

    void sync_palette(const uint8_t* dac_768);

    struct LiveCockpitState {
        float yaw_deg = 0.0f;
        float pitch_deg = 0.0f;
        float roll_deg = 0.0f;
        float airspeed_kts = 0.0f;
        float agl_ft = 5000.0f;
        float g_load = 1.0f;
        float wing_sweep_deg = 20.0f;
        float sim_time_sec = 0.0f;
        bool missile_warning = false;
        bool back_seat_rio = false;
    };

    void render(float daylight, const float ground_mul[3],
                GLuint live_fwd_cockpit_tex = 0, bool use_live_forward = false,
                const LiveCockpitState* live_state = nullptr,
                bool show_modern_aux = false);

private:
    void ensure_gl_resources();
    void upload_view_texture(int slot, const uint8_t* dac_768);
    void build_procedural_textures();

    void draw_forward_panel(GLuint tex, float r, float g, float b);
    void draw_angled_panel(GLuint tex, float yaw_deg, float r, float g, float b);
    void draw_side_consoles(float r, float g, float b, bool wide_modern_layout);
    void draw_rear_cockpit(float r, float g, float b);
    void draw_exterior_tomcat(float r, float g, float b, float daylight, float wing_sweep_deg, float sim_time_sec);
    void draw_canopy_frame(float daylight);

    std::array<std::array<uint8_t, kSpriteW * kSpriteH>, ViewCount> raw_idx_{};
    std::array<std::array<uint8_t, kSpriteW * kSpriteH>, ViewCount> raw_opaque_{};
    std::array<bool, ViewCount> has_view_{};
    std::array<bool, ViewCount> dirty_view_{};
    std::array<GLuint, ViewCount> view_tex_{};

    std::array<uint8_t, 768> last_dac_{};
    bool has_dac_ = false;
    bool gl_ready_ = false;
    GLuint tex_metal_ = 0;
    GLuint tex_wing_ = 0;
};

} // namespace f14

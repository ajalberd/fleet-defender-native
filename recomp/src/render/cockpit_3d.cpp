#include "render/cockpit_3d.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace f14 {

Cockpit3D::~Cockpit3D() {
    for (GLuint t : view_tex_) {
        if (t != 0) glDeleteTextures(1, &t);
    }
    if (tex_metal_ != 0) glDeleteTextures(1, &tex_metal_);
    if (tex_wing_ != 0) glDeleteTextures(1, &tex_wing_);
}

void Cockpit3D::set_view_indices(ViewSlot slot, const uint8_t* indices_64k, const uint8_t* alpha_mask_64k) {
    int s = static_cast<int>(slot);
    if (s < 0 || s >= ViewCount || !indices_64k) return;
    std::memcpy(raw_idx_[s].data(), indices_64k, kSpriteW * kSpriteH);
    if (alpha_mask_64k) {
        std::memcpy(raw_opaque_[s].data(), alpha_mask_64k, kSpriteW * kSpriteH);
    } else {
        for (int i = 0; i < kSpriteW * kSpriteH; ++i) {
            raw_opaque_[s][i] = (indices_64k[i] != 0) ? 255 : 0;
        }
    }
    has_view_[s] = true;
    dirty_view_[s] = true;
}

bool Cockpit3D::has_any_views() const {
    for (bool b : has_view_) if (b) return true;
    return false;
}

bool Cockpit3D::has_all_views() const {
    return has_view_[ViewForward] && has_view_[ViewLeft] && has_view_[ViewRight];
}

void Cockpit3D::clear_views() {
    has_view_.fill(false);
    dirty_view_.fill(false);
}

void Cockpit3D::sync_palette(const uint8_t* dac_768) {
    if (!dac_768) return;
    if (has_dac_ && std::memcmp(last_dac_.data(), dac_768, 768) == 0) return;
    std::memcpy(last_dac_.data(), dac_768, 768);
    has_dac_ = true;
    for (int i = 0; i < ViewCount; ++i) {
        if (has_view_[i]) dirty_view_[i] = true;
    }
}

void Cockpit3D::ensure_gl_resources() {
    if (gl_ready_) return;
    glGenTextures(ViewCount, view_tex_.data());
    for (int i = 0; i < ViewCount; ++i) {
        glBindTexture(GL_TEXTURE_2D, view_tex_[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    build_procedural_textures();
    gl_ready_ = true;
}

void Cockpit3D::build_procedural_textures() {
    auto make_tex = [](GLuint& out_tex, int w, int h, const std::vector<uint8_t>& rgba) {
        glGenTextures(1, &out_tex);
        glBindTexture(GL_TEXTURE_2D, out_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    };
    // 1994 MicroProse retro dithered Navy Gull Grey cockpit metal
    std::vector<uint8_t> metal(32 * 32 * 4);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            int d = ((x ^ y) & 1) ? 5 : -5;
            bool seam = (x == 0 || y == 0);
            uint8_t c = static_cast<uint8_t>(std::clamp((seam ? 42 : 76) + d, 20, 220));
            int idx = (y * 32 + x) * 4;
            metal[idx + 0] = c;
            metal[idx + 1] = static_cast<uint8_t>(c + 2);
            metal[idx + 2] = static_cast<uint8_t>(c + 6);
            metal[idx + 3] = 255;
        }
    }
    make_tex(tex_metal_, 32, 32, metal);

    // US Navy F-14 Low-Vis Tactical Paint Scheme (TPS) wing texture
    std::vector<uint8_t> wing(64 * 64 * 4);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            int d = (((x + y) & 1) ? 4 : -4);
            bool panel = (x % 16 == 0 || y % 16 == 0);
            int base = panel ? 92 : 128;
            int idx = (y * 64 + x) * 4;
            wing[idx + 0] = static_cast<uint8_t>(std::clamp(base + d, 30, 220));
            wing[idx + 1] = static_cast<uint8_t>(std::clamp(base + 4 + d, 30, 225));
            wing[idx + 2] = static_cast<uint8_t>(std::clamp(base + 12 + d, 30, 235));
            wing[idx + 3] = 255;
        }
    }
    make_tex(tex_wing_, 64, 64, wing);
}

void Cockpit3D::upload_view_texture(int slot, const uint8_t* dac_768) {
    std::vector<uint8_t> rgba(kSpriteW * kSpriteH * 4);
    for (int i = 0; i < kSpriteW * kSpriteH; ++i) {
        uint8_t c = raw_idx_[slot][i];
        uint8_t a = raw_opaque_[slot][i];
        rgba[i * 4 + 0] = static_cast<uint8_t>((dac_768[c * 3 + 0] * 255) / 63);
        rgba[i * 4 + 1] = static_cast<uint8_t>((dac_768[c * 3 + 1] * 255) / 63);
        rgba[i * 4 + 2] = static_cast<uint8_t>((dac_768[c * 3 + 2] * 255) / 63);
        rgba[i * 4 + 3] = a;
    }
    glBindTexture(GL_TEXTURE_2D, view_tex_[slot]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSpriteW, kSpriteH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    dirty_view_[slot] = false;
}

void Cockpit3D::draw_forward_panel(GLuint tex, float r, float g, float b) {
    if (tex == 0) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(r, g, b, 1.0f);
    // 4:3 forward cockpit quad matching pilot eye frustum
    const float z = -1.20f;
    const float hw = 0.80f;
    const float top = 0.60f;
    const float bot = -0.60f;
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-hw, top, z);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( hw, top, z);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( hw, bot, z);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-hw, bot, z);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void Cockpit3D::draw_angled_panel(GLuint tex, float yaw_deg, float r, float g, float b) {
    if (tex == 0) return;
    glPushMatrix();
    glRotatef(-yaw_deg, 0.0f, 1.0f, 0.0f);
    draw_forward_panel(tex, r, g, b);
    glPopMatrix();
}

void Cockpit3D::draw_side_consoles(float r, float g, float b, bool wide_modern_layout) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex_metal_);
    glColor4f(r * 0.75f, g * 0.78f, b * 0.82f, 1.0f);
    float sx = wide_modern_layout ? 0.62f : 0.56f;
    // Left & right cockpit tub sills + instrument side consoles
    for (int side : {-1, 1}) {
        float x = sx * static_cast<float>(side);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex3f(x, -0.18f, -1.20f);
        glTexCoord2f(2, 0); glVertex3f(x, -0.18f,  0.85f);
        glTexCoord2f(2, 1); glVertex3f(x * 0.85f, -0.62f,  0.85f);
        glTexCoord2f(0, 1); glVertex3f(x * 0.85f, -0.62f, -1.20f);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}

void Cockpit3D::draw_rear_cockpit(float r, float g, float b) {
    // Behind the pilot: Martin-Baker GRU-7A ejection seat headrest & RIO bulkhead
    glColor4f(r * 0.28f, g * 0.30f, b * 0.34f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-0.24f,  0.14f, 0.48f);
    glVertex3f( 0.24f,  0.14f, 0.48f);
    glVertex3f( 0.28f, -0.58f, 0.48f);
    glVertex3f(-0.28f, -0.58f, 0.48f);
    glEnd();
}

void Cockpit3D::draw_exterior_tomcat(float r, float g, float b, float daylight, float wing_sweep_deg, float /*sim_time_sec*/) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex_wing_);
    float l = 0.25f + 0.75f * daylight;
    glColor4f(r * l, g * l, b * l, 1.0f);

    // 1. F-14 Long Forward Radome Nose ahead of windscreen
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0.5f, 1.0f); glVertex3f( 0.00f, -0.34f, -5.80f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-0.56f, -0.26f, -1.35f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 0.56f, -0.26f, -1.35f);
    glEnd();

    // 2. Variable-Sweep Tomcat Wings (20..68 deg sweep!) visible over left/right shoulders
    float sweep_rad = std::clamp(wing_sweep_deg, 20.0f, 68.0f) * (3.14159265f / 180.0f);
    float span = 6.2f * std::cos(sweep_rad - 0.35f);
    float aft  = 6.2f * std::sin(sweep_rad);
    for (int side : {-1, 1}) {
        float s = static_cast<float>(side);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex3f(s * 1.10f, -0.38f,  0.60f);
        glTexCoord2f(1, 0); glVertex3f(s * (1.10f + span), -0.42f, 0.60f + aft);
        glTexCoord2f(1, 1); glVertex3f(s * (1.10f + span), -0.44f, 1.65f + aft);
        glTexCoord2f(0, 1); glVertex3f(s * 1.10f, -0.40f,  2.85f);
        glEnd();

        // 3. Iconic F-14 Twin Canted Vertical Stabilizers (visible in Rear-Left / Rear-Right 6 o'clock!)
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex3f(s * 1.15f, -0.30f, 4.20f);
        glTexCoord2f(1, 0); glVertex3f(s * 1.45f,  1.85f, 5.45f);
        glTexCoord2f(1, 1); glVertex3f(s * 1.45f,  1.85f, 6.45f);
        glTexCoord2f(0, 1); glVertex3f(s * 1.15f, -0.30f, 6.75f);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}

void Cockpit3D::draw_canopy_frame(float daylight) {
    float c = 0.08f + 0.14f * daylight;
    glColor4f(c, c, c + 0.02f, 1.0f);
    // F-14 heavy windscreen bow arch framing the HUD glass
    glLineWidth(4.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= 16; ++i) {
        float a = 3.14159265f * (static_cast<float>(i) / 16.0f);
        float x = -0.60f * std::cos(a);
        float y = -0.18f + 0.72f * std::sin(a);
        glVertex3f(x, y, -0.92f);
    }
    glEnd();
    glLineWidth(1.0f);
}

void Cockpit3D::render(float daylight, const float ground_mul[3],
                       GLuint live_fwd_cockpit_tex, bool use_live_forward,
                       const LiveCockpitState* live_state,
                       bool show_modern_aux) {
    ensure_gl_resources();
    if (has_dac_) {
        for (int i = 0; i < ViewCount; ++i) {
            if (has_view_[i] && dirty_view_[i]) upload_view_texture(i, last_dac_.data());
        }
    }
    float r = ground_mul ? ground_mul[0] : 1.0f;
    float g = ground_mul ? ground_mul[1] : 1.0f;
    float b = ground_mul ? ground_mul[2] : 1.0f;
    float sweep = live_state ? live_state->wing_sweep_deg : 20.0f;
    float sim_t = live_state ? live_state->sim_time_sec : 0.0f;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    draw_exterior_tomcat(r, g, b, daylight, sweep, sim_t);
    draw_side_consoles(r, g, b, show_modern_aux);
    draw_rear_cockpit(r, g, b);
    draw_canopy_frame(daylight);

    bool is_rio = live_state && live_state->back_seat_rio;
    GLuint fwd_tex = (use_live_forward && live_fwd_cockpit_tex != 0)
        ? live_fwd_cockpit_tex
        : (is_rio && has_view_[ViewRioForward] ? view_tex_[ViewRioForward]
                                               : (has_view_[ViewForward] ? view_tex_[ViewForward] : 0));
    if (fwd_tex != 0) {
        draw_forward_panel(fwd_tex, 1.0f, 1.0f, 1.0f);
    }
    // 360-degree harvested Pilot side & rear-quarter cockpit panels!
    if (has_view_[ViewLeft] && view_tex_[ViewLeft] != 0) {
        draw_angled_panel(view_tex_[ViewLeft], -90.0f, 1.0f, 1.0f, 1.0f);
    }
    if (has_view_[ViewRight] && view_tex_[ViewRight] != 0) {
        draw_angled_panel(view_tex_[ViewRight], 90.0f, 1.0f, 1.0f, 1.0f);
    }
    if (has_view_[ViewRearLeft] && view_tex_[ViewRearLeft] != 0) {
        draw_angled_panel(view_tex_[ViewRearLeft], -145.0f, 1.0f, 1.0f, 1.0f);
    }
    if (has_view_[ViewRearRight] && view_tex_[ViewRearRight] != 0) {
        draw_angled_panel(view_tex_[ViewRearRight], 145.0f, 1.0f, 1.0f, 1.0f);
    }
    glDisable(GL_BLEND);
}

} // namespace f14

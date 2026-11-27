#include "render/modern_renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace f14 {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
// Fleet Defender 20-bit world units: exact 4.0 ft per PxLng/PyLng unit (1.2192 m),
// verified against AWG9.C (3040 units = 2 NM = 12,152 ft) and live flight TAS!
constexpr double kWorldUnitToMeters = 1.2192;
constexpr double kFeetToMeters = 0.3048;

std::string to_upper_str(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

void set_perspective(float vfov_deg, float aspect, float znear, float zfar, float cy_offset_ndc = 0.0f) {
    float top = znear * std::tan(vfov_deg * 0.5f * kDegToRad);
    float bottom = -top;
    float right = top * aspect;
    float left = -right;
    float dy = cy_offset_ndc * top;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom + dy, top + dy, znear, zfar);
    glMatrixMode(GL_MODELVIEW);
}
} // namespace

ModernRenderer::ModernRenderer() {
    for (int i = 0; i < 256; ++i) {
        vga_rgb_[i * 3 + 0] = static_cast<float>((i * 37) & 0xFF) / 255.0f;
        vga_rgb_[i * 3 + 1] = static_cast<float>((i * 73) & 0xFF) / 255.0f;
        vga_rgb_[i * 3 + 2] = static_cast<float>((i * 113) & 0xFF) / 255.0f;
    }
}

ModernRenderer::~ModernRenderer() {
    for (auto& kv : pcx_textures_) {
        if (kv.second.gl_tex != 0) glDeleteTextures(1, &kv.second.gl_tex);
    }
}

bool ModernRenderer::load_world_wld(const std::string& wld_path) {
    std::ifstream f(wld_path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < 768 + 2 * 65536) return false;

    std::memcpy(wld_pal_.data(), data.data(), 768);
    wld_hmap_.assign(data.begin() + 768, data.begin() + 768 + 65536);
    wld_bmap_.assign(data.begin() + 768 + 65536, data.begin() + 768 + 2 * 65536);

    // Parse trailing OBJECTS[96][16], GroundObjectScale[200], GroundObjectNames[32][20]
    for (auto& grp : wld_objects_) grp.fill(WldGroundObj{});
    wld_obj_scales_.fill(2);
    wld_obj_names_.fill("");
    constexpr size_t kTailOff = 768 + 13 * 65536;
    if (data.size() >= kTailOff + 9216 + 400 + 640) {
        auto rds16 = [&](size_t off) -> int16_t {
            return static_cast<int16_t>(data[off] | (data[off + 1] << 8));
        };
        for (int g = 0; g < 96; ++g) {
            for (int j = 0; j < 16; ++j) {
                size_t o = kTailOff + static_cast<size_t>((g * 16 + j) * 6);
                wld_objects_[g][j].num = rds16(o + 0);
                wld_objects_[g][j].x   = rds16(o + 2);
                wld_objects_[g][j].y   = rds16(o + 4);
            }
        }
        for (int i = 0; i < 32; ++i) {
            wld_obj_scales_[i] = rds16(kTailOff + 9216 + static_cast<size_t>(i * 2));
            char nbuf[21]{};
            std::memcpy(nbuf, data.data() + kTailOff + 9216 + 400 + static_cast<size_t>(i * 20), 20);
            wld_obj_names_[i] = to_upper_str(std::string(nbuf));
        }
    }

    has_wld_ = true;
    loaded_theater_ = fs::path(wld_path).filename().string();
    return true;
}

bool ModernRenderer::load_pcx_file(const std::string& path, PcxTexture& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < 128 || data[0] != 0x0A || data[3] != 8) return false;

    auto rd16 = [&](size_t off) -> uint16_t { return static_cast<uint16_t>(data[off] | (data[off + 1] << 8)); };
    int xmin = rd16(4), ymin = rd16(6), xmax = rd16(8), ymax = rd16(10);
    int bpl = rd16(66);
    int w = xmax - xmin + 1;
    int h = ymax - ymin + 1;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || bpl < w) return false;

    out.width = w;
    out.height = h;
    out.indices.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    size_t src = 128;
    std::vector<uint8_t> scanline(static_cast<size_t>(bpl), 0);
    for (int y = 0; y < h && src < data.size(); ++y) {
        int x = 0;
        while (x < bpl && src < data.size()) {
            uint8_t v = data[src++];
            if ((v & 0xC0u) == 0xC0u) {
                int run = v & 0x3Fu;
                uint8_t val = (src < data.size()) ? data[src++] : 0;
                for (int r = 0; r < run && x < bpl; ++r) scanline[static_cast<size_t>(x++)] = val;
            } else {
                scanline[static_cast<size_t>(x++)] = v;
            }
        }
        std::memcpy(&out.indices[static_cast<size_t>(y) * static_cast<size_t>(w)], scanline.data(), static_cast<size_t>(w));
    }
    out.dirty = true;
    return true;
}

GLuint ModernRenderer::get_or_upload_pcx_texture(const std::string& pcx_name) {
    if (pcx_name.empty()) return 0;
    auto it = pcx_textures_.find(pcx_name);
    if (it == pcx_textures_.end()) {
        if (game_dir_.empty()) return 0;
        PcxTexture tex;
        fs::path p = fs::path(game_dir_) / pcx_name;
        if (!fs::exists(p)) {
            // Case-insensitive search in game_dir_
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(game_dir_, ec)) {
                if (to_upper_str(entry.path().filename().string()) == pcx_name) {
                    p = entry.path();
                    break;
                }
            }
        }
        if (!load_pcx_file(p.string(), tex)) {
            pcx_textures_[pcx_name] = PcxTexture{}; // Negative cache
            return 0;
        }
        it = pcx_textures_.emplace(pcx_name, std::move(tex)).first;
    }
    PcxTexture& tex = it->second;
    if (tex.width <= 0 || tex.height <= 0 || tex.indices.empty()) return 0;
    if (tex.gl_tex == 0) {
        glGenTextures(1, &tex.gl_tex);
        glBindTexture(GL_TEXTURE_2D, tex.gl_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        tex.dirty = true;
    }
    if (tex.dirty) {
        std::vector<uint8_t> rgba(static_cast<size_t>(tex.width) * static_cast<size_t>(tex.height) * 4);
        for (size_t i = 0; i < tex.indices.size(); ++i) {
            uint8_t c = tex.indices[i];
            rgba[i * 4 + 0] = static_cast<uint8_t>(std::clamp(vga_rgb_[c * 3 + 0] * 255.0f, 0.0f, 255.0f));
            rgba[i * 4 + 1] = static_cast<uint8_t>(std::clamp(vga_rgb_[c * 3 + 1] * 255.0f, 0.0f, 255.0f));
            rgba[i * 4 + 2] = static_cast<uint8_t>(std::clamp(vga_rgb_[c * 3 + 2] * 255.0f, 0.0f, 255.0f));
            rgba[i * 4 + 3] = 255;
        }
        glBindTexture(GL_TEXTURE_2D, tex.gl_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        tex.dirty = false;
    }
    return tex.gl_tex;
}

bool ModernRenderer::parse_3dz_file(const std::string& path, Model3dz& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 26) return false;

    auto rd16 = [&](size_t off) -> uint16_t { return static_cast<uint16_t>(b[off] | (b[off + 1] << 8)); };
    auto rds16 = [&](size_t off) -> int16_t { return static_cast<int16_t>(rd16(off)); };

    uint16_t rad = rd16(0);
    char tex_buf[15]{};
    std::memcpy(tex_buf, b.data() + 2, 14);
    out.name = to_upper_str(fs::path(path).filename().string());
    out.tex_pcx_name = to_upper_str(std::string(tex_buf));
    out.bound_radius = static_cast<float>(rad) * 0.08f;

    uint16_t matnum = rd16(16);
    uint16_t ptsnum = rd16(18);
    uint16_t srfnum = rd16(20);
    if (ptsnum == 0 || ptsnum > 600 || srfnum == 0 || srfnum > 600) return false;

    size_t pos = 22;
    if (matnum > 1) pos += static_cast<size_t>(matnum - 1) * 8; // matdef
    pos += static_cast<size_t>(srfnum) * 12;                     // srfdef
    pos += 2 + static_cast<size_t>(srfnum) * 2;                  // root + bspnodes
    if (pos + static_cast<size_t>(ptsnum) * 8 + static_cast<size_t>(srfnum) * 2 > b.size()) return false;

    // Parse vertices (ptdef: int16 x, y, z, mat). Convert MicroProse local axes to OpenGL (+X right, +Y up, -Z fwd)
    constexpr float kModelScale = 0.055f;
    out.verts.resize(ptsnum);
    for (uint16_t i = 0; i < ptsnum; ++i) {
        int16_t vx = rds16(pos + i * 8 + 0);
        int16_t vy = rds16(pos + i * 8 + 2);
        int16_t vz = rds16(pos + i * 8 + 4);
        out.verts[i] = {static_cast<float>(vx) * kModelScale,
                        static_cast<float>(vy) * kModelScale,
                        static_cast<float>(-vz) * kModelScale};
    }
    pos += static_cast<size_t>(ptsnum) * 8;

    // Parse surface GOP command streams (deduplicated by byte offset)
    std::unordered_set<uint16_t> visited_offs;
    for (uint16_t s = 0; s < srfnum; ++s) {
        uint16_t gop_off = rd16(pos + s * 2);
        if (gop_off >= b.size() || !visited_offs.insert(gop_off).second) continue;
        size_t p = gop_off;
        while (p < b.size()) {
            uint8_t cmd = b[p];
            if (cmd == 0xFF) break;
            bool skip_anim = false;
            if (cmd & 0x80u) {
                if (p + 2 >= b.size()) break;
                uint8_t anim_val = b[p + 1];
                skip_anim = (anim_val != 0); // Render default flight-state geometry (val==0)
                p += 2;
                cmd = b[p];
            }
            uint8_t op = cmd & 0x7Fu;
            p++;
            if (op == 0) { p += 2; } // GPDOT
            else if (op == 1) { p += 3; } // GPLINE
            else if (op == 2) { p += 2; } // GPOBJ
            else if (op == 3) { // GPPLY
                if (p >= b.size()) break;
                uint8_t n = b[p++];
                if (p + n + 1 > b.size()) break;
                if (!skip_anim && n >= 3 && n <= 24) {
                    Model3dz::Face face;
                    face.textured = false;
                    face.verts.reserve(n);
                    bool ok = true;
                    for (uint8_t k = 0; k < n; ++k) {
                        uint8_t vi = b[p + k];
                        if (vi >= ptsnum) { ok = false; break; }
                        face.verts.push_back(vi);
                    }
                    face.color_idx = b[p + n];
                    if (ok) out.faces.push_back(std::move(face));
                }
                p += n + 1;
            } else if (op == 4) { // GPGRDPLY
                if (p >= b.size()) break;
                uint8_t n = b[p++];
                if (p + n * 2 > b.size()) break;
                if (!skip_anim && n >= 3 && n <= 24) {
                    Model3dz::Face face;
                    face.textured = false;
                    face.color_idx = b[p + 1];
                    bool ok = true;
                    for (uint8_t k = 0; k < n; ++k) {
                        uint8_t vi = b[p + k * 2];
                        if (vi >= ptsnum) { ok = false; break; }
                        face.verts.push_back(vi);
                    }
                    if (ok) out.faces.push_back(std::move(face));
                }
                p += n * 2;
            } else if (op == 5) { // GPTXTRPLY
                if (p >= b.size()) break;
                uint8_t n = b[p++];
                if (p + n * 3 + 1 > b.size()) break;
                if (!skip_anim && n >= 3 && n <= 24) {
                    Model3dz::Face face;
                    face.textured = !out.tex_pcx_name.empty();
                    face.verts.reserve(n);
                    face.u.reserve(n);
                    face.v.reserve(n);
                    bool ok = true;
                    for (uint8_t k = 0; k < n; ++k) {
                        uint8_t vi = b[p + k * 3 + 0];
                        uint8_t tu = b[p + k * 3 + 1];
                        uint8_t tv = b[p + k * 3 + 2];
                        if (vi >= ptsnum) { ok = false; break; }
                        face.verts.push_back(vi);
                        face.u.push_back(static_cast<float>(tu) / 256.0f);
                        face.v.push_back(static_cast<float>(tv) / 200.0f);
                    }
                    face.color_idx = b[p + n * 3];
                    if (ok) out.faces.push_back(std::move(face));
                }
                p += n * 3 + 1;
            } else {
                break;
            }
        }
    }

    // Compute per-face normals for subtle retro directional shading
    for (auto& face : out.faces) {
        if (face.verts.size() < 3) continue;
        const auto& v0 = out.verts[face.verts[0]];
        const auto& v1 = out.verts[face.verts[1]];
        const auto& v2 = out.verts[face.verts[2]];
        float ax = v1.x - v0.x, ay = v1.y - v0.y, az = v1.z - v0.z;
        float bx = v2.x - v0.x, by = v2.y - v0.y, bz = v2.z - v0.z;
        float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-6f) face.normal = {nx / len, ny / len, nz / len};
    }
    return !out.faces.empty();
}

bool ModernRenderer::load_3dz_directory(const std::string& game_dir) {
    game_dir_ = game_dir;
    models_3dz_.clear();
    std::error_code ec;
    if (!fs::exists(game_dir, ec)) return false;
    for (const auto& entry : fs::directory_iterator(game_dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (to_upper_str(entry.path().extension().string()) == ".3DZ") {
            Model3dz m;
            if (parse_3dz_file(entry.path().string(), m)) {
                models_3dz_[m.name] = std::move(m);
            }
        }
    }
    return !models_3dz_.empty();
}

void ModernRenderer::set_vga_palette(const uint8_t* dac_768) {
    if (!dac_768) return;
    bool changed = !has_vga_pal_ || (std::memcmp(vga_dac_raw_.data(), dac_768, 768) != 0);
    if (!changed) return;
    std::memcpy(vga_dac_raw_.data(), dac_768, 768);
    for (int i = 0; i < 256 * 3; ++i) {
        vga_rgb_[i] = static_cast<float>(dac_768[i]) / 63.0f;
    }
    has_vga_pal_ = true;
    for (auto& kv : pcx_textures_) kv.second.dirty = true;
    cockpit_3d_.sync_palette(dac_768);
    update_scene_lighting();
}

void ModernRenderer::update_scene_lighting() {
    if (vr_cfg_.lighting_mode == LightingMode::Day) {
        lit_.daylight = 1.0f; lit_.night_amount = 0.0f; lit_.sunset_amount = 0.0f;
    } else if (vr_cfg_.lighting_mode == LightingMode::Dusk) {
        lit_.daylight = 0.50f; lit_.night_amount = 0.25f; lit_.sunset_amount = 0.85f;
    } else if (vr_cfg_.lighting_mode == LightingMode::Night) {
        lit_.daylight = 0.14f; lit_.night_amount = 1.0f; lit_.sunset_amount = 0.0f;
    } else if (has_vga_pal_) {
        float avg = 0.0f;
        for (int i = 0x80; i <= 0x88; ++i) {
            avg += (vga_rgb_[i * 3 + 0] + vga_rgb_[i * 3 + 1] + vga_rgb_[i * 3 + 2]) / 3.0f;
        }
        avg /= 9.0f;
        lit_.daylight = std::clamp(avg * 1.6f, 0.14f, 1.0f);
        lit_.night_amount = std::clamp((0.35f - avg) * 3.0f, 0.0f, 1.0f);
        lit_.sunset_amount = std::clamp(1.0f - std::abs(lit_.daylight - 0.52f) * 3.0f, 0.0f, 1.0f) * (1.0f - lit_.night_amount);
    }

    float d = lit_.daylight, s = lit_.sunset_amount;
    lit_.sky_top[0] = 0.04f + 0.13f * d + 0.18f * s;
    lit_.sky_top[1] = 0.06f + 0.40f * d;
    lit_.sky_top[2] = 0.12f + 0.68f * d - 0.28f * s;
    lit_.horizon[0] = 0.08f + 0.60f * d + 0.28f * s;
    lit_.horizon[1] = 0.10f + 0.69f * d - 0.06f * s;
    lit_.horizon[2] = 0.15f + 0.73f * d - 0.35f * s;
    // Match authentic DOS Fleet Defender North Atlantic slate-teal-blue water (R~0.15, G~0.34, B~0.46)
    lit_.ocean_rgb[0] = 0.02f + 0.13f * d + 0.07f * s;
    lit_.ocean_rgb[1] = 0.05f + 0.29f * d;
    lit_.ocean_rgb[2] = 0.09f + 0.37f * d - 0.10f * s;
    lit_.ground_mul[0] = 0.22f + 0.78f * d + 0.12f * s;
    lit_.ground_mul[1] = 0.22f + 0.78f * d;
    lit_.ground_mul[2] = 0.25f + 0.75f * d - 0.15f * s;
}

float ModernRenderer::compute_smart_scale(float slant_km, bool is_ground) const {
    if (vr_cfg_.smart_scaling_mode == SmartScalingMode::Off) return 1.0f;
    if (is_ground && !vr_cfg_.smart_scale_ground) return 1.0f;
    float d = std::max(0.0f, slant_km);
    float s = 1.0f;
    switch (vr_cfg_.smart_scaling_mode) {
        case SmartScalingMode::Serfoss2003: s = 1.0f + 0.09f * d + 0.0045f * d * d; break;
        case SmartScalingMode::Why485FovRes: {
            float res_factor = std::clamp(60.0f / std::max(12.0f, last_display_ppd_), 0.6f, 2.5f);
            s = 1.0f + std::pow(std::max(0.0f, d - 0.4f), 0.75f) * 0.38f * res_factor;
            break;
        }
        case SmartScalingMode::LegacyLinear: s = 1.0f + 0.20f * d; break;
        default: break;
    }
    s = 1.0f + (s - 1.0f) * std::clamp(vr_cfg_.aircraft_scale, 0.25f, 3.0f);
    return std::clamp(s, 1.0f, is_ground ? 3.5f : 6.0f);
}

void ModernRenderer::update_telemetry(const FlightTelemetry& raw, float dt_sec) {
    last_dt_sec_ = std::clamp(dt_sec, 0.001f, 0.05f);
    sim_time_sec_ += last_dt_sec_;

    // Update persistent missile smoke trail ribbons
    for (auto& kv : missile_trails_) {
        for (auto& pt : kv.second) pt.age += last_dt_sec_;
        kv.second.erase(std::remove_if(kv.second.begin(), kv.second.end(),
                                       [&](const MissileTrailPt& pt) { return pt.age > vr_cfg_.missile_trail_lifetime; }),
                        kv.second.end());
    }
    for (const auto& m : raw.missiles) {
        auto& tr = missile_trails_[m.slot_id];
        if (tr.empty() || std::abs(tr.back().wx - m.world_x) + std::abs(tr.back().wy - m.world_y) > 2.0) {
            tr.push_back({m.world_x, m.world_y, m.world_z, 0.0f});
            if (tr.size() > 140) tr.erase(tr.begin());
        }
    }

    for (auto& fx : world_fx_) {
        fx.age += last_dt_sec_;
        fx.wx += fx.vx * last_dt_sec_;
        fx.wy += fx.vy * last_dt_sec_;
        fx.wz += fx.vz * last_dt_sec_;
        if (fx.kind == WorldFx::Kind::Flare) fx.vz -= 25.0 * last_dt_sec_;
    }
    world_fx_.erase(std::remove_if(world_fx_.begin(), world_fx_.end(),
                                   [](const WorldFx& fx) { return fx.age >= fx.max_age; }),
                    world_fx_.end());

    if (!raw.in_flight) { smooth_tel_ = raw; has_smooth_tel_ = false; return; }
    if (!has_smooth_tel_ || vr_cfg_.smooth_rate <= 0.0f) { smooth_tel_ = raw; has_smooth_tel_ = true; return; }

    float a = 1.0f - std::exp(-vr_cfg_.smooth_rate * last_dt_sec_);
    auto lerp_ang = [](float cur, float tgt, float t) {
        float diff = std::fmod(tgt - cur + 540.0f, 360.0f) - 180.0f;
        return cur + diff * t;
    };
    smooth_tel_ = raw;
    smooth_tel_.cam_yaw_deg = lerp_ang(smooth_tel_.cam_yaw_deg, raw.cam_yaw_deg, a);
    smooth_tel_.cam_pitch_deg += (raw.cam_pitch_deg - smooth_tel_.cam_pitch_deg) * a;
    smooth_tel_.cam_roll_deg = lerp_ang(smooth_tel_.cam_roll_deg, raw.cam_roll_deg, a);
}

void ModernRenderer::draw_vga_quad(int x, int y, int w, int h, GLuint tex,
                                   float v_min, float v_max, bool alpha_blend) {
    glViewport(x, y, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    if (alpha_blend) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    else glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, v_min); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, v_min); glVertex2f(1.0f, 0.0f);
    glTexCoord2f(1.0f, v_max); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, v_max); glVertex2f(0.0f, 1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void ModernRenderer::draw_sky_backdrop(float pitch_deg, float roll_deg, float horizon_ndc_y, float vfov_deg, float aspect) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float pitch_ndc = -std::tan(pitch_deg * kDegToRad) / std::max(0.15f, std::tan(vfov_deg * 0.5f * kDegToRad));
    glTranslatef(0.0f, horizon_ndc_y, 0.0f);
    glRotatef(-roll_deg, 0.0f, 0.0f, 1.0f);
    glTranslatef(0.0f, pitch_ndc, 0.0f);

    float ext = 6.0f;
    glBegin(GL_QUADS);
    glColor3fv(lit_.sky_top); glVertex2f(-ext, ext); glVertex2f(ext, ext);
    glColor3fv(lit_.horizon); glVertex2f( ext, 0.0f); glVertex2f(-ext, 0.0f);
    glColor3fv(lit_.horizon); glVertex2f(-ext, 0.0f); glVertex2f(ext, 0.0f);
    glColor3fv(lit_.ocean_rgb); glVertex2f( ext, -ext); glVertex2f(-ext, -ext);
    glEnd();
}

void ModernRenderer::draw_night_stars() {
    if (lit_.night_amount < 0.25f) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    uint32_t rng = 0x51A7F14u;
    for (int i = 0; i < 160; ++i) {
        rng = rng * 1664525u + 1013904223u;
        float az = static_cast<float>(rng & 0xFFFFu) * (2.0f * kPi / 65536.0f);
        rng = rng * 1664525u + 1013904223u;
        float el = 0.08f + static_cast<float>(rng & 0xFFFFu) * (1.42f / 65536.0f);
        float br = (0.45f + 0.55f * ((i * 37) % 100) / 100.0f) * lit_.night_amount;
        glColor3f(br, br, br * 1.05f);
        float r = 60000.0f;
        glVertex3f(r * std::cos(el) * std::sin(az), r * std::sin(el), -r * std::cos(el) * std::cos(az));
    }
    glEnd();
    glPointSize(1.0f);
    glEnable(GL_DEPTH_TEST);
}

GLuint ModernRenderer::ensure_ocean_texture() {
    if (ocean_tex_ != 0) return ocean_tex_;
    constexpr int kSz = 512;
    std::vector<uint8_t> rgba(kSz * kSz * 4, 255);

    auto hash2 = [](int x, int y, int seed) -> float {
        uint32_t h = static_cast<uint32_t>(x * 374761393 + y * 668265263 + seed * 1442695041);
        h = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
    };
    auto vnoise = [&](float u, float v, int period, int seed) -> float {
        float fx = u * period;
        float fy = v * period;
        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        float tx = fx - x0;
        float ty = fy - y0;
        tx = tx * tx * (3.0f - 2.0f * tx);
        ty = ty * ty * (3.0f - 2.0f * ty);
        int ix0 = ((x0 % period) + period) % period;
        int iy0 = ((y0 % period) + period) % period;
        int ix1 = (ix0 + 1) % period;
        int iy1 = (iy0 + 1) % period;
        float a = hash2(ix0, iy0, seed);
        float b = hash2(ix1, iy0, seed);
        float c = hash2(ix0, iy1, seed);
        float d = hash2(ix1, iy1, seed);
        return (a + (b - a) * tx) * (1.0f - ty) + (c + (d - c) * tx) * ty;
    };

    // Option B: Enhanced Retro DOS maritime facets (pure organic multi-octave fBm, ZERO sine-wave waffle grid!)
    std::vector<float> hmap(kSz * kSz, 0.0f);
    for (int y = 0; y < kSz; ++y) {
        float v = static_cast<float>(y) / static_cast<float>(kSz);
        for (int x = 0; x < kSz; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(kSz);
            // Gentle domain warp so wave clusters feel completely organic & non-repeating
            float wu = u + vnoise(u, v, 4, 7) * 0.08f;
            float wv = v + vnoise(u, v, 4, 13) * 0.08f;
            hmap[y * kSz + x] = 0.35f * vnoise(wu, wv, 8, 21)
                              + 0.30f * vnoise(wu, wv, 16, 22)
                              + 0.20f * vnoise(wu, wv, 32, 23)
                              + 0.15f * vnoise(wu, wv, 64, 24);
        }
    }

    for (int y = 0; y < kSz; ++y) {
        int ym = (y - 1 + kSz) % kSz;
        int yp = (y + 1) % kSz;
        for (int x = 0; x < kSz; ++x) {
            int xm = (x - 1 + kSz) % kSz;
            int xp = (x + 1) % kSz;

            float h = hmap[y * kSz + x];
            float dx = hmap[y * kSz + xp] - hmap[y * kSz + xm];
            float dy = hmap[yp * kSz + x] - hmap[ym * kSz + x];
            float lit = std::clamp(0.50f + (-dx * 2.4f - dy * 2.8f), 0.20f, 0.92f);
            // Gently quantized into 9 crisp retro-DOS maritime tonal bands (harmonizes with pixel-art cockpit!)
            float q = std::floor((h * 0.60f + lit * 0.40f) * 9.0f) / 9.0f;
            float base = std::clamp(0.72f + 0.50f * q, 0.68f, 1.00f);

            // Subtle specular sparkle baked into the brightest sunlit wave crests
            float glint = std::pow(std::clamp((-dx * 3.8f - dy * 4.2f), 0.0f, 1.0f), 3.2f) * 0.18f;
            float r = std::clamp(base * 0.97f + glint, 0.0f, 1.0f);
            float g = std::clamp(base * 0.99f + glint, 0.0f, 1.0f);
            float b = std::clamp(base * 1.01f + glint, 0.0f, 1.0f);

            size_t idx = static_cast<size_t>((y * kSz + x) * 4);
            rgba[idx + 0] = static_cast<uint8_t>(r * 255.0f);
            rgba[idx + 1] = static_cast<uint8_t>(g * 255.0f);
            rgba[idx + 2] = static_cast<uint8_t>(b * 255.0f);
            rgba[idx + 3] = 255;
        }
    }

    glGenTextures(1, &ocean_tex_);
    glBindTexture(GL_TEXTURE_2D, ocean_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Build full box-filtered mipmap pyramid on CPU (zero GL extension dependencies, silky anti-aliased horizon!)
    int level = 0;
    int mw = kSz, mh = kSz;
    std::vector<uint8_t> cur = std::move(rgba);
    while (true) {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, mw, mh, 0, GL_RGBA, GL_UNSIGNED_BYTE, cur.data());
        if (mw == 1 && mh == 1) break;
        int nw = std::max(1, mw / 2);
        int nh = std::max(1, mh / 2);
        std::vector<uint8_t> nxt(nw * nh * 4, 255);
        for (int y = 0; y < nh; ++y) {
            int sy0 = y * 2, sy1 = std::min(mh - 1, sy0 + 1);
            for (int x = 0; x < nw; ++x) {
                int sx0 = x * 2, sx1 = std::min(mw - 1, sx0 + 1);
                for (int c = 0; c < 4; ++c) {
                    int sum = cur[(sy0 * mw + sx0) * 4 + c] + cur[(sy0 * mw + sx1) * 4 + c]
                            + cur[(sy1 * mw + sx0) * 4 + c] + cur[(sy1 * mw + sx1) * 4 + c];
                    nxt[(y * nw + x) * 4 + c] = static_cast<uint8_t>(sum >> 2);
                }
            }
        }
        cur = std::move(nxt);
        mw = nw;
        mh = nh;
        ++level;
    }
    return ocean_tex_;
}

void ModernRenderer::draw_ocean_and_wld_terrain(const FlightTelemetry& tel) {
    glEnable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, vr_cfg_.wireframe_terrain ? GL_LINE : GL_FILL);

    double cam_x = tel.has_camera ? tel.cam_x : tel.world_x;
    double cam_y = tel.has_camera ? tel.cam_y : tel.world_y;
    float cam_alt_m = static_cast<float>((tel.has_camera ? tel.cam_alt_ft : tel.altitude_ft) * kFeetToMeters);

    // Base textured North Atlantic ocean with world-anchored swells, Fresnel horizon blend & fine low-alt chop!
    if (vr_cfg_.ocean_waves && !vr_cfg_.wireframe_terrain) {
        GLuint otex = ensure_ocean_texture();
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, otex);

        // Match authentic DOS Fleet Defender slate-teal-blue water directly from lit_.ocean_rgb
        float tr = std::clamp(lit_.ocean_rgb[0] * 1.12f, 0.03f, 0.92f);
        float tg = std::clamp(lit_.ocean_rgb[1] * 1.12f, 0.06f, 0.94f);
        float tb = std::clamp(lit_.ocean_rgb[2] * 1.12f, 0.10f, 0.96f);
        // Smooth Fresnel sky reflection toward the horizon
        float hr = std::clamp(lit_.horizon[0] * 0.78f + tr * 0.22f, 0.08f, 1.0f);
        float hg = std::clamp(lit_.horizon[1] * 0.78f + tg * 0.22f, 0.10f, 1.0f);
        float hb = std::clamp(lit_.horizon[2] * 0.78f + tb * 0.22f, 0.14f, 1.0f);

        // Layer 1: Broad organic retro-DOS ocean sheet (1:1 world-anchored at 1,600 m tile)
        constexpr float kMacroTile = 1600.0f;
        float drift_u = sim_time_sec_ * 0.0014f;
        float drift_v = sim_time_sec_ * 0.0009f;
        float base_u = static_cast<float>(std::fmod(cam_x * kWorldUnitToMeters, static_cast<double>(kMacroTile))) / kMacroTile + drift_u;
        float base_v = static_cast<float>(std::fmod(cam_y * kWorldUnitToMeters, static_cast<double>(kMacroTile))) / kMacroTile + drift_v;

        // World-fixed sun azimuth (South-West 215 deg) for natural directional sun-sheen path!
        constexpr float kSunDirX = -0.573576f; // sin(215 deg)
        constexpr float kSunDirZ =  0.819152f; // -cos(215 deg) in OpenGL (+Z = South)
        float sun_strength = (lit_.night_amount > 0.5f) ? 0.14f : (0.30f + 0.25f * lit_.sunset_amount);
        float sun_r = (lit_.sunset_amount > 0.3f) ? 1.00f : (lit_.night_amount > 0.5f ? 0.65f : 0.92f);
        float sun_g = (lit_.sunset_amount > 0.3f) ? 0.78f : (lit_.night_amount > 0.5f ? 0.74f : 0.96f);
        float sun_b = (lit_.sunset_amount > 0.3f) ? 0.52f : (lit_.night_amount > 0.5f ? 0.88f : 1.00f);

        constexpr int kGrid = 20;
        constexpr float kSeaR = 95000.0f;
        constexpr float kStep = (2.0f * kSeaR) / kGrid;
        glBegin(GL_QUADS);
        for (int gx = 0; gx < kGrid; ++gx) {
            float x0 = -kSeaR + gx * kStep;
            float x1 = x0 + kStep;
            for (int gz = 0; gz < kGrid; ++gz) {
                float z0 = -kSeaR + gz * kStep;
                float z1 = z0 + kStep;
                auto emit_sea = [&](float vx, float vz) {
                    float dist = std::sqrt(vx * vx + vz * vz);
                    float fresnel = std::clamp((dist - 12000.0f) / 60000.0f, 0.0f, 0.82f);
                    float r = tr * (1.0f - fresnel) + hr * fresnel;
                    float g = tg * (1.0f - fresnel) + hg * fresnel;
                    float b = tb * (1.0f - fresnel) + hb * fresnel;

                    // Subtle world-fixed directional sun-sheen band along the water toward the sun!
                    if (dist > 400.0f) {
                        float align = (vx * kSunDirX + vz * kSunDirZ) / dist;
                        if (align > 0.70f) {
                            float sband = std::pow((align - 0.70f) / 0.30f, 3.0f);
                            float dfade = (1.0f - fresnel * 0.7f) * std::clamp(dist / 3500.0f, 0.25f, 1.0f);
                            float sh = sband * dfade * sun_strength;
                            r = std::clamp(r + sun_r * sh, 0.0f, 1.0f);
                            g = std::clamp(g + sun_g * sh, 0.0f, 1.0f);
                            b = std::clamp(b + sun_b * sh, 0.0f, 1.0f);
                        }
                    }

                    glColor3f(r, g, b);
                    glTexCoord2f(vx / kMacroTile + base_u, vz / kMacroTile + base_v);
                    glVertex3f(vx, -cam_alt_m, vz);
                };
                emit_sea(x0, z0);
                emit_sea(x1, z0);
                emit_sea(x1, z1);
                emit_sea(x0, z1);
            }
        }
        glEnd();

        // Layer 2: Rotated irrational-ratio facet overlay (590 m @ 38.4 deg) — completely eliminates
        // tile repetition at all altitudes while giving crisp depth & speed cues on approach!
        {
            constexpr float kDetailTile = 590.0f;
            constexpr float kCosR = 0.783693f; // cos(38.4 deg)
            constexpr float kSinR = 0.621148f;
            double wx_m = cam_x * kWorldUnitToMeters;
            double wz_m = cam_y * kWorldUnitToMeters;
            float du0 = static_cast<float>(std::fmod(wx_m * kCosR - wz_m * kSinR, static_cast<double>(kDetailTile))) / kDetailTile - sim_time_sec_ * 0.0022f;
            float dv0 = static_cast<float>(std::fmod(wx_m * kSinR + wz_m * kCosR, static_cast<double>(kDetailTile))) / kDetailTile + sim_time_sec_ * 0.0017f;

            float detail_vis = std::clamp(0.46f - cam_alt_m / 28000.0f, 0.22f, 0.46f);
            constexpr int kDGrid = 14;
            constexpr float kDetailR = 26000.0f;
            constexpr float kDStep = (2.0f * kDetailR) / kDGrid;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -1.0f);
            glBegin(GL_QUADS);
            for (int gx = 0; gx < kDGrid; ++gx) {
                float x0 = -kDetailR + gx * kDStep;
                float x1 = x0 + kDStep;
                for (int gz = 0; gz < kDGrid; ++gz) {
                    float z0 = -kDetailR + gz * kDStep;
                    float z1 = z0 + kDStep;
                    auto emit_det = [&](float vx, float vz) {
                        float dist = std::sqrt(vx * vx + vz * vz);
                        float fade = 1.0f - std::clamp((dist - kDetailR * 0.35f) / (kDetailR * 0.65f), 0.0f, 1.0f);
                        float u = (vx * kCosR - vz * kSinR) / kDetailTile + du0;
                        float v = (vx * kSinR + vz * kCosR) / kDetailTile + dv0;
                        float sh = 0.0f;
                        if (dist > 300.0f) {
                            float align = (vx * kSunDirX + vz * kSunDirZ) / dist;
                            if (align > 0.72f) sh = std::pow((align - 0.72f) / 0.28f, 2.5f) * sun_strength * 0.75f;
                        }
                        glColor4f(std::clamp(tr * 1.06f + sun_r * sh, 0.0f, 1.0f),
                                  std::clamp(tg * 1.06f + sun_g * sh, 0.0f, 1.0f),
                                  std::clamp(tb * 1.06f + sun_b * sh, 0.0f, 1.0f),
                                  detail_vis * fade);
                        glTexCoord2f(u, v);
                        glVertex3f(vx, -cam_alt_m, vz);
                    };
                    emit_det(x0, z0);
                    emit_det(x1, z0);
                    emit_det(x1, z1);
                    emit_det(x0, z1);
                }
            }
            glEnd();
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
        glDisable(GL_TEXTURE_2D);
    } else {
        glColor3fv(lit_.ocean_rgb);
        float sea_r = 95000.0f;
        glBegin(GL_QUADS);
        glVertex3f(-sea_r, -cam_alt_m, -sea_r);
        glVertex3f( sea_r, -cam_alt_m, -sea_r);
        glVertex3f( sea_r, -cam_alt_m,  sea_r);
        glVertex3f(-sea_r, -cam_alt_m,  sea_r);
        glEnd();
    }

    if (has_wld_ && wld_hmap_.size() == 65536 && wld_bmap_.size() == 65536) {
        double cell_m = 4096.0 * kWorldUnitToMeters;
        int cx = std::clamp(static_cast<int>(cam_x / 4096.0), 0, 255);
        int cy = std::clamp(static_cast<int>(cam_y / 4096.0), 0, 255);
        int rad = 18;
        auto sample_b = [&](int gx, int gy) -> uint8_t {
            gx = std::clamp(gx, 0, 255);
            gy = std::clamp(gy, 0, 255);
            return wld_bmap_[gy * 256 + gx];
        };
        auto sample_h = [&](int gx, int gy) -> float {
            gx = std::clamp(gx, 0, 255);
            gy = std::clamp(gy, 0, 255);
            float h = static_cast<float>(wld_hmap_[gy * 256 + gx]) * 24.0f * vr_cfg_.terrain_scale;
            // Flat coastal land / islands / airfields (bmap != 255) sit slightly above sea level!
            if (h <= 0.5f && wld_bmap_[gy * 256 + gx] != 255) h = 2.5f;
            return h;
        };

        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-2.0f, -2.0f);
        glBegin(GL_TRIANGLES);
        for (int gy = std::max(0, cy - rad); gy < std::min(255, cy + rad); ++gy) {
            for (int gx = std::max(0, cx - rad); gx < std::min(255, cx + rad); ++gx) {
                uint8_t b00 = sample_b(gx, gy), b10 = sample_b(gx + 1, gy);
                uint8_t b01 = sample_b(gx, gy + 1), b11 = sample_b(gx + 1, gy + 1);
                float h00 = sample_h(gx, gy), h10 = sample_h(gx + 1, gy);
                float h01 = sample_h(gx, gy + 1), h11 = sample_h(gx + 1, gy + 1);
                bool any_land = (b00 != 255 || b10 != 255 || b01 != 255 || b11 != 255 ||
                                 h00 > 1.0f || h10 > 1.0f || h01 > 1.0f || h11 > 1.0f);
                if (!any_land) continue;
                bool coastal_edge = (b00 == 255 || b10 == 255 || b01 == 255 || b11 == 255);

                float x0 = static_cast<float>((gx * 4096.0 - cam_x) * kWorldUnitToMeters);
                float x1 = x0 + static_cast<float>(cell_m);
                float z0 = static_cast<float>((gy * 4096.0 - cam_y) * kWorldUnitToMeters);
                float z1 = z0 + static_cast<float>(cell_m);

                float avg_h = (h00 + h10 + h01 + h11) * 0.25f;
                float snow = std::clamp((avg_h - 1100.0f) / 1100.0f, 0.0f, 1.0f);
                float slope = std::clamp(0.84f + (h10 - h00) * 0.00055f + (h01 - h00) * 0.00035f, 0.52f, 1.18f);

                // Biome-aware coloring: arid desert (Med bmap>=40), airbases/cities (32..35), coastal sand, lush plains, snow peaks
                float br = 0.22f, bg = 0.36f, bb = 0.20f;
                if (b00 >= 40 && b00 < 96) {
                    br = 0.56f; bg = 0.48f; bb = 0.34f; // Arid Mediterranean / desert land
                } else if (b00 == 32 || b00 == 33) {
                    br = 0.32f; bg = 0.36f; bb = 0.30f; // City / airbase tarmac & concrete
                } else if (coastal_edge && avg_h < 35.0f) {
                    br = 0.42f; bg = 0.44f; bb = 0.32f; // Coastal shore / beach / cliff
                }
                glColor3f((br + (0.86f - br) * snow) * lit_.ground_mul[0] * slope,
                          (bg + (0.88f - bg) * snow) * lit_.ground_mul[1] * slope,
                          (bb + (0.92f - bb) * snow) * lit_.ground_mul[2] * slope);

                glVertex3f(x0, h00 - cam_alt_m, z0);
                glVertex3f(x1, h10 - cam_alt_m, z0);
                glVertex3f(x1, h11 - cam_alt_m, z1);

                glVertex3f(x0, h00 - cam_alt_m, z0);
                glVertex3f(x1, h11 - cam_alt_m, z1);
                glVertex3f(x0, h01 - cam_alt_m, z1);
            }
        }
        glEnd();
        glDisable(GL_POLYGON_OFFSET_FILL);

        // Render 3D Ground Objects (Runways, Hangars, Towers, Cities, SAM Sites) from WLD OBJECTS[96][16]
        int obj_rad = 6; // ~30 km radius
        for (int gy = std::max(0, cy - obj_rad); gy < std::min(255, cy + obj_rad); ++gy) {
            for (int gx = std::max(0, cx - obj_rad); gx < std::min(255, cx + obj_rad); ++gx) {
                uint8_t grp = sample_b(gx, gy);
                if (grp >= 96) continue;
                const auto& slot_list = wld_objects_[grp];
                float base_h = sample_h(gx, gy);
                for (const auto& obj : slot_list) {
                    if (obj.num < 0 || obj.num >= 32) continue;
                    const std::string& mname = wld_obj_names_[obj.num];
                    if (mname.empty()) continue;
                    auto mit = models_3dz_.find(mname);
                    if (mit == models_3dz_.end()) continue;

                    double ox_w = gx * 4096.0 + static_cast<double>(obj.x) * 32.0;
                    double oz_w = gy * 4096.0 + static_cast<double>(obj.y) * 32.0;
                    float dx = static_cast<float>((ox_w - cam_x) * kWorldUnitToMeters);
                    float dz = static_cast<float>((oz_w - cam_y) * kWorldUnitToMeters);
                    float dist_km = std::sqrt(dx * dx + dz * dz) * 0.001f;
                    if (dist_km > 28.0f) continue;

                    float scale = (wld_obj_scales_[obj.num] < 0) ? 1.8f : 0.45f * std::max(1, static_cast<int>(wld_obj_scales_[obj.num]));
                    glPushMatrix();
                    glTranslatef(dx, base_h - cam_alt_m, dz);
                    draw_3dz_model(mit->second, scale, lit_.daylight);
                    glPopMatrix();
                }
            }
        }
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

GLuint ModernRenderer::ensure_cloud_texture() {
    if (cloud_tex_ != 0) return cloud_tex_;
    constexpr int kSz = 256;
    std::vector<uint8_t> rgba(kSz * kSz * 4, 0);

    // Deterministic seamless tileable value-noise helper (period p)
    auto hash2 = [](int x, int y, int seed) -> float {
        uint32_t h = static_cast<uint32_t>(x * 374761393 + y * 668265263 + seed * 1442695041);
        h = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
    };
    auto vnoise = [&](float u, float v, int period, int seed) -> float {
        float fx = u * period;
        float fy = v * period;
        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        float tx = fx - x0;
        float ty = fy - y0;
        tx = tx * tx * (3.0f - 2.0f * tx);
        ty = ty * ty * (3.0f - 2.0f * ty);
        int ix0 = ((x0 % period) + period) % period;
        int iy0 = ((y0 % period) + period) % period;
        int ix1 = (ix0 + 1) % period;
        int iy1 = (iy0 + 1) % period;
        float a = hash2(ix0, iy0, seed);
        float b = hash2(ix1, iy0, seed);
        float c = hash2(ix0, iy1, seed);
        float d = hash2(ix1, iy1, seed);
        return (a + (b - a) * tx) * (1.0f - ty) + (c + (d - c) * tx) * ty;
    };

    for (int y = 0; y < kSz; ++y) {
        float v = static_cast<float>(y) / static_cast<float>(kSz);
        for (int x = 0; x < kSz; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(kSz);
            // 5-octave seamless fBm for billowing maritime stratocumulus clouds
            float n = 0.0f;
            n += 0.45f * vnoise(u, v, 4, 11);
            n += 0.25f * vnoise(u, v, 8, 29);
            n += 0.15f * vnoise(u, v, 16, 53);
            n += 0.10f * vnoise(u, v, 32, 97);
            n += 0.05f * vnoise(u, v, 64, 151);

            // Distinct cumulus cloud banks with crisp blue-sky breaks & sculpted sunlit tops vs cool shaded edges
            float density = std::clamp((n - 0.41f) / 0.33f, 0.0f, 1.0f);
            density = density * density * (3.0f - 2.0f * density);
            float core = std::clamp((n - 0.48f) / 0.30f, 0.0f, 1.0f);
            float r_f = 0.64f + 0.36f * core;
            float g_f = 0.71f + 0.29f * core;
            float b_f = 0.80f + 0.19f * core;
            uint8_t r = static_cast<uint8_t>(std::clamp(r_f * 255.0f, 0.0f, 255.0f));
            uint8_t g = static_cast<uint8_t>(std::clamp(g_f * 255.0f, 0.0f, 255.0f));
            uint8_t b = static_cast<uint8_t>(std::clamp(b_f * 255.0f, 0.0f, 255.0f));
            uint8_t a = static_cast<uint8_t>(std::clamp(density * 255.0f, 0.0f, 255.0f));

            size_t idx = static_cast<size_t>((y * kSz + x) * 4);
            rgba[idx + 0] = r;
            rgba[idx + 1] = g;
            rgba[idx + 2] = b;
            rgba[idx + 3] = a;
        }
    }

    glGenTextures(1, &cloud_tex_);
    glBindTexture(GL_TEXTURE_2D, cloud_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSz, kSz, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return cloud_tex_;
}

void ModernRenderer::draw_clouds(const FlightTelemetry& tel) {
    if (!vr_cfg_.show_clouds) return;
    double cam_x = tel.has_camera ? tel.cam_x : tel.world_x;
    double cam_y = tel.has_camera ? tel.cam_y : tel.world_y;
    float cam_alt_m = static_cast<float>((tel.has_camera ? tel.cam_alt_ft : tel.altitude_ft) * kFeetToMeters);

    // Sync cloud deck altitude to F14.EXE's CLOUDHEIGHT (clamped to realistic maritime deck 6,500..15,000 ft
    // so carrier ops at sea level stay clear while high-altitude flight flies over the classic cloud blanket)
    float deck_ft = (tel.cloud_deck_ft >= 2500.0f) ? std::clamp(tel.cloud_deck_ft, 2500.0f, 15500.0f) : 6500.0f;
    float base_y = deck_ft * kFeetToMeters - cam_alt_m;
    bool looking_down = (cam_alt_m >= deck_ft * kFeetToMeters);

    GLuint tex = ensure_cloud_texture();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    // Darker stormy cloud undersides when below an overcast deck; bright sunlit tops when above
    float underside_dim = (!looking_down && tel.overcast) ? 0.68f : (!looking_down ? 0.85f : 1.0f);
    float cr = std::clamp(lit_.cloud_rgb[0] * (0.30f + 0.70f * lit_.daylight) * underside_dim, 0.0f, 1.0f);
    float cg = std::clamp(lit_.cloud_rgb[1] * (0.30f + 0.70f * lit_.daylight) * underside_dim, 0.0f, 1.0f);
    float cb = std::clamp(lit_.cloud_rgb[2] * (0.34f + 0.66f * lit_.daylight) * underside_dim, 0.0f, 1.0f);

    // World-anchored UV offset at 1:1 true airspeed (kWorldUnitToMeters = 1.2192 m)
    constexpr float kTileMeters = 11000.0f;
    float uv_off_x = static_cast<float>(std::fmod(cam_x * kWorldUnitToMeters, static_cast<double>(kTileMeters))) / kTileMeters;
    float uv_off_z = static_cast<float>(std::fmod(cam_y * kWorldUnitToMeters, static_cast<double>(kTileMeters))) / kTileMeters;

    // Three softly stacked volumetric cloud slices (base, mid, sunlit puff tops) so you feel true 3D thickness
    // as you climb above, dive into, or break out underneath the cloud deck!
    struct CloudLayer { float dy_m; float alpha; float uv_scale; float uv_shift; float shade; };
    CloudLayer layers[3] = {
        {   0.0f, tel.overcast ? 0.92f : 0.84f, 1.00f, 0.00f, looking_down ? 0.86f : 0.82f },
        { 220.0f, tel.overcast ? 0.78f : 0.66f, 0.84f, 0.29f, looking_down ? 0.96f : 0.93f },
        { 460.0f, tel.overcast ? 0.62f : 0.50f, 0.68f, 0.61f, 1.00f },
    };

    constexpr int kGrid = 14;
    constexpr float kExtent = 78000.0f;
    constexpr float kStep = (2.0f * kExtent) / kGrid;

    for (const auto& ly : layers) {
        float y = base_y + ly.dy_m;
        // Gently fade a slice right as the camera passes through it to avoid a harsh flat-plane pop
        float pass_fade = std::clamp(std::abs(y) / 95.0f, 0.12f, 1.0f);
        float lr = std::clamp(cr * ly.shade, 0.0f, 1.0f);
        float lg = std::clamp(cg * ly.shade, 0.0f, 1.0f);
        float lb = std::clamp(cb * ly.shade, 0.0f, 1.0f);
        glBegin(GL_QUADS);
        for (int gx = 0; gx < kGrid; ++gx) {
            float x0 = -kExtent + gx * kStep;
            float x1 = x0 + kStep;
            for (int gz = 0; gz < kGrid; ++gz) {
                float z0 = -kExtent + gz * kStep;
                float z1 = z0 + kStep;

                auto emit = [&](float vx, float vz) {
                    float dist = std::sqrt(vx * vx + vz * vz);
                    float edge_fade = 1.0f - std::clamp((dist - kExtent * 0.52f) / (kExtent * 0.48f), 0.0f, 1.0f);
                    float a = ly.alpha * edge_fade * pass_fade;
                    float u = (vx / kTileMeters + uv_off_x) * ly.uv_scale + ly.uv_shift;
                    float v = (vz / kTileMeters + uv_off_z) * ly.uv_scale + ly.uv_shift * 0.7f;
                    glColor4f(lr, lg, lb, a);
                    glTexCoord2f(u, v);
                    glVertex3f(vx, y, vz);
                };
                emit(x0, z0);
                emit(x1, z0);
                emit(x1, z1);
                emit(x0, z1);
            }
        }
        glEnd();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

void ModernRenderer::draw_3dz_model(const Model3dz& m, float scale, float daylight) {
    glPushMatrix();
    glScalef(scale, scale, scale);
    GLuint tex = m.tex_pcx_name.empty() ? 0 : get_or_upload_pcx_texture(m.tex_pcx_name);
    float shade_base = 0.25f + 0.75f * daylight;

    for (const auto& face : m.faces) {
        if (face.verts.size() < 3) continue;
        float ndl = std::clamp(0.65f + 0.35f * (face.normal.y * 0.8f + face.normal.x * 0.3f), 0.45f, 1.05f);
        float lit = shade_base * ndl;
        if (face.textured && tex != 0 && face.u.size() == face.verts.size()) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex);
            glColor3f(lit, lit, lit);
            glBegin(GL_POLYGON);
            for (size_t k = 0; k < face.verts.size(); ++k) {
                const auto& v = m.verts[face.verts[k]];
                glTexCoord2f(face.u[k], face.v[k]);
                glVertex3f(v.x, v.y, v.z);
            }
            glEnd();
            glDisable(GL_TEXTURE_2D);
        } else {
            uint8_t c = face.color_idx;
            glColor3f(vga_rgb_[c * 3 + 0] * lit, vga_rgb_[c * 3 + 1] * lit, vga_rgb_[c * 3 + 2] * lit);
            glBegin(GL_POLYGON);
            for (uint16_t vi : face.verts) {
                const auto& v = m.verts[vi];
                glVertex3f(v.x, v.y, v.z);
            }
            glEnd();
        }
    }
    glPopMatrix();
}

void ModernRenderer::draw_fallback_jet(float scale, bool friendly, bool is_tomcat) {
    glPushMatrix();
    glScalef(scale, scale, scale);
    float l = 0.25f + 0.75f * lit_.daylight;
    if (is_tomcat) glColor3f(0.55f * l, 0.58f * l, 0.64f * l);
    else if (friendly) glColor3f(0.45f * l, 0.55f * l, 0.65f * l);
    else glColor3f(0.58f * l, 0.52f * l, 0.45f * l);
    glBegin(GL_TRIANGLES);
    glVertex3f( 0.0f, 0.0f, -9.5f); glVertex3f(-7.5f, 0.0f, 4.0f); glVertex3f(7.5f, 0.0f, 4.0f);
    glVertex3f(-1.2f, 0.0f,  5.0f); glVertex3f(-1.5f, 3.2f, 7.5f); glVertex3f(-1.2f, 0.0f, 8.5f);
    if (is_tomcat) {
        glVertex3f(1.2f, 0.0f, 5.0f); glVertex3f(1.5f, 3.2f, 7.5f); glVertex3f(1.2f, 0.0f, 8.5f);
    }
    glEnd();
    glPopMatrix();
}

void ModernRenderer::draw_fallback_carrier(float scale) {
    glPushMatrix();
    glScalef(scale, scale, scale);
    float l = 0.25f + 0.75f * lit_.daylight;
    glColor3f(0.28f * l, 0.30f * l, 0.32f * l);
    glBegin(GL_QUADS);
    glVertex3f(-38.0f, 20.0f, -166.0f); glVertex3f(38.0f, 20.0f, -166.0f);
    glVertex3f( 38.0f, 20.0f,  166.0f); glVertex3f(-38.0f, 20.0f, 166.0f);
    glEnd();
    glColor3f(0.52f * l, 0.54f * l, 0.58f * l);
    glBegin(GL_QUADS);
    glVertex3f(26.0f, 20.0f, -20.0f); glVertex3f(36.0f, 20.0f, -20.0f);
    glVertex3f(36.0f, 52.0f, -20.0f); glVertex3f(26.0f, 52.0f, -20.0f);
    glEnd();
    glPopMatrix();
}

void ModernRenderer::draw_entities_and_ships(const FlightTelemetry& tel) {
    double cam_x = tel.has_camera ? tel.cam_x : tel.world_x;
    double cam_y = tel.has_camera ? tel.cam_y : tel.world_y;
    double cam_z_m = (tel.has_camera ? tel.cam_alt_ft : tel.altitude_ft) * kFeetToMeters;

    for (const auto& e : tel.entities) {
        if (e.is_player && !tel.is_external_cam) continue;
        if (e.is_ship && !vr_cfg_.show_ships) continue;
        if (!e.is_ship && !vr_cfg_.show_aircraft) continue;

        float dx = static_cast<float>((e.world_x - cam_x) * kWorldUnitToMeters);
        float dz = static_cast<float>((e.world_y - cam_y) * kWorldUnitToMeters);
        float dy = static_cast<float>(e.world_z * kFeetToMeters - cam_z_m);
        float dist_km = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.001f;
        if (dist_km > 95.0f) continue;

        float scale = (e.is_player || e.is_carrier) ? (e.is_carrier ? 5.5f : 1.0f) : compute_smart_scale(dist_km, e.is_ground);
        glPushMatrix();
        glTranslatef(dx, dy, dz);
        glRotatef(-e.yaw_deg, 0.0f, 1.0f, 0.0f);
        glRotatef(e.pitch_deg, 1.0f, 0.0f, 0.0f);
        glRotatef(-e.roll_deg, 0.0f, 0.0f, 1.0f);

        auto it = e.model_name.empty() ? models_3dz_.end() : models_3dz_.find(e.model_name);
        if (it != models_3dz_.end()) {
            draw_3dz_model(it->second, scale, lit_.daylight);
        } else if (e.is_carrier) {
            draw_fallback_carrier(1.0f);
        } else {
            draw_fallback_jet(scale, e.is_friendly, e.is_player);
        }
        glPopMatrix();

        // High-altitude twin wingtip contrails (>= 20,000 ft MSL)
        if (vr_cfg_.show_contrails && !e.is_ship && e.world_z >= 20000.0) {
            float yr = e.yaw_deg * kDegToRad;
            float bx = -std::sin(yr) * 320.0f, bz = std::cos(yr) * 320.0f;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(2.0f);
            glBegin(GL_LINES);
            glColor4f(0.95f, 0.97f, 1.0f, 0.55f); glVertex3f(dx, dy, dz);
            glColor4f(0.95f, 0.97f, 1.0f, 0.0f);  glVertex3f(dx + bx, dy, dz + bz);
            glEnd();
            glLineWidth(1.0f);
            glDisable(GL_BLEND);
        }
    }
}

void ModernRenderer::draw_missiles_and_fx(const FlightTelemetry& tel) {
    double cam_x = tel.has_camera ? tel.cam_x : tel.world_x;
    double cam_y = tel.has_camera ? tel.cam_y : tel.world_y;
    double cam_z_m = (tel.has_camera ? tel.cam_alt_ft : tel.altitude_ft) * kFeetToMeters;

    if (vr_cfg_.show_missiles_trails) {
        // Persistent 3D smoke trail ribbons visible out to 25+ NM
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(3.0f);
        for (const auto& kv : missile_trails_) {
            const auto& pts = kv.second;
            if (pts.size() < 2) continue;
            glBegin(GL_LINE_STRIP);
            for (const auto& pt : pts) {
                float alpha = std::clamp(1.0f - pt.age / std::max(0.5f, vr_cfg_.missile_trail_lifetime), 0.0f, 1.0f) * 0.72f;
                glColor4f(0.88f * lit_.daylight + 0.12f, 0.88f * lit_.daylight + 0.12f, 0.90f * lit_.daylight + 0.10f, alpha);
                glVertex3f(static_cast<float>((pt.wx - cam_x) * kWorldUnitToMeters),
                           static_cast<float>(pt.wz * kFeetToMeters - cam_z_m),
                           static_cast<float>((pt.wy - cam_y) * kWorldUnitToMeters));
            }
            glEnd();
        }
        glLineWidth(1.0f);
        glDisable(GL_BLEND);

        // Blazing rocket flares & 3D missile bodies
        auto misl_it = models_3dz_.find("PHOENIX2.3DZ");
        for (const auto& m : tel.missiles) {
            float dx = static_cast<float>((m.world_x - cam_x) * kWorldUnitToMeters);
            float dz = static_cast<float>((m.world_y - cam_y) * kWorldUnitToMeters);
            float dy = static_cast<float>(m.world_z * kFeetToMeters - cam_z_m);
            if (misl_it != models_3dz_.end()) {
                glPushMatrix();
                glTranslatef(dx, dy, dz);
                glRotatef(-m.yaw_deg, 0.0f, 1.0f, 0.0f);
                glRotatef(m.pitch_deg, 1.0f, 0.0f, 0.0f);
                draw_3dz_model(misl_it->second, 1.2f, lit_.daylight);
                glPopMatrix();
            }
            glPointSize(7.0f);
            glBegin(GL_POINTS);
            glColor3f(1.0f, 0.88f, 0.35f);
            glVertex3f(dx, dy, dz);
            glEnd();
        }
    }

    if (!world_fx_.empty()) {
        glPointSize(5.0f);
        glBegin(GL_POINTS);
        for (const auto& fx : world_fx_) {
            float dx = static_cast<float>((fx.wx - cam_x) * kWorldUnitToMeters);
            float dz = static_cast<float>((fx.wy - cam_y) * kWorldUnitToMeters);
            float dy = static_cast<float>(fx.wz * kFeetToMeters - cam_z_m);
            if (fx.kind == WorldFx::Kind::Flare) glColor3f(1.0f, 0.92f, 0.45f);
            else if (fx.kind == WorldFx::Kind::Chaff) glColor3f(0.85f, 0.88f, 0.92f);
            else glColor3f(1.0f, 0.72f, 0.20f);
            glVertex3f(dx, dy, dz);
        }
        glEnd();
    }
    glPointSize(1.0f);
}

void ModernRenderer::draw_padlock_xray_box(int x, int y, int w, int h, const FlightTelemetry& tel,
                                           float cam_yaw, float cam_pitch, float cam_roll,
                                           float vfov_deg, float horizon_ndc) {
    if (!tel.padlock_has_target) return;
    double cam_x = tel.has_camera ? tel.cam_x : tel.world_x;
    double cam_y = tel.has_camera ? tel.cam_y : tel.world_y;
    double cam_z_m = (tel.has_camera ? tel.cam_alt_ft : tel.altitude_ft) * kFeetToMeters;
    float dx = static_cast<float>((tel.padlock_target_x - cam_x) * kWorldUnitToMeters);
    float dz = static_cast<float>((tel.padlock_target_y - cam_y) * kWorldUnitToMeters);
    float dy = static_cast<float>(tel.padlock_target_z * kFeetToMeters - cam_z_m);

    // Transform into camera space
    float cy = std::cos(cam_yaw * kDegToRad), sy = std::sin(cam_yaw * kDegToRad);
    float x1 =  dx * cy + dz * sy;
    float z1 = -dx * sy + dz * cy;
    float cp = std::cos(cam_pitch * kDegToRad), sp = std::sin(cam_pitch * kDegToRad);
    float y2 =  dy * cp + z1 * sp;
    float z2 = -dy * sp + z1 * cp;
    float cr = std::cos(cam_roll * kDegToRad), sr = std::sin(cam_roll * kDegToRad);
    float cx =  x1 * cr - y2 * sr;
    float cyv = x1 * sr + y2 * cr;
    if (z2 >= -1.0f) return; // Behind camera

    float aspect = static_cast<float>(w) / static_cast<float>(std::max(1, h));
    float f = 1.0f / std::tan(vfov_deg * 0.5f * kDegToRad);
    float ndc_x = (cx * f / aspect) / (-z2);
    float ndc_y = (cyv * f) / (-z2) + horizon_ndc;
    if (std::abs(ndc_x) > 0.98f || std::abs(ndc_y) > 0.98f) return;

    glViewport(x, y, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    glColor3f(1.0f, 0.22f, 0.18f);
    float bx = 0.032f / aspect, by = 0.032f;
    glBegin(GL_LINE_LOOP);
    glVertex2f(ndc_x - bx, ndc_y - by);
    glVertex2f(ndc_x + bx, ndc_y - by);
    glVertex2f(ndc_x + bx, ndc_y + by);
    glVertex2f(ndc_x - bx, ndc_y + by);
    glEnd();
    glLineWidth(1.0f);
}

void ModernRenderer::render_3d_view(int x, int y, int w, int h, const FlightTelemetry& tel,
                                    float eye_offset_m, float cy_vga, bool vga_frustum,
                                    bool widescreen_cockpit, float vga_frustum_aspect) {
    if (w <= 0 || h <= 0) return;
    glViewport(x, y, w, h);

    float aspect = vga_frustum ? vga_frustum_aspect : (static_cast<float>(w) / static_cast<float>(h));
    float vfov = widescreen_cockpit ? vr_cfg_.widescreen_fov_deg : vr_cfg_.fov_deg;
    last_display_ppd_ = static_cast<float>(h) / std::max(15.0f, vfov);

    float horizon_ndc = 1.0f - 2.0f * ((cy_vga + vr_cfg_.hud_cy_trim_px) / 200.0f);
    float cam_yaw = (tel.has_camera ? tel.cam_yaw_deg : tel.yaw_deg) + vr_cfg_.head_yaw_deg;
    float cam_pitch = std::clamp((tel.has_camera ? tel.cam_pitch_deg : tel.pitch_deg) + vr_cfg_.head_pitch_deg, -89.0f, 89.0f);
    float cam_roll = tel.has_camera ? tel.cam_roll_deg : tel.roll_deg;

    // Unclamped 3D Padlock: aim camera directly at locked bandit or missile when Padlock is active!
    if (vr_cfg_.enhanced_3d_padlock && tel.padlock_active && tel.padlock_has_target) {
        double dx = (tel.padlock_target_x - tel.world_x) * kWorldUnitToMeters;
        double dz = (tel.padlock_target_y - tel.world_y) * kWorldUnitToMeters;
        double dy = (tel.padlock_target_z - tel.altitude_ft) * kFeetToMeters;
        double horiz = std::sqrt(dx * dx + dz * dz);
        cam_yaw = static_cast<float>(std::atan2(dx, -dz) * (180.0 / kPi));
        if (cam_yaw < 0.0f) cam_yaw += 360.0f;
        cam_pitch = std::clamp(static_cast<float>(std::atan2(dy, std::max(1.0, horiz)) * (180.0 / kPi)), -86.0f, 86.0f);
    }

    draw_sky_backdrop(cam_pitch, cam_roll, horizon_ndc, vfov, static_cast<float>(w) / static_cast<float>(h));

    set_perspective(vfov, static_cast<float>(w) / static_cast<float>(h), 0.5f, 120000.0f, -horizon_ndc);
    glLoadIdentity();
    if (std::abs(eye_offset_m) > 1e-4f) glTranslatef(-eye_offset_m, 0.0f, 0.0f);
    glRotatef(cam_roll, 0.0f, 0.0f, 1.0f);
    glRotatef(-cam_pitch, 1.0f, 0.0f, 0.0f);
    glRotatef(cam_yaw, 0.0f, 1.0f, 0.0f);

    draw_night_stars();

    if (vr_cfg_.horizon_fog) {
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, 28000.0f);
        glFogf(GL_FOG_END, 92000.0f);
        float fog_c[4] = {lit_.horizon[0], lit_.horizon[1], lit_.horizon[2], 1.0f};
        glFogfv(GL_FOG_COLOR, fog_c);
    } else {
        glDisable(GL_FOG);
    }

    draw_ocean_and_wld_terrain(tel);
    draw_clouds(tel);
    draw_entities_and_ships(tel);
    draw_missiles_and_fx(tel);
    glDisable(GL_FOG);
}

void ModernRenderer::render_vector_hud(int /*x*/, int /*y*/, int /*w*/, int /*h*/, const FlightTelemetry& /*tel*/) {}

void ModernRenderer::render_frame(int win_w, int win_h, GLuint vga_tex, GLuint vga_cockpit_tex, GLuint vga_hud_tex,
                                  const FlightTelemetry& raw_tel, float dt_sec) {
    update_telemetry(raw_tel, dt_sec);

    glViewport(0, 0, win_w, win_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float target_aspect = 4.0f / 3.0f;
    float win_aspect = static_cast<float>(win_w) / static_cast<float>(std::max(1, win_h));
    int rect_w = win_w, rect_h = win_h, rect_x = 0, rect_y = 0;
    if (win_aspect > target_aspect) {
        rect_w = static_cast<int>(win_h * target_aspect);
        rect_x = (win_w - rect_w) / 2;
    } else {
        rect_h = static_cast<int>(win_w / target_aspect);
        rect_y = (win_h - rect_h) / 2;
    }

    if (mode_ == RenderMode::ClassicVga || !raw_tel.in_flight) {
        draw_vga_quad(rect_x, rect_y, rect_w, rect_h, vga_tex);
        return;
    }

    const FlightTelemetry& tel = vr_cfg_.sync_hud_no_lag ? raw_tel : smooth_tel_;
    bool freelook_active = (std::abs(vr_cfg_.head_yaw_deg) > 0.5f || std::abs(vr_cfg_.head_pitch_deg) > 0.5f);

    if (mode_ == RenderMode::HybridCockpit) {
        if (vr_cfg_.hybrid_fill_widescreen) {
            render_3d_view(0, 0, win_w, win_h, tel, 0.0f, 52.0f, false, false, target_aspect);
        } else {
            render_3d_view(rect_x, rect_y, rect_w, rect_h, tel, 0.0f, 52.0f, true, false, target_aspect);
        }
        if ((freelook_active || tel.padlock_active) && !tel.is_external_cam && vr_cfg_.virtual_cockpit_freelook) {
            glClear(GL_DEPTH_BUFFER_BIT);
            set_perspective(vr_cfg_.widescreen_fov_deg, win_aspect, 0.08f, 40.0f, 0.0f);
            glLoadIdentity();
            glRotatef(-vr_cfg_.head_pitch_deg, 1.0f, 0.0f, 0.0f);
            glRotatef(vr_cfg_.head_yaw_deg, 0.0f, 1.0f, 0.0f);
            Cockpit3D::LiveCockpitState cs{};
            cs.yaw_deg = tel.yaw_deg; cs.pitch_deg = tel.pitch_deg; cs.roll_deg = tel.roll_deg;
            cs.airspeed_kts = tel.airspeed_kts; cs.agl_ft = tel.agl_ft; cs.g_load = tel.g_load;
            cs.wing_sweep_deg = tel.wing_sweep_deg; cs.sim_time_sec = sim_time_sec_;
            cs.back_seat_rio = tel.back_seat_rio;
            cockpit_3d_.render(lit_.daylight, lit_.ground_mul, vga_cockpit_tex, true, &cs, false);
        } else {
            draw_vga_quad(rect_x, rect_y, rect_w, rect_h, vga_cockpit_tex, 0.0f, 1.0f, true);
        }
        if (tel.padlock_active && tel.padlock_has_target) {
            draw_padlock_xray_box(0, 0, win_w, win_h, tel, tel.cam_yaw_deg, tel.cam_pitch_deg, tel.cam_roll_deg, vr_cfg_.fov_deg, 0.48f);
        }
    } else if (mode_ == RenderMode::Modern3D) {
        render_3d_view(0, 0, win_w, win_h, tel, 0.0f, 72.0f, false, vr_cfg_.widescreen_3d_cockpit);
        if (vr_cfg_.widescreen_3d_cockpit && !tel.is_external_cam) {
            glClear(GL_DEPTH_BUFFER_BIT);
            set_perspective(vr_cfg_.widescreen_fov_deg, win_aspect, 0.08f, 40.0f, 0.0f);
            glLoadIdentity();
            glRotatef(-vr_cfg_.head_pitch_deg, 1.0f, 0.0f, 0.0f);
            glRotatef(vr_cfg_.head_yaw_deg, 0.0f, 1.0f, 0.0f);
            Cockpit3D::LiveCockpitState cs{};
            cs.yaw_deg = tel.yaw_deg; cs.pitch_deg = tel.pitch_deg; cs.roll_deg = tel.roll_deg;
            cs.airspeed_kts = tel.airspeed_kts; cs.agl_ft = tel.agl_ft; cs.g_load = tel.g_load;
            cs.wing_sweep_deg = tel.wing_sweep_deg; cs.sim_time_sec = sim_time_sec_;
            cs.back_seat_rio = tel.back_seat_rio;
            cockpit_3d_.render(lit_.daylight, lit_.ground_mul, vga_cockpit_tex, true, &cs, true);
        } else if (vr_cfg_.use_original_hud) {
            draw_vga_quad(rect_x, rect_y, rect_w, rect_h, vga_hud_tex, 0.0f, 1.0f, true);
        }
    } else if (mode_ == RenderMode::VrStereoPreview) {
        int half_w = win_w / 2;
        float half_ipd = (vr_cfg_.ipd_mm * 0.001f) * 0.5f;
        render_3d_view(0, 0, half_w, win_h, tel, -half_ipd, 72.0f, false, true);
        render_3d_view(half_w, 0, win_w - half_w, win_h, tel, +half_ipd, 72.0f, false, true);
    }
}

void ModernRenderer::spawn_chaff(const FlightTelemetry& tel) {
    world_fx_.push_back({WorldFx::Kind::Chaff, tel.world_x, tel.world_y, tel.altitude_ft, 0, 0, -5.0, 0.0f, 4.0f});
}

void ModernRenderer::spawn_flare(const FlightTelemetry& tel) {
    world_fx_.push_back({WorldFx::Kind::Flare, tel.world_x, tel.world_y, tel.altitude_ft, 0, 0, -15.0, 0.0f, 3.5f});
}

void ModernRenderer::spawn_gun_tracer(const FlightTelemetry& tel) {
    float yr = tel.yaw_deg * kDegToRad;
    float pr = tel.pitch_deg * kDegToRad;
    double vx = std::sin(yr) * std::cos(pr) * 110.0;
    double vy = -std::cos(yr) * std::cos(pr) * 110.0;
    double vz = std::sin(pr) * 3300.0;
    world_fx_.push_back({WorldFx::Kind::Tracer, tel.world_x, tel.world_y, tel.altitude_ft, vx, vy, vz, 0.0f, 1.4f});
}

} // namespace f14

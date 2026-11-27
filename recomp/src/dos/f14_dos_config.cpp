#include "dos/f14_dos_config.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace f14 {

namespace {

int16_t rd16(const uint8_t* p) {
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

void wr16(uint8_t* p, int16_t v) {
    uint16_t u = static_cast<uint16_t>(v);
    p[0] = static_cast<uint8_t>(u & 0xFFu);
    p[1] = static_cast<uint8_t>((u >> 8) & 0xFFu);
}

fs::path find_ci_file(const fs::path& dir, const char* upper, const char* lower) {
    std::error_code ec;
    if (fs::exists(dir / upper, ec)) return dir / upper;
    if (fs::exists(dir / lower, ec)) return dir / lower;
    return dir / upper;
}

} // namespace

bool F14DosConfigStore::load(const std::string& game_dir) {
    last_dir_ = game_dir;
    realism_.loaded = false;
    detail_.loaded = false;
    if (game_dir.empty()) return false;

    fs::path dir(game_dir);
    bool any = false;

    // 1. Load CONFIG.F14 (30 bytes)
    {
        fs::path p = find_ci_file(dir, "CONFIG.F14", "config.f14");
        std::ifstream in(p, std::ios::binary);
        if (in) {
            uint8_t buf[30]{};
            in.read(reinterpret_cast<char*>(buf), sizeof(buf));
            if (in.gcount() >= 30) {
                realism_.training        = rd16(buf + 0);
                realism_.diff_level      = rd16(buf + 2);
                realism_.weapon_effect   = rd16(buf + 4);
                realism_.flight_model    = rd16(buf + 6);
                realism_.damage          = rd16(buf + 8);
                realism_.carrier_landing = rd16(buf + 10);
                realism_.rio_assist      = rd16(buf + 12);
                realism_.back_seat       = rd16(buf + 14);
                realism_.gun_camera      = rd16(buf + 16);
                realism_.radar           = rd16(buf + 18);
                realism_.crash_mode      = rd16(buf + 20);
                realism_.enemy_skill     = rd16(buf + 22);
                realism_.carrier         = rd16(buf + 24);
                realism_.squadron        = rd16(buf + 26);
                realism_.world           = rd16(buf + 28);
                realism_.loaded = true;
                any = true;
            }
        }
    }

    // 2. Load JOY.F14 (81 bytes)
    {
        fs::path p = find_ci_file(dir, "JOY.F14", "joy.f14");
        std::ifstream in(p, std::ios::binary);
        if (in) {
            uint8_t buf[81]{};
            in.read(reinterpret_cast<char*>(buf), sizeof(buf));
            if (in.gcount() >= 81) {
                detail_.offset        = rd16(buf + 0);
                detail_.kb_sense      = rd16(buf + 2);
                detail_.snd_detail    = rd16(buf + 4);
                detail_.msg_detail    = rd16(buf + 6);
                detail_.sky_detail    = rd16(buf + 8);
                detail_.grnd_detail   = rd16(buf + 10);
                detail_.plane_detail  = rd16(buf + 12);
                detail_.missile_trail = rd16(buf + 14);
                for (int i = 0; i < 6; ++i) {
                    detail_.joy_flags[i] = rd16(buf + 16 + i * 2);
                }
                std::memcpy(detail_.joy_vals.data(), buf + 28, 52);
                detail_.deadzone = buf[80];
                detail_.loaded = true;
                any = true;
            }
        }
    }

    return any;
}

bool F14DosConfigStore::save(const std::string& game_dir) const {
    if (game_dir.empty()) return false;
    fs::path dir(game_dir);
    bool ok = true;

    if (realism_.loaded) {
        uint8_t buf[30]{};
        wr16(buf + 0,  realism_.training);
        wr16(buf + 2,  realism_.diff_level);
        wr16(buf + 4,  realism_.weapon_effect);
        wr16(buf + 6,  realism_.flight_model);
        wr16(buf + 8,  realism_.damage);
        wr16(buf + 10, realism_.carrier_landing);
        wr16(buf + 12, realism_.rio_assist);
        wr16(buf + 14, realism_.back_seat);
        wr16(buf + 16, realism_.gun_camera);
        wr16(buf + 18, realism_.radar);
        wr16(buf + 20, realism_.crash_mode);
        wr16(buf + 22, realism_.enemy_skill);
        wr16(buf + 24, realism_.carrier);
        wr16(buf + 26, realism_.squadron);
        wr16(buf + 28, realism_.world);

        fs::path p = find_ci_file(dir, "CONFIG.F14", "config.f14");
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out) ok = false;
        else {
            out.write(reinterpret_cast<const char*>(buf), sizeof(buf));
            if (!out.good()) ok = false;
        }
    }

    if (detail_.loaded) {
        uint8_t buf[81]{};
        wr16(buf + 0,  detail_.offset);
        wr16(buf + 2,  detail_.kb_sense);
        wr16(buf + 4,  detail_.snd_detail);
        wr16(buf + 6,  detail_.msg_detail);
        wr16(buf + 8,  detail_.sky_detail);
        wr16(buf + 10, detail_.grnd_detail);
        wr16(buf + 12, detail_.plane_detail);
        wr16(buf + 14, detail_.missile_trail);
        for (int i = 0; i < 6; ++i) {
            wr16(buf + 16 + i * 2, detail_.joy_flags[i]);
        }
        std::memcpy(buf + 28, detail_.joy_vals.data(), 52);
        buf[80] = detail_.deadzone;

        fs::path p = find_ci_file(dir, "JOY.F14", "joy.f14");
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out) ok = false;
        else {
            out.write(reinterpret_cast<const char*>(buf), sizeof(buf));
            if (!out.good()) ok = false;
        }
    }

    return ok;
}

} // namespace f14

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace f14 {

// Exact binary mirror of Fleet Defender Gold CONFIG.F14 (30 bytes = 15 int16_t)
struct F14DosRealismConfig {
    bool loaded = false;
    int16_t training = 0;         // [0] 0=Normal, 1=Training Mode
    int16_t diff_level = 2;       // [1] 0=Novice, 1=Pilot, 2=Ace
    int16_t weapon_effect = 2;    // [2] 0=Easy, 1=Moderate, 2=Authentic
    int16_t flight_model = 2;     // [3] 0=Novice, 1=Moderate, 2=Authentic
    int16_t damage = 2;           // [4] 0=Invulnerable, 1=Moderate, 2=Authentic
    int16_t carrier_landing = 2;  // [5] 0=Easy, 1=Moderate, 2=Authentic
    int16_t rio_assist = 0;       // [6] 0=Authentic/Manual, 1=Moderate, 2=Full Assist
    int16_t back_seat = 2;        // [7] 0=Pilot Only, 1=RIO Only, 2=Pilot & RIO
    int16_t gun_camera = 1;       // [8] 0=Off, 1=On, 2=Auto
    int16_t radar = 2;            // [9] 0=Simple, 1=Moderate, 2=Authentic
    int16_t crash_mode = 2;       // [10] 0=Bounce, 1=Damage, 2=Fatal
    int16_t enemy_skill = 0;      // [11] 0=Novice, 1=Regular, 2=Ace
    int16_t carrier = 0;          // [12] Preserved
    int16_t squadron = 7;         // [13] Preserved
    int16_t world = 2;            // [14] Preserved
};

// Exact binary mirror of Fleet Defender Gold JOY.F14 (81 bytes)
struct F14DosDetailConfig {
    bool loaded = false;
    int16_t offset = 16;          // [0] Preserved
    int16_t kb_sense = 2;         // [1] 0=Low, 1=Medium, 2=High
    int16_t snd_detail = 0;       // [2] 0=All On, 1=No Engine, 2=Speech/Warn Off, 3=All Off
    int16_t msg_detail = 0;       // [3] 0=All, 1=No RIO, 2=No Game, 3=All Off
    int16_t sky_detail = 2;       // [4] 0=Low, 1=High, 2=Max
    int16_t grnd_detail = 2;      // [5] 0=Low, 1=High, 2=Max
    int16_t plane_detail = 2;     // [6] 0=Low, 1=Medium, 2=High
    int16_t missile_trail = 1;    // [7] 0=Off, 1=On
    std::array<int16_t, 6> joy_flags{7, 1, 3, 8, 0, 0}; // [8..13] Preserved (managed by HotasMapper)
    std::array<uint8_t, 52> joy_vals{};                 // [28..79] Preserved
    uint8_t deadzone = 12;                              // [80] 0..40
};

class F14DosConfigStore {
public:
    bool load(const std::string& game_dir);
    bool save(const std::string& game_dir) const;

    F14DosRealismConfig& realism() { return realism_; }
    const F14DosRealismConfig& realism() const { return realism_; }
    F14DosDetailConfig& detail() { return detail_; }
    const F14DosDetailConfig& detail() const { return detail_; }

    const std::string& last_dir() const { return last_dir_; }

private:
    F14DosRealismConfig realism_;
    F14DosDetailConfig detail_;
    std::string last_dir_;
};

} // namespace f14

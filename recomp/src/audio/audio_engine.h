#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_AudioStream;

namespace MT32Emu {
class Synth;
class ROMImage;
} // namespace MT32Emu

namespace f14 {

enum class MusicBackend {
    CM32L = 0,
    SB16_OPL3 = 1,
    Muted = 2
};

struct FlightAudioTelemetry {
    bool in_flight = false;
    bool paused = false;
    bool is_external_cam = false;
    float rpm_pct = 75.0f;       // 0..100
    float throttle_01 = 0.60f;   // 0..1
    int ab_stage = 0;            // 0..5
    float airspeed_kts = 400.0f; // KIAS
    float g_load = 1.0f;         // G-load
    // 3D Spatial Audio coordinates (camera-relative stereo pan -1..+1, distance km, Doppler ratio)
    float player_ext_pan = 0.0f;
    float player_ext_dist_km = 0.03f;
    bool has_nearby_jet = false;
    float nearby_jet_dist_km = 99.0f;
    float nearby_jet_pan = 0.0f;
    float nearby_jet_doppler = 1.0f;
    float combat_pan = 0.0f;     // Direction of nearest bandit for spatialized explosions/AAA
};

struct AudioConfig {
    MusicBackend music_backend = MusicBackend::CM32L;
    bool enable_sb16_sfx = true;
    std::string cm32l_rom_dir = "roms";
    // Note: The 64-voice sample proxy uses Falcon 3.0 Y.MID program IDs (39/87/100/108).
    // Fleet Defender Gold's RSOUND.14G uses its own MT-32 SysEx patch bank, so keep direct
    // live Munt CM-32L/MT-32 rendering enabled by default until F-14 patch IDs are mapped.
    bool use_cm32l_sample_proxy = false;
    bool telemetry_flight_audio = false;
    bool spatial_3d_audio = true;        // 3D stereo panning & Doppler flybys from live entity/camera coordinates!
    bool audio_radio_ducking = true;     // Gently dip engine while SB16 wingman/AWACS radio voices speak
    bool audio_warm_enhance = true;      // Subtle warm cockpit bass body & stereo depth (OFF = pure dry CM-32L)
    bool gun_proxy_anti_drop = true;     // Keep baked CM-32L M61 Vulcan firing even if DOS SFX queue drops it
    int mt32_max_partials = 32;          // 32 = authentic 1988 CM-32L (Direct Live mode)
    bool nice_partial_mixing = false;    // false = authentic LA-32 hardware mixing (Direct Live mode)
    bool polyphonic_gun_sfx = false;     // Optional synthetic M61A1 Vulcan layer (OFF = pure CM-32L / baked CM-32L gun)
    float master_volume = 0.85f;
    float mt32_volume = 0.95f;           // Live CM-32L Menu / Briefing music
    float engine_bus_volume = 0.85f;     // Jetengine, Afterburner, Wind & 3D Flybys
    float wind_volume = 0.60f;           // Canopy wind / high-G buffet trim within the Engine bus (0 = off)
    float weapons_bus_volume = 1.00f;    // M61 Gun, Missile Launch, Chaff, Flare, Explosions, AAA
    float avionics_bus_volume = 0.85f;   // Sidewinder Growl/Lock, AMRAAM Lock, RWR Warnings, Clicks
    float sb16_sfx_volume = 1.00f;       // SB16 Wingman / AWACS Radio Voice Comms
    float gun_sfx_volume = 0.65f;
    float opl3_volume = 0.85f;
};

class AudioEngine {
public:
    static constexpr int kMaxProxyVoices = 64;

    AudioEngine();
    ~AudioEngine();

    bool init();
    void shutdown();

    // Load or reload CM-32L / MT-32 ROMs from config.cm32l_rom_dir
    bool load_mt32_roms(const std::string& rom_dir);

    // Hardware port I/O from DOS machine
    uint8_t mpu401_read(uint16_t port);
    void mpu401_write(uint16_t port, uint8_t val);

    uint8_t sb16_read(uint16_t port);
    void sb16_write(uint16_t port, uint8_t val, double live_pit_hz = 11025.0);

    // Queue 8-bit unsigned PCM samples from SB16 DMA into the SDL3 mixer
    void push_sb_pcm8(const uint8_t* samples, size_t count, int sample_rate);
    // Queue 1 sample from SB Direct DAC (cmd 0x10) with continuous fractional-phase resampling
    void push_sb_dac8(uint8_t sample, double sample_rate_hz);

    // Check if SB16 DSP just requested a single-cycle DMA transfer (returns byte count, 0 if none)
    uint32_t consume_sb_dma_request(int& out_sample_rate);

    uint8_t adlib_read(uint16_t port);
    void adlib_write(uint16_t port, uint8_t val);

    uint64_t midi_bytes_received() const { return midi_bytes_rx_; }
    uint64_t sysex_messages_received() const { return sysex_rx_; }
    uint64_t sb_dac_samples_received() const { return sb_dac_rx_; }
    uint32_t mt32_partial_limit() const { return mt32_partial_limit_; }
    uint32_t mt32_active_partials() const { return mt32_active_partials_; }
    uint32_t proxy_active_voices() const { return proxy_active_voices_; }
    uint32_t proxy_baked_count() const { return proxy_baked_count_; }
    bool proxy_ready() const { return config_.use_cm32l_sample_proxy && mt32_ready_ && config_.music_backend == MusicBackend::CM32L; }
    std::string last_midi_summary() const;

    // Tell audio engine whether player is currently in 3D flight (routes in-flight SFX through 64-voice proxy,
    // keeping War Room / Intro / Briefing music on live libmt32emu!)
    void set_in_flight(bool in_flight);
    // Feed live 3D flight & spatial telemetry every host frame for zero-choppy Engine/AB/Wind/Doppler 3D audio!
    void update_flight_telemetry(const FlightAudioTelemetry& tel);
    // Direct non-blocking combat triggers (100% immune to Falcon 3.0's 3-slot DOS SFX dropout!)
    void trigger_chaff_sfx();
    void trigger_flare_sfx();
    void trigger_missile_launch_sfx();
    void prebake_core_sfx();
    // Play short preview sound on Roland MT-32 / SB16 so user can test mixer balance in Launcher/F12
    void play_test_tone(bool test_sb16_voice = false);

    // Non-blocking M61A1 Vulcan cannon trigger (driven from live cockpit gun input when cannon is selected)
    void set_gun_firing(bool active) { gun_firing_.store(active, std::memory_order_relaxed); }

    // Pump audio to SDL3 stream each host frame
    void update();

    AudioConfig& config() { return config_; }
    const AudioConfig& config() const { return config_; }
    void set_fast_boot_mute(bool m) { fast_boot_mute_ = m; }
    void set_trace_audio(bool t) { trace_audio_ = t; }
    void pump_silent_mt32(uint32_t frames = 512);
    bool mt32_ready() const { return mt32_ready_; }
    const std::string& mt32_status() const { return mt32_status_; }

private:
    struct BakedSample {
        std::vector<float> stereo_44k; // Interleaved L/R @ 44,100 Hz
        uint32_t num_frames = 0;
        uint8_t ref_note = 60;
        bool looping = false;
        uint32_t loop_start = 0;
        uint32_t loop_end = 0;
        bool valid = false;
    };

    enum class SfxBus : uint8_t {
        Engine = 0,
        Weapons = 1,
        Avionics = 2
    };

    struct ProxyVoice {
        bool active = false;
        bool looping = false;
        bool releasing = false;
        bool is_gun_fallback = false;
        SfxBus bus = SfxBus::Weapons;
        uint8_t midi_ch = 0;
        uint8_t prog = 0;
        uint8_t note = 60;
        uint16_t sample_key = 0;
        double phase = 0.0;
        double phase_inc = 1.0;
        float env = 0.0f;
        float vel_gain = 1.0f;
        float gain_l = 1.0f;
        float gain_r = 1.0f;
        float target_gain_l = 1.0f;
        float target_gain_r = 1.0f;
        uint64_t birth_seq = 0;
    };

    struct ContinuousLoopVoice {
        double phase = 0.0;
        float gain_l = 0.0f;
        float gain_r = 0.0f;
        float pitch = 1.0f;
    };

    AudioConfig config_;
    SDL_AudioStream* stream_ = nullptr;

    // Munt libmt32emu state
    std::mutex mt32_mutex_;
    std::unique_ptr<MT32Emu::Synth> mt32_synth_;
    std::string loaded_ctrl_rom_path_;
    std::string loaded_pcm_rom_path_;
    bool mt32_ready_ = false;
    std::string mt32_status_ = "Not initialized";

    // 64-Voice Pre-Baked CM-32L Sample Proxy (synthesized at runtime strictly from user's valid CM-32L ROMs!)
    std::mutex proxy_mutex_;
    std::unique_ptr<MT32Emu::Synth> baker_synth_;
    std::unordered_map<uint16_t, BakedSample> baked_cache_;
    std::array<ProxyVoice, kMaxProxyVoices> proxy_voices_{};
    size_t baked_sysex_count_ = 0;
    bool proxy_bank_dirty_ = true;
    uint64_t voice_seq_ = 0;
    uint32_t proxy_active_voices_ = 0;
    uint32_t proxy_baked_count_ = 0;
    bool prev_proxy_in_flight_ = false;
    std::atomic<bool> in_flight_{false};
    uint8_t last_gun_note_ = 59;
    std::chrono::steady_clock::time_point last_gun_midi_time_{};
    std::chrono::steady_clock::time_point last_prog_trigger_[128]{};
    // Layer 1: Continuous telemetry-driven Engine, Afterburner, Wind & 3D Doppler Flyby voices
    FlightAudioTelemetry flight_tel_{};
    ContinuousLoopVoice tel_jet_cockpit_{};
    ContinuousLoopVoice tel_jet_outside_{};
    ContinuousLoopVoice tel_ab_{};
    ContinuousLoopVoice tel_wind_{};
    float wind_ctrl_env_ = 0.0f;         // Slow attack/release envelope for canopy wind level (0..1)
    ContinuousLoopVoice tel_flyby_3d_{};
    bool core_prebaked_ = false;
    float radio_duck_env_ = 1.0f;
    float warm_lp_l_ = 0.0f;
    float warm_lp_r_ = 0.0f;
    uint32_t cm_pan_toggle_ = 0;

    // MPU-401 state
    bool mpu_uart_mode_ = false;
    std::vector<uint8_t> mpu_rx_queue_;
    std::vector<uint8_t> midi_buf_;
    bool in_sysex_ = false;
    uint8_t running_status_ = 0;
    uint8_t active_midi_notes_[16][128]{}; // Tracks sounding (ch, note) to stop lock-tone stacking at >32 partials!
    // Cached SysEx & Program/CC state so changing CM-32L polyphony or reloading ROMs NEVER wipes Y.MID patches!
    std::vector<std::vector<uint8_t>> sysex_history_;
    int16_t chan_prog_[16]{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    uint8_t chan_vol_[16]{100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100};
    uint8_t chan_expr_[16]{127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127};
    uint8_t chan_pan_[16]{64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64};
    float chan_bend_semi_[16]{};
    // Wall-clock rate limiter for frame-driven Falcon 3.0 SFX retriggers (e.g., AMRAAM lock prog=47 @ 20 MIPS!)
    std::chrono::steady_clock::time_point last_sfx_note_time_[16]{};
    int16_t last_sfx_prog_[16]{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    int16_t last_sfx_note_[16]{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    uint32_t last_midi_msg_ = 0;
    uint64_t note_on_rx_ = 0;

    // SB16 DSP state
    uint8_t sb_reset_state_ = 0;
    std::vector<uint8_t> sb_read_queue_;
    uint8_t sb_cmd_ = 0;
    uint8_t sb_cmd_bytes_left_ = 0;
    uint8_t sb_cmd_params_[4]{};
    int sb_cmd_param_idx_ = 0;
    int sb_sample_rate_ = 11025;
    uint32_t sb_dma_req_len_ = 0;
    uint32_t sb_block_size_ = 0;
    std::mutex sb_pcm_mutex_;
    std::vector<float> sb_pcm_queue_;
    size_t sb_pcm_read_pos_ = 0;
    double sb_dac_phase_ = 0.0;
    float sb_dac_prev_ = 0.0f;
    float sb_dac_lp_ = 0.0f;
    uint64_t midi_bytes_rx_ = 0;
    uint64_t sysex_rx_ = 0;
    uint64_t sb_dac_rx_ = 0;
    uint32_t mt32_partial_limit_ = 32;
    uint32_t mt32_active_partials_ = 0;
    int mt32_test_note_blocks_ = 0;
    bool fast_boot_mute_ = false;
    bool trace_audio_ = false;

    // Dedicated non-blocking M61A1 6000-RPM Vulcan rotary cannon synth state (optional synthetic mode)
    std::atomic<bool> gun_firing_{false};
    float gun_env_ = 0.0f;
    float gun_phase_ = 0.0f;
    float gun_lp_ = 0.0f;
    float gun_hp_prev_ = 0.0f;
    uint32_t gun_rng_ = 0x12345678u;

    void process_midi_byte(uint8_t b);
    void send_short_midi(uint32_t msg);

    // 64-Voice CM-32L Sample Proxy internals
    void invalidate_proxy_bank();
    bool ensure_baker_ready_unlocked(const std::string& ctrl_path, const std::string& pcm_path,
                                     const std::vector<std::vector<uint8_t>>& sysex_copy);
    const BakedSample* get_or_bake_sample(uint8_t prog, uint8_t note, uint16_t& out_key);
    void proxy_handle_note_on(uint8_t ch, uint8_t prog, uint8_t note, uint8_t vel);
    void proxy_handle_note_off(uint8_t ch, uint8_t note);
    void proxy_release_channel(uint8_t ch, bool continuous_only);
    void proxy_update_channel_params(uint8_t ch);
    void proxy_release_all();
    void compute_ch_stereo_gains(uint8_t ch, float vel_gain, float& out_l, float& out_r) const;
    static SfxBus classify_sfx_bus(uint8_t prog);
    static void pan_to_lr(float pan_m1_p1, float gain, float& out_l, float& out_r);
    void spawn_direct_oneshot(uint8_t prog, uint8_t note, float gain, float pan_m1_p1, int dedup_ms);
    void mix_continuous_loop(ContinuousLoopVoice& lv, uint16_t sample_key,
                             float target_gl, float target_gr, float target_pitch,
                             float* out_stereo, int num_frames);
};

} // namespace f14

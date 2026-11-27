#include "audio/audio_engine.h"

#include <SDL3/SDL.h>
#include <mt32emu/mt32emu.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace f14 {

namespace {
int midi_data_len(uint8_t status) {
    switch (status & 0xF0) {
        case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0:
            return 2;
        case 0xC0: case 0xD0:
            return 1;
        default:
            return 0;
    }
}
} // namespace

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init() {
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = 44100;

    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
        mt32_status_ = std::string("SDL audio error: ") + SDL_GetError();
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);

    // Attempt to load user's CM-32L ROMs automatically
    load_mt32_roms(config_.cm32l_rom_dir);
    return true;
}

void AudioEngine::shutdown() {
    {
        std::lock_guard<std::mutex> lock(proxy_mutex_);
        if (baker_synth_) {
            baker_synth_->close();
            baker_synth_.reset();
        }
        baked_cache_.clear();
        for (auto& v : proxy_voices_) v.active = false;
        proxy_active_voices_ = 0;
        proxy_baked_count_ = 0;
    }
    {
        std::lock_guard<std::mutex> lock(mt32_mutex_);
        if (mt32_synth_) {
            mt32_synth_->close();
            mt32_synth_.reset();
        }
        mt32_ready_ = false;
    }
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

bool AudioEngine::load_mt32_roms(const std::string& rom_dir) {
    bool loaded_ok = false;
    {
        std::lock_guard<std::mutex> lock(mt32_mutex_);
        config_.cm32l_rom_dir = rom_dir;
        mt32_ready_ = false;
        loaded_ctrl_rom_path_.clear();
        loaded_pcm_rom_path_.clear();

        if (mt32_synth_) {
            mt32_synth_->close();
            mt32_synth_.reset();
        }

        if (!fs::exists(rom_dir) || !fs::is_directory(rom_dir)) {
            mt32_status_ = "ROM folder not found: " + rom_dir;
        } else {
            // Search for CM32L or MT32 control & PCM ROMs
            const char* ctrl_names[] = {
                "CM32L_CONTROL.ROM", "cm32l_control.rom",
                "MT32_CONTROL.ROM", "mt32_control.rom"
            };
            const char* pcm_names[] = {
                "CM32L_PCM.ROM", "cm32l_pcm.rom",
                "MT32_PCM.ROM", "mt32_pcm.rom"
            };

            std::string ctrl_path, pcm_path;
            for (const char* name : ctrl_names) {
                fs::path p = fs::path(rom_dir) / name;
                if (fs::exists(p)) { ctrl_path = p.string(); break; }
            }
            for (const char* name : pcm_names) {
                fs::path p = fs::path(rom_dir) / name;
                if (fs::exists(p)) { pcm_path = p.string(); break; }
            }

            if (ctrl_path.empty() || pcm_path.empty()) {
                mt32_status_ = "Missing CONTROL/PCM ROMs in " + rom_dir;
            } else {
                MT32Emu::FileStream ctrl_file, pcm_file;
                if (!ctrl_file.open(ctrl_path.c_str()) || !pcm_file.open(pcm_path.c_str())) {
                    mt32_status_ = "Failed to open ROM files";
                } else {
                    const MT32Emu::ROMImage* ctrl_img = MT32Emu::ROMImage::makeROMImage(&ctrl_file);
                    const MT32Emu::ROMImage* pcm_img = MT32Emu::ROMImage::makeROMImage(&pcm_file);

                    auto synth = std::make_unique<MT32Emu::Synth>();
                    MT32Emu::Bit32u partials = static_cast<MT32Emu::Bit32u>(std::clamp(config_.mt32_max_partials, 32, 256));
                    bool ok = synth->open(*ctrl_img, *pcm_img, partials);

                    MT32Emu::ROMImage::freeROMImage(ctrl_img);
                    MT32Emu::ROMImage::freeROMImage(pcm_img);

                    if (!ok) {
                        mt32_status_ = "libmt32emu failed to initialize synth";
                    } else {
                        synth->setNicePartialMixingEnabled(config_.nice_partial_mixing);
                        mt32_partial_limit_ = synth->getPartialCount();

                        // Replay all previously received SysEx (e.g., MUSIC\Y.MID 83 custom SFX timbres) & Program Changes
                        // so changing CM-32L polyphony or reloading ROMs mid-game NEVER wipes Falcon's custom SFX patches!
                        for (const auto& sx : sysex_history_) {
                            if (!sx.empty()) {
                                synth->playSysexNow(sx.data(), static_cast<uint32_t>(sx.size()));
                            }
                        }
                        for (int ch = 0; ch < 16; ++ch) {
                            if (chan_prog_[ch] >= 0) {
                                uint32_t pmsg = static_cast<uint32_t>(0xC0 | ch) | (static_cast<uint32_t>(chan_prog_[ch]) << 8);
                                synth->playMsgNow(pmsg);
                            }
                        }
                        std::memset(active_midi_notes_, 0, sizeof(active_midi_notes_));

                        loaded_ctrl_rom_path_ = ctrl_path;
                        loaded_pcm_rom_path_ = pcm_path;
                        mt32_synth_ = std::move(synth);
                        mt32_ready_ = true;
                        mt32_status_ = "CM-32L / MT-32 Active (" + fs::path(ctrl_path).filename().string() +
                                       ", " + std::to_string(mt32_partial_limit_) + " partials)";
                        loaded_ok = true;
                    }
                }
            }
        }
    }
    invalidate_proxy_bank();
    return loaded_ok;
}

void AudioEngine::invalidate_proxy_bank() {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    if (baker_synth_) {
        baker_synth_->close();
        baker_synth_.reset();
    }
    baked_cache_.clear();
    for (auto& v : proxy_voices_) v.active = false;
    proxy_active_voices_ = 0;
    proxy_baked_count_ = 0;
    baked_sysex_count_ = 0;
    proxy_bank_dirty_ = true;
    core_prebaked_ = false;
    tel_jet_cockpit_ = {};
    tel_jet_outside_ = {};
    tel_ab_ = {};
    tel_wind_ = {};
    wind_ctrl_env_ = 0.0f;
    tel_flyby_3d_ = {};
}

AudioEngine::SfxBus AudioEngine::classify_sfx_bus(uint8_t prog) {
    switch (prog) {
        case 39: case 87: case 100: case 108:
            return SfxBus::Engine;
        case 47: case 98: case 99: case 101: case 102: case 103: case 106: case 124:
            return SfxBus::Avionics;
        default:
            return SfxBus::Weapons;
    }
}

void AudioEngine::pan_to_lr(float pan_m1_p1, float gain, float& out_l, float& out_r) {
    float p = std::clamp(pan_m1_p1, -1.0f, 1.0f);
    float norm = (p + 1.0f) * 0.5f; // 0 = hard left, 0.5 = center, 1 = hard right
    constexpr float kHalfPi = 1.57079632679f;
    out_l = gain * std::cos(norm * kHalfPi) * 1.41421356f;
    out_r = gain * std::sin(norm * kHalfPi) * 1.41421356f;
}

void AudioEngine::update_flight_telemetry(const FlightAudioTelemetry& tel) {
    set_in_flight(tel.in_flight);
    std::lock_guard<std::mutex> plock(proxy_mutex_);
    flight_tel_ = tel;
}

void AudioEngine::prebake_core_sfx() {
    if (!mt32_ready_ || config_.music_backend != MusicBackend::CM32L || !config_.use_cm32l_sample_proxy) return;
    // Pre-bake Falcon 3.0's core Y.MID SFX timbres upfront on entering flight (~150 ms once)
    // so there is ZERO mid-dogfight first-shot hitch!
    static constexpr struct { uint8_t prog; uint8_t note; } kCore[] = {
        {39, 53},  // Jetengine
        {87, 53},  // Afterburn2
        {100, 53}, // JetOutside
        {108, 60}, // Wind
        {43, 59},  // machinegun (M61A1 Vulcan)
        {104, 60}, // Chaff
        {109, 60}, // Flare
        {42, 60},  // missileFir
        {47, 75},  // SquareWave (AMRAAM/radar lock)
        {98, 70},  // MissGrowl
        {99, 72},  // MissLock
        {63, 60},  // DistExplo2
        {81, 60},  // DistExplo4
        {97, 60},  // PlaneHit
        {46, 60},  // tripleA
        {101, 64}, // EnemyLock
        {102, 64}, // StallWarn
        {124, 64}, // EnemyLaunc
        {106, 41}, // Click1
    };
    for (const auto& c : kCore) {
        uint16_t k = 0;
        get_or_bake_sample(c.prog, c.note, k);
    }
    std::lock_guard<std::mutex> plock(proxy_mutex_);
    core_prebaked_ = true;
}

void AudioEngine::spawn_direct_oneshot(uint8_t prog, uint8_t note, float gain, float pan_m1_p1, int dedup_ms) {
    if (fast_boot_mute_ || !mt32_ready_ || config_.music_backend != MusicBackend::CM32L ||
        !config_.use_cm32l_sample_proxy || !in_flight_.load(std::memory_order_relaxed)) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    if (dedup_ms > 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_prog_trigger_[prog]).count();
        if (elapsed < dedup_ms) return;
        last_prog_trigger_[prog] = now;
    }
    uint16_t key = 0;
    const BakedSample* smp = get_or_bake_sample(prog, note, key);
    if (!smp) return;

    float gl = gain, gr = gain;
    pan_to_lr(config_.spatial_3d_audio ? pan_m1_p1 : 0.0f, gain, gl, gr);

    std::lock_guard<std::mutex> plock(proxy_mutex_);
    int slot = -1;
    uint64_t oldest_seq = UINT64_MAX;
    int oldest_slot = 0;
    for (int i = 0; i < kMaxProxyVoices; ++i) {
        if (!proxy_voices_[i].active) { slot = i; break; }
        if (proxy_voices_[i].birth_seq < oldest_seq) { oldest_seq = proxy_voices_[i].birth_seq; oldest_slot = i; }
    }
    if (slot < 0) slot = oldest_slot;

    ProxyVoice& v = proxy_voices_[slot];
    v.active = true;
    v.looping = false;
    v.releasing = false;
    v.is_gun_fallback = false;
    v.bus = classify_sfx_bus(prog);
    v.midi_ch = 2;
    v.prog = prog;
    v.note = note;
    v.sample_key = key;
    v.phase = 0.0;
    v.phase_inc = 1.0;
    v.env = 1.0f;
    v.vel_gain = 1.0f;
    v.gain_l = gl;
    v.gain_r = gr;
    v.target_gain_l = gl;
    v.target_gain_r = gr;
    v.birth_seq = ++voice_seq_;
}

void AudioEngine::trigger_chaff_sfx() {
    // Alternating left/right ALE-40 rear countermeasure dispenser stereo placement!
    float pan = ((++cm_pan_toggle_ & 1u) != 0) ? -0.32f : 0.32f;
    spawn_direct_oneshot(104, 60, 1.05f, pan, 240);
}

void AudioEngine::trigger_flare_sfx() {
    float pan = ((++cm_pan_toggle_ & 1u) != 0) ? 0.32f : -0.32f;
    spawn_direct_oneshot(109, 60, 1.10f, pan, 240);
}

void AudioEngine::trigger_missile_launch_sfx() {
    // Alternating left/right wingtip rail stereo whoosh (CM-32L Program 42 'missileFir')!
    float pan = ((++cm_pan_toggle_ & 1u) != 0) ? -0.35f : 0.35f;
    spawn_direct_oneshot(42, 60, 1.15f, pan, 260);
}

void AudioEngine::play_test_tone(bool test_sb16_voice) {
    if (test_sb16_voice) {
        // Synthesize a clean two-tone radio squelch / cockpit chime into the SB16 PCM bus
        if (!config_.enable_sb16_sfx) return;
        std::lock_guard<std::mutex> lock(sb_pcm_mutex_);
        constexpr int kSamples = 44100 / 3; // ~333 ms
        sb_pcm_queue_.reserve(sb_pcm_queue_.size() + kSamples);
        for (int i = 0; i < kSamples; ++i) {
            float t = static_cast<float>(i) / 44100.0f;
            float env = std::min(1.0f, t / 0.015f) * std::exp(-t * 7.0f);
            float f = (t < 0.14f) ? 880.0f : 1174.66f; // A5 -> D6 cockpit attention chime
            sb_pcm_queue_.push_back(std::sin(2.0f * 3.14159265f * f * t) * 0.45f * env);
        }
        return;
    }

    // Play a quick brass/synth triad on live Roland CM-32L / MT-32
    std::lock_guard<std::mutex> lock(mt32_mutex_);
    if (mt32_ready_ && mt32_synth_ && config_.music_backend == MusicBackend::CM32L) {
        const uint8_t ch = 1; // MIDI Ch 2 (part 1)
        mt32_synth_->playMsgNow(static_cast<uint32_t>(0xC0u | ch) | (50u << 8)); // Synth Brass / Strings
        const uint8_t notes[3] = {57, 64, 69}; // A3 - E4 - A4 power fifth
        for (uint8_t n : notes) {
            mt32_synth_->playMsgNow(static_cast<uint32_t>(0x90u | ch) | (static_cast<uint32_t>(n) << 8) | (96u << 16));
        }
        mt32_test_note_blocks_ = 16; // ~370 ms at 1024-frame blocks (44.1 kHz)
    }
}

void AudioEngine::set_in_flight(bool in_flight) {
    bool prev = in_flight_.exchange(in_flight, std::memory_order_relaxed);
    if (prev == in_flight) return;

    if (!in_flight) {
        // Exited 3D combat back to War Room / Debriefing — cleanly fade out all in-flight SFX proxy voices!
        proxy_release_all();
    } else {
        // Cut any lingering pre-flight cutscene / loadscreen audio (e.g. runway engine startup sound
        // when skipping the pre-flight video early!): flush SB16 PCM/DAC buffer + silence all 16 MT-32 channels!
        {
            std::lock_guard<std::mutex> slock(sb_pcm_mutex_);
            sb_pcm_queue_.clear();
            sb_pcm_read_pos_ = 0;
            sb_dac_prev_ = 0.0f;
            sb_dac_lp_ = 0.0f;
        }
        {
            std::lock_guard<std::mutex> mlock(mt32_mutex_);
            if (mt32_ready_ && mt32_synth_ && config_.music_backend == MusicBackend::CM32L) {
                for (uint8_t ch = 0; ch < 16; ++ch) {
                    // MT-32 uses CC 123 (0x7B = All Notes Off); also send CC 120 (0x78 = All Sound Off)
                    mt32_synth_->playMsgNow(static_cast<uint32_t>(0xB0u | ch) | (123u << 8));
                    mt32_synth_->playMsgNow(static_cast<uint32_t>(0xB0u | ch) | (120u << 8));
                }
            }
        }
        if (stream_) {
            SDL_ClearAudioStream(stream_);
        }

        // Reset stale War Room music pan / expression / pitch-bend on Ch 2..8 so cockpit SFX start dead-center!
        for (uint8_t ch = 1; ch <= 7; ++ch) {
            chan_pan_[ch] = 64;
            chan_expr_[ch] = 127;
            chan_bend_semi_[ch] = 0.0f;
        }
        if (config_.use_cm32l_sample_proxy && mt32_ready_ && config_.music_backend == MusicBackend::CM32L) {
            if (!core_prebaked_ && sysex_history_.size() >= 64) {
                prebake_core_sfx();
            }
        }
    }
}

uint8_t AudioEngine::mpu401_read(uint16_t port) {
    if (port == 0x331) {
        // Status port: bit 7 = 0 if data available to read, bit 6 = 0 if ready for write
        return mpu_rx_queue_.empty() ? 0x80 : 0x00;
    }
    if (port == 0x330) {
        if (!mpu_rx_queue_.empty()) {
            uint8_t v = mpu_rx_queue_.front();
            mpu_rx_queue_.erase(mpu_rx_queue_.begin());
            return v;
        }
        return 0xFE;
    }
    return 0xFF;
}

void AudioEngine::mpu401_write(uint16_t port, uint8_t val) {
    if (port == 0x331) {
        // Command port
        if (val == 0xFF) {
            mpu_uart_mode_ = false;
            mpu_rx_queue_.push_back(0xFE); // ACK
        } else if (val == 0x3F) {
            mpu_uart_mode_ = true;
            mpu_rx_queue_.push_back(0xFE); // ACK
        } else {
            mpu_rx_queue_.push_back(0xFE); // ACK other commands
        }
        return;
    }
    if (port == 0x330) {
        process_midi_byte(val);
    }
}

void AudioEngine::process_midi_byte(uint8_t b) {
    midi_bytes_rx_++;
    if (b >= 0xF8) return; // Realtime ignore

    if (b == 0xF0) {
        in_sysex_ = true;
        midi_buf_.clear();
        midi_buf_.push_back(b);
        return;
    }
    if (b == 0xF7) {
        if (in_sysex_) {
            midi_buf_.push_back(b);
            sysex_rx_++;
            {
                std::lock_guard<std::mutex> lock(mt32_mutex_);
                if (sysex_history_.size() < 512) {
                    sysex_history_.push_back(midi_buf_);
                }
                if (trace_audio_) {
                    std::printf("[SYSEX #%llu] len=%zu head=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                                static_cast<unsigned long long>(sysex_rx_), midi_buf_.size(),
                                midi_buf_.size()>0?midi_buf_[0]:0, midi_buf_.size()>1?midi_buf_[1]:0,
                                midi_buf_.size()>2?midi_buf_[2]:0, midi_buf_.size()>3?midi_buf_[3]:0,
                                midi_buf_.size()>4?midi_buf_[4]:0, midi_buf_.size()>5?midi_buf_[5]:0,
                                midi_buf_.size()>6?midi_buf_[6]:0, midi_buf_.size()>7?midi_buf_[7]:0);
                }
                if (mt32_ready_ && mt32_synth_ && config_.music_backend == MusicBackend::CM32L) {
                    // Apply immediately so SysEx bursts (e.g., MUSIC\Y.MID 83-patch SFX bank)
                    // NEVER overflow libmt32emu's event queue — even during fast-boot!
                    mt32_synth_->playSysexNow(midi_buf_.data(), static_cast<uint32_t>(midi_buf_.size()));
                }
            }
            {
                std::lock_guard<std::mutex> plock(proxy_mutex_);
                proxy_bank_dirty_ = true;
            }
            in_sysex_ = false;
            midi_buf_.clear();
        }
        return;
    }
    if (in_sysex_) {
        midi_buf_.push_back(b);
        return;
    }

    if (b & 0x80) {
        running_status_ = b;
        midi_buf_.clear();
        midi_buf_.push_back(b);
        if (midi_data_len(b) == 0) {
            midi_buf_.clear();
        }
    } else if (running_status_ != 0) {
        if (midi_buf_.empty()) {
            midi_buf_.push_back(running_status_);
        }
        midi_buf_.push_back(b);
        int expected = 1 + midi_data_len(running_status_);
        if (static_cast<int>(midi_buf_.size()) >= expected) {
            uint32_t msg = midi_buf_[0];
            if (midi_buf_.size() > 1) msg |= (static_cast<uint32_t>(midi_buf_[1]) << 8);
            if (midi_buf_.size() > 2) msg |= (static_cast<uint32_t>(midi_buf_[2]) << 16);
            // During fast-boot, keep all Program/Control/Pitch/Note-Off state updates,
            // and suppress only audible Note-On (0x9n with velocity > 0).
            uint8_t st = midi_buf_[0] & 0xF0;
            uint8_t ch = midi_buf_[0] & 0x0F;
            uint8_t d1 = (midi_buf_.size() > 1) ? (midi_buf_[1] & 0x7F) : 0;
            uint8_t vel = (midi_buf_.size() > 2) ? (midi_buf_[2] & 0x7F) : 0;
            const bool use_proxy = config_.use_cm32l_sample_proxy && mt32_ready_ &&
                                   config_.music_backend == MusicBackend::CM32L &&
                                   in_flight_.load(std::memory_order_relaxed);
            if (st == 0xC0) {
                if (ch >= 1 && ch <= 7 && chan_prog_[ch] != static_cast<int16_t>(d1)) {
                    proxy_release_channel(ch, true);
                }
                chan_prog_[ch] = d1;
            } else if (st == 0xB0) {
                if (d1 == 7) {
                    chan_vol_[ch] = vel;
                    proxy_update_channel_params(ch);
                } else if (d1 == 11) {
                    chan_expr_[ch] = vel;
                    proxy_update_channel_params(ch);
                } else if (d1 == 10) {
                    chan_pan_[ch] = vel;
                    proxy_update_channel_params(ch);
                } else if (d1 == 121) {
                    chan_vol_[ch] = 100;
                    chan_expr_[ch] = 127;
                    chan_pan_[ch] = 64;
                    chan_bend_semi_[ch] = 0.0f;
                    proxy_update_channel_params(ch);
                }
            } else if (st == 0xE0) {
                int raw_bend = ((static_cast<int>(vel) << 7) | static_cast<int>(d1)) - 8192;
                chan_bend_semi_[ch] = (static_cast<float>(raw_bend) / 8192.0f) * 2.0f;
                proxy_update_channel_params(ch);
            }

            if (st == 0x90 && vel > 0) {
                note_on_rx_++;
                last_midi_msg_ = msg;
                if (trace_audio_ && !fast_boot_mute_) {
                    const char* pname = (mt32_ready_ && mt32_synth_ && ch >= 1 && ch <= 8)
                                        ? mt32_synth_->getPatchName(ch - 1) : "?";
                    std::printf("[MIDI] NoteOn  ch=%u note=%u vel=%u prog=%d patch='%s'%s\n",
                                ch + 1, d1, vel, chan_prog_[ch], pname ? pname : "?",
                                (use_proxy && ch >= 1 && ch <= 7) ? " [PROXY]" : "");
                }
                if (!fast_boot_mute_) {
                    if (use_proxy && ch >= 1 && ch <= 7) {
                        uint8_t prog = (chan_prog_[ch] >= 0) ? static_cast<uint8_t>(chan_prog_[ch]) : 0;
                        proxy_handle_note_on(ch, prog, d1, vel);
                    } else {
                        // Direct Live CM-32L path: wall-clock throttle for Falcon 3.0's frame-loop-driven SFX
                        bool throttled = false;
                        if (ch >= 2 && ch <= 7) {
                            int prog = chan_prog_[ch];
                            int min_ms = 0;
                            if (prog == 47) {
                                min_ms = 140; // Crisp authentic ~7 Hz AMRAAM/radar lock chirp (SquareWave)
                            } else if (prog == 101 || prog == 102 || prog == 103 || prog == 124) {
                                min_ms = 95;  // RWR EnemyLock / EnemyLaunc / StallWarn / DamageWarn
                            } else if (prog == 98 || prog == 99) {
                                min_ms = 50;  // Only dedup identical same-note spam; allow smooth 66->78 pitch ramp!
                            }
                            if (min_ms > 0) {
                                auto now = std::chrono::steady_clock::now();
                                if (last_sfx_prog_[ch] == prog && last_sfx_note_[ch] == static_cast<int16_t>(d1)) {
                                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                       now - last_sfx_note_time_[ch]).count();
                                    if (elapsed < min_ms) {
                                        throttled = true;
                                    }
                                }
                                if (!throttled) {
                                    last_sfx_note_time_[ch] = now;
                                    last_sfx_prog_[ch] = static_cast<int16_t>(prog);
                                    last_sfx_note_[ch] = static_cast<int16_t>(d1);
                                }
                            }
                        }
                        if (!throttled) {
                            // At >32 partials, auto Note-Off identical re-triggered notes so lock tones never pile up!
                            if (mt32_partial_limit_ > 32 && active_midi_notes_[ch][d1]) {
                                uint32_t off_msg = static_cast<uint32_t>(0x80u | ch) | (static_cast<uint32_t>(d1) << 8);
                                send_short_midi(off_msg);
                            }
                            active_midi_notes_[ch][d1] = 1;
                            send_short_midi(msg);
                        }
                    }
                }
            } else {
                if (trace_audio_ && !fast_boot_mute_ && (st == 0x80 || st == 0x90 || st == 0xC0 || st == 0xB0)) {
                    std::printf("[MIDI] st=0x%02X ch=%u d1=%u d2=%u\n", st, ch + 1, d1, vel);
                }
                bool suppress_off = false;
                if (st == 0x80 || (st == 0x90 && vel == 0)) {
                    if (ch >= 1 && ch <= 7) {
                        proxy_handle_note_off(ch, d1);
                    }
                    // Drop redundant NoteOffs belonging to throttled SFX NoteOns (zero stuck notes!)
                    if (ch >= 2 && ch <= 7 && !active_midi_notes_[ch][d1] &&
                        (last_sfx_prog_[ch] == 47 || last_sfx_prog_[ch] == 101 || last_sfx_prog_[ch] == 124)) {
                        suppress_off = true;
                    }
                    active_midi_notes_[ch][d1] = 0;
                } else if (st == 0xB0 && (d1 == 120 || d1 == 123)) {
                    if (ch >= 1 && ch <= 7) {
                        proxy_release_channel(ch, false);
                    }
                    std::fill(std::begin(active_midi_notes_[ch]), std::end(active_midi_notes_[ch]), 0);
                }
                if (!suppress_off) {
                    send_short_midi(msg);
                }
            }
            midi_buf_.clear();
        }
    }
}

namespace {
struct BakeSpec {
    uint8_t ref_note = 60;
    bool looping = false;
    bool pitch_swept = false; // If true, bake once at ref_note and pitch-shift smoothly in real time
    int hold_ms = 450;
    int tail_ms = 500;
};

BakeSpec get_bake_spec(uint8_t prog, uint8_t played_note) {
    switch (prog) {
        case 39:  return {53, true,  true,  2100, 0};    // Jetengine (sweeps 53..62 with RPM)
        case 87:  return {53, true,  true,  2100, 0};    // Afterburn2 (sweeps from 53+)
        case 100: return {53, true,  true,  2100, 0};    // JetOutside
        case 108: return {60, true,  true,  2100, 0};    // Wind
        case 98:  return {70, true,  true,  1400, 0};    // MissGrowl (sweeps 66..78)
        case 99:  return {72, true,  true,  1400, 0};    // MissLock
        case 43:  return {played_note, false, false,   60,  90};  // machinegun (crisp bassy M61 pulse for ~30Hz BRRRT!)
        case 47:  return {played_note, false, false,   65, 45};   // SquareWave (AMRAAM/radar lock chirp)
        case 101: // EnemyLock
        case 102: // StallWarn
        case 103: // DamageWarn
        case 124: // EnemyLaunc
                  return {played_note, false, false,  140, 120};
        case 42:  return {played_note, false, false,  850, 700};  // missileFir
        case 63:  // DistExplo2
        case 81:  // DistExplo4
        case 97:  // PlaneHit
                  return {played_note, false, false,  750, 1150};
        case 94:  return {played_note, false, false,  900, 550};  // Flyby
        case 46:  // tripleA
        case 110: // TripleA2
                  return {played_note, false, false,  350, 550};
        case 104: // Chaff
        case 109: // Flare
                  return {played_note, false, false,  320, 450};
        case 105: // WheelSkid
        case 106: // Click1
        case 123: // GearDown1
                  return {played_note, false, false,  320, 380};
        default:  return {played_note, false, false,  450, 500};
    }
}
} // namespace

void AudioEngine::compute_ch_stereo_gains(uint8_t ch, float vel_gain, float& out_l, float& out_r) const {
    float vol = (static_cast<float>(chan_vol_[ch]) / 127.0f) * (static_cast<float>(chan_expr_[ch]) / 127.0f);
    vol = std::clamp(vol, 0.0f, 1.25f) * vel_gain;
    // Equal-power MIDI pan (64 = center, 0 = hard left, 127 = hard right)
    float pan_norm = std::clamp(static_cast<float>(chan_pan_[ch]) / 127.0f, 0.0f, 1.0f);
    constexpr float kHalfPi = 1.57079632679f;
    out_l = vol * std::cos(pan_norm * kHalfPi) * 1.41421356f;
    out_r = vol * std::sin(pan_norm * kHalfPi) * 1.41421356f;
}

bool AudioEngine::ensure_baker_ready_unlocked(const std::string& ctrl_path, const std::string& pcm_path,
                                              const std::vector<std::vector<uint8_t>>& sysex_copy) {
    if (ctrl_path.empty() || pcm_path.empty()) return false;
    if (!baker_synth_ || proxy_bank_dirty_ || baked_sysex_count_ != sysex_copy.size()) {
        if (baker_synth_) {
            baker_synth_->close();
            baker_synth_.reset();
        }
        baked_cache_.clear();
        for (auto& v : proxy_voices_) v.active = false;
        proxy_active_voices_ = 0;
        proxy_baked_count_ = 0;

        MT32Emu::FileStream ctrl_file, pcm_file;
        if (!ctrl_file.open(ctrl_path.c_str()) || !pcm_file.open(pcm_path.c_str())) {
            return false;
        }
        const MT32Emu::ROMImage* ctrl_img = MT32Emu::ROMImage::makeROMImage(&ctrl_file);
        const MT32Emu::ROMImage* pcm_img = MT32Emu::ROMImage::makeROMImage(&pcm_file);
        auto synth = std::make_unique<MT32Emu::Synth>();
        bool ok = synth->open(*ctrl_img, *pcm_img, 32);
        MT32Emu::ROMImage::freeROMImage(ctrl_img);
        MT32Emu::ROMImage::freeROMImage(pcm_img);
        if (!ok) return false;

        // Replay Falcon 3.0's exact Y.MID SysEx patch/timbre bank into the offline baker!
        for (const auto& sx : sysex_copy) {
            if (!sx.empty()) {
                synth->playSysexNow(sx.data(), static_cast<uint32_t>(sx.size()));
            }
        }
        // Standardize Part 1 (MIDI Ch 2, status 0xB1/0xC1/0x91) to clean unity volume & center pan
        synth->playMsgNow(0x006407B1u); // CC 7 = 100
        synth->playMsgNow(0x007F0BB1u); // CC 11 = 127
        synth->playMsgNow(0x00400AB1u); // CC 10 = 64 (center)
        synth->playMsgNow(0x004000E1u); // Pitch bend = center

        baker_synth_ = std::move(synth);
        baked_sysex_count_ = sysex_copy.size();
        proxy_bank_dirty_ = false;
    }
    return baker_synth_ != nullptr;
}

const AudioEngine::BakedSample* AudioEngine::get_or_bake_sample(uint8_t prog, uint8_t note, uint16_t& out_key) {
    BakeSpec spec = get_bake_spec(prog, note);
    uint8_t key_note = spec.pitch_swept ? spec.ref_note : note;
    out_key = static_cast<uint16_t>((static_cast<uint16_t>(prog) << 7) | (key_note & 0x7Fu));

    // Snapshot ROM paths & SysEx history without nesting locks
    std::string ctrl_path, pcm_path;
    size_t cur_sysex_count = 0;
    {
        std::lock_guard<std::mutex> mlock(mt32_mutex_);
        if (!mt32_ready_ || loaded_ctrl_rom_path_.empty() || loaded_pcm_rom_path_.empty()) return nullptr;
        ctrl_path = loaded_ctrl_rom_path_;
        pcm_path = loaded_pcm_rom_path_;
        cur_sysex_count = sysex_history_.size();
    }

    std::vector<std::vector<uint8_t>> sysex_copy;
    bool need_reinit = false;
    {
        std::lock_guard<std::mutex> plock(proxy_mutex_);
        if (!proxy_bank_dirty_ && baker_synth_ && baked_sysex_count_ == cur_sysex_count) {
            auto it = baked_cache_.find(out_key);
            if (it != baked_cache_.end() && it->second.valid) {
                return &it->second;
            }
        } else {
            need_reinit = true;
        }
    }

    if (need_reinit) {
        std::lock_guard<std::mutex> mlock(mt32_mutex_);
        sysex_copy = sysex_history_;
    }

    std::lock_guard<std::mutex> plock(proxy_mutex_);
    if (need_reinit || !baker_synth_) {
        if (!ensure_baker_ready_unlocked(ctrl_path, pcm_path, sysex_copy)) {
            return nullptr;
        }
    }
    auto it = baked_cache_.find(out_key);
    if (it != baked_cache_.end() && it->second.valid) {
        return &it->second;
    }

    // Bake this exact CM-32L timbre offline from the user's validated CM-32L ROMs @ 32 kHz -> 44.1 kHz stereo float!
    // Bake at full CC7=127, CC11=127, vel=127, center pan for maximum dynamic range & authentic timbre response.
    baker_synth_->playMsgNow(0x00007BB1u); // Ch 2 All Notes Off (MT-32 standard CC 123)
    baker_synth_->playMsgNow(static_cast<uint32_t>(0xC1u) | (static_cast<uint32_t>(prog) << 8));
    baker_synth_->playMsgNow(0x007F07B1u); // CC 7 = 127
    baker_synth_->playMsgNow(0x007F0BB1u); // CC 11 = 127
    baker_synth_->playMsgNow(0x00400AB1u); // CC 10 = 64 (center)
    baker_synth_->playMsgNow(0x004000E1u); // Pitch bend = center
    // Trigger NoteOn on Ch 2 (MT-32 Part 1) at full velocity 127
    baker_synth_->playMsgNow(static_cast<uint32_t>(0x91u) |
                             (static_cast<uint32_t>(key_note) << 8) |
                             (127u << 16));

    constexpr int kMtRate = 32000;
    constexpr int kOutRate = 44100;
    int hold_frames = std::max(512, (spec.hold_ms * kMtRate) / 1000);
    int tail_frames = (spec.tail_ms * kMtRate) / 1000;

    std::vector<float> raw32k;
    raw32k.reserve(static_cast<size_t>(hold_frames + tail_frames) * 2);

    auto render_chunk = [&](int total_frames, bool early_stop_silence) {
        constexpr int kBlock = 512;
        float buf[kBlock * 2];
        int silent_run = 0;
        int done = 0;
        while (done < total_frames) {
            int n = std::min(kBlock, total_frames - done);
            baker_synth_->render(buf, static_cast<uint32_t>(n));
            raw32k.insert(raw32k.end(), buf, buf + n * 2);
            done += n;
            if (early_stop_silence) {
                float peak = 0.0f;
                for (int i = 0; i < n * 2; ++i) peak = std::max(peak, std::abs(buf[i]));
                if (peak < 0.0004f) {
                    silent_run += n;
                    if (silent_run >= 2048) break;
                } else {
                    silent_run = 0;
                }
            }
        }
    };

    render_chunk(hold_frames, false);
    // Send NoteOff on Ch 2
    baker_synth_->playMsgNow(static_cast<uint32_t>(0x81u) | (static_cast<uint32_t>(key_note) << 8));
    if (!spec.looping && tail_frames > 0) {
        render_chunk(tail_frames, true);
    }

    // Drain any residual reverb tail from the offline baker so the next bake starts 100% silent
    baker_synth_->playMsgNow(0x000078B1u); // Ch 2 All Sound Off
    {
        float drain[512 * 2];
        for (int k = 0; k < 24; ++k) {
            baker_synth_->render(drain, 512);
            float peak = 0.0f;
            for (int i = 0; i < 1024; ++i) peak = std::max(peak, std::abs(drain[i]));
            if (peak < 0.0003f) break;
        }
    }

    uint32_t src_frames = static_cast<uint32_t>(raw32k.size() / 2);
    if (src_frames < 64) return nullptr;

    // Linearly resample 32,000 Hz -> 44,100 Hz stereo float once at bake time!
    uint32_t dst_frames = static_cast<uint32_t>((static_cast<uint64_t>(src_frames) * kOutRate) / kMtRate);
    BakedSample sample;
    sample.ref_note = key_note;
    sample.looping = spec.looping;
    sample.stereo_44k.resize(static_cast<size_t>(dst_frames) * 2);
    double ratio = static_cast<double>(src_frames - 1) / static_cast<double>(std::max<uint32_t>(1, dst_frames - 1));
    for (uint32_t i = 0; i < dst_frames; ++i) {
        double sp = static_cast<double>(i) * ratio;
        uint32_t idx0 = static_cast<uint32_t>(sp);
        uint32_t idx1 = std::min(idx0 + 1, src_frames - 1);
        float frac = static_cast<float>(sp - static_cast<double>(idx0));
        sample.stereo_44k[i * 2 + 0] = raw32k[idx0 * 2 + 0] * (1.0f - frac) + raw32k[idx1 * 2 + 0] * frac;
        sample.stereo_44k[i * 2 + 1] = raw32k[idx0 * 2 + 1] * (1.0f - frac) + raw32k[idx1 * 2 + 1] * frac;
    }

    if (sample.looping) {
        // Measure head peak vs tail RMS to automatically distinguish sustained loops from percussive bursts (e.g. gun)
        uint32_t head_end = std::min<uint32_t>(dst_frames, static_cast<uint32_t>(kOutRate * 0.35));
        float head_peak = 0.0f;
        for (uint32_t i = 0; i < head_end; ++i) {
            head_peak = std::max(head_peak, std::max(std::abs(sample.stereo_44k[i * 2 + 0]),
                                                     std::abs(sample.stereo_44k[i * 2 + 1])));
        }
        uint32_t tail_start = std::min<uint32_t>(dst_frames, static_cast<uint32_t>(kOutRate * 0.45));
        double tail_sq = 0.0;
        uint32_t tail_cnt = 0;
        for (uint32_t i = tail_start; i < dst_frames; ++i) {
            float l = sample.stereo_44k[i * 2 + 0];
            float r = sample.stereo_44k[i * 2 + 1];
            tail_sq += static_cast<double>(l * l + r * r) * 0.5;
            ++tail_cnt;
        }
        float tail_rms = (tail_cnt > 0) ? static_cast<float>(std::sqrt(tail_sq / tail_cnt)) : 0.0f;

        if (head_peak > 1e-4f && tail_rms >= 0.08f * head_peak && dst_frames > static_cast<uint32_t>(kOutRate * 0.65)) {
            // True sustained timbre (Jetengine, Afterburner, Wind, MissGrowl, sustained M61):
            // Equal-power cosine crossfade of final 160 ms into loop_start for 100% click-free infinite looping!
            uint32_t xfade = std::min<uint32_t>(static_cast<uint32_t>(kOutRate * 0.16), dst_frames / 4);
            uint32_t lstart = std::min<uint32_t>(static_cast<uint32_t>(kOutRate * 0.22), dst_frames / 4);
            uint32_t lend = dst_frames - xfade;
            if (lend > lstart + xfade) {
                for (uint32_t k = 0; k < xfade; ++k) {
                    float t = static_cast<float>(k) / static_cast<float>(xfade);
                    float w_in = std::sin(t * 1.5707963f);
                    float w_out = std::cos(t * 1.5707963f);
                    uint32_t dst_i = lstart + k;
                    uint32_t src_i = lend + k;
                    sample.stereo_44k[dst_i * 2 + 0] = sample.stereo_44k[dst_i * 2 + 0] * w_in +
                                                       sample.stereo_44k[src_i * 2 + 0] * w_out;
                    sample.stereo_44k[dst_i * 2 + 1] = sample.stereo_44k[dst_i * 2 + 1] * w_in +
                                                       sample.stereo_44k[src_i * 2 + 1] * w_out;
                }
                sample.stereo_44k.resize(static_cast<size_t>(lend) * 2);
                sample.num_frames = lend;
                sample.loop_start = lstart;
                sample.loop_end = lend;
            } else {
                sample.num_frames = dst_frames;
                sample.loop_start = 0;
                sample.loop_end = dst_frames;
            }
        } else {
            // Percussive decaying burst timbre (e.g., if machinegun decays naturally): loop active body cleanly
            uint32_t active_end = dst_frames;
            float thresh = std::max(0.001f, head_peak * 0.025f);
            while (active_end > static_cast<uint32_t>(kOutRate * 0.08) &&
                   std::abs(sample.stereo_44k[(active_end - 1) * 2 + 0]) < thresh &&
                   std::abs(sample.stereo_44k[(active_end - 1) * 2 + 1]) < thresh) {
                --active_end;
            }
            uint32_t xfade = std::min<uint32_t>(static_cast<uint32_t>(kOutRate * 0.02), active_end / 4);
            uint32_t lstart = std::min<uint32_t>(static_cast<uint32_t>(kOutRate * 0.012), active_end / 5);
            uint32_t lend = active_end - xfade;
            if (lend > lstart + xfade) {
                for (uint32_t k = 0; k < xfade; ++k) {
                    float t = static_cast<float>(k) / static_cast<float>(xfade);
                    uint32_t dst_i = lstart + k;
                    uint32_t src_i = lend + k;
                    sample.stereo_44k[dst_i * 2 + 0] = sample.stereo_44k[dst_i * 2 + 0] * t +
                                                       sample.stereo_44k[src_i * 2 + 0] * (1.0f - t);
                    sample.stereo_44k[dst_i * 2 + 1] = sample.stereo_44k[dst_i * 2 + 1] * t +
                                                       sample.stereo_44k[src_i * 2 + 1] * (1.0f - t);
                }
                sample.stereo_44k.resize(static_cast<size_t>(lend) * 2);
                sample.num_frames = lend;
                sample.loop_start = lstart;
                sample.loop_end = lend;
            } else {
                sample.num_frames = active_end;
                sample.loop_start = 0;
                sample.loop_end = active_end;
            }
        }
    } else {
        // One-shot SFX: trim trailing silence and apply clean 12 ms anti-click tail fade
        uint32_t active_end = dst_frames;
        while (active_end > 256 &&
               std::abs(sample.stereo_44k[(active_end - 1) * 2 + 0]) < 0.0004f &&
               std::abs(sample.stereo_44k[(active_end - 1) * 2 + 1]) < 0.0004f) {
            --active_end;
        }
        uint32_t fade = std::min<uint32_t>(529, active_end / 4); // ~12 ms
        for (uint32_t k = 0; k < fade; ++k) {
            float g = 1.0f - (static_cast<float>(k) / static_cast<float>(fade));
            uint32_t idx = active_end - fade + k;
            sample.stereo_44k[idx * 2 + 0] *= g;
            sample.stereo_44k[idx * 2 + 1] *= g;
        }
        sample.stereo_44k.resize(static_cast<size_t>(active_end) * 2);
        sample.num_frames = active_end;
    }

    // Balanced cockpit mix calibration:
    // Keep locks/RWR at natural CM-32L levels (already crisp & balanced with SB16 voice comms),
    // while bringing naturally quiet low-frequency CM-32L timbres (Jetengine, Afterburn2, machinegun, explosions)
    // up to a rich, punchy, authentic cockpit mix without clipping!
    float cur_pk = 0.0f;
    double cur_sq = 0.0;
    for (float s : sample.stereo_44k) {
        float a = std::abs(s);
        if (a > cur_pk) cur_pk = a;
        cur_sq += static_cast<double>(a) * static_cast<double>(a);
    }
    float cur_rms = sample.stereo_44k.empty() ? 0.0f : static_cast<float>(std::sqrt(cur_sq / sample.stereo_44k.size()));
    float scale = 1.0f;
    if (cur_pk > 1e-4f && cur_rms > 1e-5f) {
        if (prog == 39 || prog == 100) {
            // Jetengine / JetOutside: warm audible F110 cockpit turbine hum
            scale = std::min(0.095f / cur_rms, 0.45f / cur_pk);
            scale = std::clamp(scale, 1.0f, 6.0f);
        } else if (prog == 87) {
            // Afterburn2: deep unmistakable afterburner roar
            scale = std::min(0.115f / cur_rms, 0.52f / cur_pk);
            scale = std::clamp(scale, 1.0f, 18.0f);
        } else if (prog == 108) {
            // Canopy Wind
            scale = std::min(0.055f / cur_rms, 0.30f / cur_pk);
            scale = std::clamp(scale, 1.0f, 4.5f);
        } else if (prog == 43) {
            // M61A1 Vulcan machinegun: thick bassy CM-32L pulse (3-4 overlapping ~32 ms shots sum to full Vulcan roar!)
            scale = std::clamp(0.38f / cur_pk, 1.0f, 3.5f);
        } else if (prog == 104 || prog == 109) {
            // Chaff & Flare ejection pop/hiss: crisp and audible over turbine & RWR!
            scale = std::clamp(0.46f / cur_pk, 1.0f, 4.5f);
        } else if (prog == 42 || prog == 63 || prog == 81 || prog == 97 || prog == 46 || prog == 110 || prog == 94) {
            // Missile launch whoosh, explosions, plane impacts, AAA, flyby
            scale = std::clamp(0.50f / cur_pk, 1.0f, 4.0f);
        }
        if (std::abs(scale - 1.0f) > 0.01f) {
            for (float& s : sample.stereo_44k) s *= scale;
            cur_pk *= scale;
            cur_rms *= scale;
        }
    }

    sample.valid = (sample.num_frames > 32);
    auto [ins_it, _] = baked_cache_.emplace(out_key, std::move(sample));
    proxy_baked_count_ = static_cast<uint32_t>(baked_cache_.size());

    if (trace_audio_) {
        std::printf("[PROXY BAKE] prog=%u note=%u loop=%d frames=%u (%.2fs) scale=%.2fx peak=%.4f rms=%.4f total_baked=%u\n",
                    prog, key_note, ins_it->second.looping ? 1 : 0, ins_it->second.num_frames,
                    static_cast<float>(ins_it->second.num_frames) / 44100.0f, scale, cur_pk, cur_rms, proxy_baked_count_);
    }
    return ins_it->second.valid ? &ins_it->second : nullptr;
}

void AudioEngine::proxy_handle_note_on(uint8_t ch, uint8_t prog, uint8_t note, uint8_t vel) {
    // When Layer 1 Telemetry-Driven Engine/AB/Wind is active, ignore Falcon's choppy MIDI engine notes
    // (39 Jetengine, 87 Afterburn2, 100 JetOutside, 108 Wind) — we drive them smoothly from live RAM!
    if (config_.telemetry_flight_audio && (prog == 39 || prog == 87 || prog == 100 || prog == 108)) {
        return;
    }

    BakeSpec spec = get_bake_spec(prog, note);

    // Per-program wall-clock cadence guard for Falcon's 20-MIPS frame-loop tones across ALL channels
    int min_ms = 0;
    if (prog == 47) {
        min_ms = 140; // Authentic ~7 Hz AMRAAM/radar lock chirp (never piles up across Ch 3 & Ch 4!)
    } else if (prog == 43) {
        min_ms = 30;  // Rapid ~33 Hz M61A1 Vulcan rotary cannon BRRRRRRT!
    } else if (prog == 104 || prog == 109) {
        min_ms = 240; // Dedup cleanly with direct RAM/input chaff & flare triggers!
    } else if (prog == 42) {
        min_ms = 260; // Dedup cleanly with direct RAM player-missile launch trigger!
    } else if (prog == 101 || prog == 102 || prog == 103 || prog == 124) {
        min_ms = 115; // Crisp RWR / Stall / Damage cockpit warnings
    }
    auto now = std::chrono::steady_clock::now();
    if (min_ms > 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_prog_trigger_[prog]).count();
        if (elapsed < min_ms) return;
        last_prog_trigger_[prog] = now;
    }
    if (prog == 43) {
        last_gun_note_ = note;
        last_gun_midi_time_ = now;
    }

    uint16_t sample_key = 0;
    const BakedSample* baked = get_or_bake_sample(prog, note, sample_key);
    if (!baked) return;

    float vel_gain = 0.25f + 0.75f * (static_cast<float>(vel) / 127.0f);
    float gl = 1.0f, gr = 1.0f;
    compute_ch_stereo_gains(ch, vel_gain, gl, gr);

    // Apply 3D spatial stereo placement to combat SFX when spatial_3d_audio is enabled!
    if (config_.spatial_3d_audio) {
        float avg_g = 0.5f * (gl + gr);
        if (prog == 43) {
            // F-16C M61A1 Vulcan cannon sits in the left wing root (or camera-relative in external view!)
            float gpan = flight_tel_.is_external_cam ? flight_tel_.player_ext_pan : -0.24f;
            pan_to_lr(gpan, avg_g, gl, gr);
        } else if (prog == 63 || prog == 81 || prog == 97 || prog == 46 || prog == 110 || prog == 94) {
            // Spatialize explosions, AAA, hits, and flybys toward active bandit direction + gentle stereo spread
            float jitter = (static_cast<float>((++cm_pan_toggle_ * 37u) & 31u) / 31.0f - 0.5f) * 0.35f;
            float span = std::clamp(flight_tel_.combat_pan + jitter, -0.88f, 0.88f);
            pan_to_lr(span, avg_g, gl, gr);
        } else if (prog == 42) {
            // Missile launch alternates left/right wingtip rails!
            float mpan = ((++cm_pan_toggle_ & 1u) != 0) ? -0.35f : 0.35f;
            pan_to_lr(mpan, avg_g, gl, gr);
        }
    }

    double pitch_semi = static_cast<double>(static_cast<int>(note) - static_cast<int>(baked->ref_note)) +
                        static_cast<double>(chan_bend_semi_[ch]);
    double phase_inc = std::clamp(std::pow(2.0, pitch_semi / 12.0), 0.25, 4.0);

    std::lock_guard<std::mutex> plock(proxy_mutex_);

    // 1. For continuous / looping voices (MissGrowl, MissLock, etc.):
    //    Reuse the active continuous voice on this channel+program so pitch glides smoothly with ZERO stacking!
    if (spec.looping) {
        for (auto& v : proxy_voices_) {
            if (v.active && v.looping && v.prog == prog && v.midi_ch == ch) {
                v.midi_ch = ch;
                v.note = note;
                v.sample_key = sample_key;
                v.phase_inc = phase_inc;
                v.vel_gain = vel_gain;
                v.target_gain_l = gl;
                v.target_gain_r = gr;
                v.releasing = false;
                v.is_gun_fallback = false;
                return;
            }
        }
    }

    // 2. Allocate a free slot from the 64-voice pool (stealing oldest only if all 64 slots are somehow full!)
    int slot = -1;
    uint64_t oldest_seq = UINT64_MAX;
    int oldest_slot = 0;
    for (int i = 0; i < kMaxProxyVoices; ++i) {
        if (!proxy_voices_[i].active) {
            slot = i;
            break;
        }
        if (proxy_voices_[i].birth_seq < oldest_seq) {
            oldest_seq = proxy_voices_[i].birth_seq;
            oldest_slot = i;
        }
    }
    if (slot < 0) slot = oldest_slot;

    ProxyVoice& v = proxy_voices_[slot];
    v.active = true;
    v.looping = baked->looping;
    v.releasing = false;
    v.is_gun_fallback = false;
    v.bus = classify_sfx_bus(prog);
    v.midi_ch = ch;
    v.prog = prog;
    v.note = note;
    v.sample_key = sample_key;
    v.phase = 0.0;
    v.phase_inc = phase_inc;
    v.env = baked->looping ? 0.0f : 1.0f; // Looping voices ramp smoothly (6 ms); one-shots keep crisp CM-32L attack!
    v.vel_gain = vel_gain;
    v.gain_l = gl;
    v.gain_r = gr;
    v.target_gain_l = gl;
    v.target_gain_r = gr;
    v.birth_seq = ++voice_seq_;
}

void AudioEngine::proxy_handle_note_off(uint8_t ch, uint8_t note) {
    std::lock_guard<std::mutex> plock(proxy_mutex_);
    for (auto& v : proxy_voices_) {
        // Only release looping/continuous voices on NoteOff — let one-shot explosions/missiles/chaff finish naturally!
        if (v.active && v.looping && v.midi_ch == ch && v.note == note && !v.is_gun_fallback) {
            v.releasing = true;
        }
    }
}

void AudioEngine::proxy_release_channel(uint8_t ch, bool continuous_only) {
    std::lock_guard<std::mutex> plock(proxy_mutex_);
    for (auto& v : proxy_voices_) {
        if (!v.active || v.midi_ch != ch || v.is_gun_fallback) continue;
        if (!continuous_only || v.looping) {
            v.releasing = true;
        }
    }
}

void AudioEngine::proxy_update_channel_params(uint8_t ch) {
    std::lock_guard<std::mutex> plock(proxy_mutex_);
    for (auto& v : proxy_voices_) {
        if (!v.active || v.midi_ch != ch || !v.looping) continue;
        compute_ch_stereo_gains(ch, v.vel_gain, v.target_gain_l, v.target_gain_r);
        auto it = baked_cache_.find(v.sample_key);
        if (it != baked_cache_.end() && it->second.valid) {
            double pitch_semi = static_cast<double>(static_cast<int>(v.note) - static_cast<int>(it->second.ref_note)) +
                                static_cast<double>(chan_bend_semi_[ch]);
            v.phase_inc = std::clamp(std::pow(2.0, pitch_semi / 12.0), 0.25, 4.0);
        }
    }
}

void AudioEngine::proxy_release_all() {
    std::lock_guard<std::mutex> plock(proxy_mutex_);
    for (auto& v : proxy_voices_) {
        if (v.active) v.releasing = true;
    }
}

std::string AudioEngine::last_midi_summary() const {
    uint32_t m = last_midi_msg_;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "SysEx=%llu | NoteOns=%llu | Last=Ch%u Note=%u Vel=%u | SB_DAC=%llu",
                  static_cast<unsigned long long>(sysex_rx_),
                  static_cast<unsigned long long>(note_on_rx_),
                  (m & 0x0Fu) + 1u, (m >> 8) & 0x7Fu, (m >> 16) & 0x7Fu,
                  static_cast<unsigned long long>(sb_dac_rx_));
    return buf;
}

void AudioEngine::send_short_midi(uint32_t msg) {
    std::lock_guard<std::mutex> lock(mt32_mutex_);
    if (mt32_ready_ && mt32_synth_ && config_.music_backend == MusicBackend::CM32L) {
        if (fast_boot_mute_) {
            mt32_synth_->playMsgNow(msg);
        } else {
            mt32_synth_->playMsg(msg);
        }
    }
}

uint8_t AudioEngine::sb16_read(uint16_t port) {
    switch (port & 0x0F) {
        case 0x0A: // DSP Read Data
            if (!sb_read_queue_.empty()) {
                uint8_t v = sb_read_queue_.front();
                sb_read_queue_.erase(sb_read_queue_.begin());
                return v;
            }
            return 0xAA;
        case 0x0C: // DSP Write Buffer Status (bit 7 = 0 ready)
            return 0x00;
        case 0x0E: // DSP Read Buffer Status (bit 7 = 1 data available)
            return sb_read_queue_.empty() ? 0x00 : 0x80;
        default:
            return 0xFF;
    }
}

void AudioEngine::sb16_write(uint16_t port, uint8_t val, double live_pit_hz) {
    switch (port & 0x0F) {
        case 0x06: // DSP Reset
            if (sb_reset_state_ == 1 && val == 0) {
                sb_read_queue_.clear();
                sb_read_queue_.push_back(0xAA); // Ready!
                sb_cmd_bytes_left_ = 0;
            }
            sb_reset_state_ = val;
            break;
        case 0x0C: // DSP Write Command/Data
            if (sb_cmd_bytes_left_ > 0) {
                sb_cmd_params_[sb_cmd_param_idx_++] = val;
                sb_cmd_bytes_left_--;
                if (sb_cmd_bytes_left_ == 0) {
                    if (sb_cmd_ == 0xE0) {
                        sb_read_queue_.push_back(static_cast<uint8_t>(~sb_cmd_params_[0]));
                    } else if (sb_cmd_ == 0x40) {
                        int div = std::max(1, 256 - static_cast<int>(sb_cmd_params_[0]));
                        sb_sample_rate_ = std::clamp(1000000 / div, 4000, 44100);
                    } else if (sb_cmd_ == 0x14) {
                        uint32_t len = (static_cast<uint32_t>(sb_cmd_params_[0]) |
                                       (static_cast<uint32_t>(sb_cmd_params_[1]) << 8)) + 1;
                        sb_dma_req_len_ = len;
                    } else if (sb_cmd_ == 0x48) {
                        sb_block_size_ = (static_cast<uint32_t>(sb_cmd_params_[0]) |
                                         (static_cast<uint32_t>(sb_cmd_params_[1]) << 8)) + 1;
                    } else if (sb_cmd_ == 0x10) {
                        // Direct DAC 8-bit sample (Falcon 3.0 drives this from PIT0 INT 08h @ ~11256 Hz!)
                        double rate = (live_pit_hz >= 2000.0 && live_pit_hz <= 44100.0) ? live_pit_hz : 11256.4;
                        push_sb_dac8(sb_cmd_params_[0], rate);
                    }
                }
                return;
            }
            sb_cmd_ = val;
            sb_cmd_param_idx_ = 0;
            if (val == 0xE1) { // Get DSP Version (SB Pro 3.02 / SB16 4.05)
                sb_read_queue_.push_back(0x03);
                sb_read_queue_.push_back(0x02);
            } else if (val == 0xE0) { // DSP Identification
                sb_cmd_bytes_left_ = 1;
            } else if (val == 0x40 || val == 0x10) {
                sb_cmd_bytes_left_ = 1;
            } else if (val == 0x14 || val == 0x48) {
                sb_cmd_bytes_left_ = 2;
            } else if (val == 0x91 || val == 0x1C) {
                sb_dma_req_len_ = sb_block_size_ ? sb_block_size_ : 1024;
            } else if (val == 0xF2) {
                // Force IRQ
                sb_dma_req_len_ = 1;
            }
            break;
        default:
            break;
    }
}

uint32_t AudioEngine::consume_sb_dma_request(int& out_sample_rate) {
    uint32_t len = sb_dma_req_len_;
    sb_dma_req_len_ = 0;
    out_sample_rate = sb_sample_rate_;
    return len;
}

void AudioEngine::push_sb_dac8(uint8_t sample, double sample_rate_hz) {
    sb_dac_rx_++;
    if (fast_boot_mute_ || !config_.enable_sb16_sfx || sample_rate_hz <= 100.0) return;
    std::lock_guard<std::mutex> lock(sb_pcm_mutex_);
    if (sb_pcm_queue_.size() - sb_pcm_read_pos_ > 44100 * 2) {
        sb_pcm_queue_.clear();
        sb_pcm_read_pos_ = 0;
    }
    float cur = (static_cast<float>(sample) - 128.0f) / 128.0f;
    double step = 44100.0 / sample_rate_hz;
    double start_phase = sb_dac_phase_;
    sb_dac_phase_ += step;
    int out_n = static_cast<int>(sb_dac_phase_);
    sb_dac_phase_ -= static_cast<double>(out_n);
    // Linearly interpolate between previous & current DAC sample + warm ~4.8kHz 1-pole LPF
    constexpr float kLpfAlpha = 0.50f;
    for (int i = 0; i < out_n; ++i) {
        float t = static_cast<float>((static_cast<double>(i) + 1.0 - start_phase) / step);
        t = std::clamp(t, 0.0f, 1.0f);
        float interp = sb_dac_prev_ + (cur - sb_dac_prev_) * t;
        sb_dac_lp_ += kLpfAlpha * (interp - sb_dac_lp_);
        sb_pcm_queue_.push_back(sb_dac_lp_);
    }
    sb_dac_prev_ = cur;
}

void AudioEngine::push_sb_pcm8(const uint8_t* samples, size_t count, int sample_rate) {
    if (fast_boot_mute_ || !config_.enable_sb16_sfx || !samples || count == 0 || sample_rate <= 0) return;
    std::lock_guard<std::mutex> lock(sb_pcm_mutex_);
    // Resample 8-bit unsigned mono PCM to 44100Hz float
    double ratio = 44100.0 / static_cast<double>(sample_rate);
    size_t out_n = static_cast<size_t>(static_cast<double>(count) * ratio);
    if (sb_pcm_queue_.size() - sb_pcm_read_pos_ > 44100 * 4) {
        sb_pcm_queue_.clear();
        sb_pcm_read_pos_ = 0;
    }
    sb_pcm_queue_.reserve(sb_pcm_queue_.size() + out_n);
    for (size_t i = 0; i < out_n; ++i) {
        size_t src = std::min(count - 1, static_cast<size_t>(static_cast<double>(i) / ratio));
        float f = (static_cast<float>(samples[src]) - 128.0f) / 128.0f;
        sb_pcm_queue_.push_back(f);
    }
}

uint8_t AudioEngine::adlib_read(uint16_t /*port*/) {
    return 0x00;
}

void AudioEngine::adlib_write(uint16_t /*port*/, uint8_t /*val*/) {
    // Nuked OPL3 hook point for Phase 2/4 legacy FM music
}

void AudioEngine::pump_silent_mt32(uint32_t frames) {
    std::lock_guard<std::mutex> lock(mt32_mutex_);
    if (mt32_ready_ && mt32_synth_) {
        std::vector<float> tmp(frames * 2, 0.0f);
        mt32_synth_->render(tmp.data(), frames);
    }
}

void AudioEngine::mix_continuous_loop(ContinuousLoopVoice& lv, uint16_t sample_key,
                                      float target_gl, float target_gr, float target_pitch,
                                      float* out_stereo, int num_frames) {
    if (target_gl < 0.0005f && target_gr < 0.0005f && lv.gain_l < 0.0005f && lv.gain_r < 0.0005f) {
        lv.gain_l = 0.0f;
        lv.gain_r = 0.0f;
        return;
    }
    auto it = baked_cache_.find(sample_key);
    if (it == baked_cache_.end() || !it->second.valid || it->second.num_frames < 4) return;
    const BakedSample& smp = it->second;
    const float* pcm = smp.stereo_44k.data();
    const uint32_t max_f = smp.num_frames - 1;
    const uint32_t lstart = smp.loop_start;
    const uint32_t lend = (smp.loop_end > smp.loop_start + 32) ? smp.loop_end : max_f;
    const double loop_len = static_cast<double>(lend - lstart);
    if (lv.phase < static_cast<double>(lstart) || lv.phase >= static_cast<double>(lend)) {
        lv.phase = static_cast<double>(lstart);
    }

    float pitch_clamped = std::clamp(target_pitch, 0.45f, 2.20f);
    for (int i = 0; i < num_frames; ++i) {
        // Smooth ~30 ms gain & pitch interpolation (zero zipper noise, zero clicks!)
        lv.gain_l += (target_gl - lv.gain_l) * 0.0012f;
        lv.gain_r += (target_gr - lv.gain_r) * 0.0012f;
        lv.pitch  += (pitch_clamped - lv.pitch) * 0.0010f;

        uint32_t i0 = static_cast<uint32_t>(lv.phase);
        if (i0 >= max_f) i0 = max_f - 1;
        uint32_t i1 = std::min(i0 + 1, max_f);
        float frac = static_cast<float>(lv.phase - static_cast<double>(i0));
        float sl = pcm[i0 * 2 + 0] * (1.0f - frac) + pcm[i1 * 2 + 0] * frac;
        float sr = pcm[i0 * 2 + 1] * (1.0f - frac) + pcm[i1 * 2 + 1] * frac;
        out_stereo[i * 2 + 0] += sl * lv.gain_l;
        out_stereo[i * 2 + 1] += sr * lv.gain_r;

        lv.phase += static_cast<double>(lv.pitch);
        if (lv.phase >= static_cast<double>(lend)) {
            lv.phase -= loop_len;
        }
    }
}

void AudioEngine::update() {
    if (!stream_) return;

    // Keep ~45ms buffered in SDL audio stream (pump multiple chunks if needed on VRR spikes)
    constexpr int kTargetQueuedBytes = 44100 * 2 * sizeof(float) / 22;
    for (int iter = 0; iter < 4; ++iter) {
        int queued = SDL_GetAudioStreamQueued(stream_);
        if (queued >= kTargetQueuedBytes) return;

        constexpr int kFrames = 1024;
        float mix[kFrames * 2]{};

        // Mix Sound Blaster 16 Digital SFX / Direct DAC Voice Comms FIRST so we know whether radio voice is active
        // (enables clean subtle engine/MT-32 ducking while RIO / AWACS / LSO speaks!)
        bool sb_voice_active = false;
        if (!fast_boot_mute_ && config_.enable_sb16_sfx) {
            std::lock_guard<std::mutex> lock(sb_pcm_mutex_);
            float sfx_gain = config_.master_volume * config_.sb16_sfx_volume;
            float voice_peak = 0.0f;
            for (int i = 0; i < kFrames && sb_pcm_read_pos_ < sb_pcm_queue_.size(); ++i) {
                float raw_s = sb_pcm_queue_[sb_pcm_read_pos_++];
                voice_peak = std::max(voice_peak, std::abs(raw_s));
                float s = raw_s * sfx_gain;
                mix[i * 2 + 0] += s;
                mix[i * 2 + 1] += s;
            }
            sb_voice_active = (voice_peak > 0.025f);
            if (sb_pcm_read_pos_ >= sb_pcm_queue_.size()) {
                sb_pcm_queue_.clear();
                sb_pcm_read_pos_ = 0;
            } else if (sb_pcm_read_pos_ > 16384) {
                sb_pcm_queue_.erase(sb_pcm_queue_.begin(), sb_pcm_queue_.begin() + static_cast<ptrdiff_t>(sb_pcm_read_pos_));
                sb_pcm_read_pos_ = 0;
            }
        }
        // Smooth radio ducking envelope (~3.5 dB dip = 0.66x while SB16 radio comms speak)
        float duck_target = (config_.audio_radio_ducking && sb_voice_active) ? 0.66f : 1.0f;
        radio_duck_env_ += (duck_target - radio_duck_env_) * (sb_voice_active ? 0.25f : 0.06f);

        if (!fast_boot_mute_ && config_.music_backend == MusicBackend::CM32L) {
            std::lock_guard<std::mutex> lock(mt32_mutex_);
            if (mt32_ready_ && mt32_synth_) {
                bool proxy_in_flight = config_.use_cm32l_sample_proxy && in_flight_.load(std::memory_order_relaxed);
                if (proxy_in_flight && !prev_proxy_in_flight_) {
                    for (uint8_t ch = 1; ch <= 7; ++ch) {
                        mt32_synth_->playMsgNow(static_cast<uint32_t>(0xB0u | ch) | (120u << 8));
                    }
                }
                prev_proxy_in_flight_ = proxy_in_flight;
                if (mt32_test_note_blocks_ > 0) {
                    if (--mt32_test_note_blocks_ == 0) {
                        const uint8_t ch = 1;
                        const uint8_t notes[3] = {57, 64, 69};
                        for (uint8_t n : notes) {
                            mt32_synth_->playMsgNow(static_cast<uint32_t>(0x80u | ch) | (static_cast<uint32_t>(n) << 8));
                        }
                    }
                }
                mt32_synth_->setNicePartialMixingEnabled(config_.nice_partial_mixing);
                constexpr int kMtFrames = 744; // ~1024 * 32000 / 44100
                float mt_buf[kMtFrames * 2]{};
                mt32_synth_->render(mt_buf, kMtFrames);
                std::vector<MT32Emu::PartialState> pstates(mt32_partial_limit_);
                mt32_synth_->getPartialStates(pstates.data());
                uint32_t act = 0;
                for (auto ps : pstates) {
                    if (ps != MT32Emu::PartialState_INACTIVE) ++act;
                }
                mt32_active_partials_ = act;

                // Apply Radio Ducking & optional Warm Cockpit Low-End Shelf to live CM-32L output too!
                float live_duck = config_.audio_radio_ducking ? radio_duck_env_ : 1.0f;
                float gain = config_.master_volume * config_.mt32_volume * live_duck;
                bool warm_live = config_.audio_warm_enhance && !proxy_in_flight;
                for (int i = 0; i < kFrames; ++i) {
                    float src_pos = static_cast<float>(i) * (static_cast<float>(kMtFrames - 1) / static_cast<float>(kFrames - 1));
                    int idx = static_cast<int>(src_pos);
                    float frac = src_pos - static_cast<float>(idx);
                    int idx2 = std::min(idx + 1, kMtFrames - 1);
                    float ml = (mt_buf[idx * 2 + 0] * (1.0f - frac) + mt_buf[idx2 * 2 + 0] * frac) * gain;
                    float mr = (mt_buf[idx * 2 + 1] * (1.0f - frac) + mt_buf[idx2 * 2 + 1] * frac) * gain;
                    if (warm_live) {
                        warm_lp_l_ += 0.026f * (ml - warm_lp_l_);
                        warm_lp_r_ += 0.026f * (mr - warm_lp_r_);
                        ml += warm_lp_l_ * 0.24f;
                        mr += warm_lp_r_ * 0.24f;
                    }
                    mix[i * 2 + 0] += ml;
                    mix[i * 2 + 1] += mr;
                }
            }
        }

        // Mix 64-Voice Pre-Baked CM-32L Sample Proxy + Layer 1 Telemetry-Driven Engine/AB/Wind/3D Doppler!
        if (!fast_boot_mute_ && config_.music_backend == MusicBackend::CM32L && mt32_ready_) {
            const bool proxy_on = config_.use_cm32l_sample_proxy && in_flight_.load(std::memory_order_relaxed);
            const bool gun_now = proxy_on && config_.gun_proxy_anti_drop && gun_firing_.load(std::memory_order_relaxed);

            // Ensure core continuous timbres are baked if telemetry flight audio is active
            if (proxy_on && config_.telemetry_flight_audio && !core_prebaked_) {
                prebake_core_sfx();
            }

            // Authentic Baked CM-32L M61 Vulcan Anti-Drop (with 3D left-wing-root / external stereo pan!):
            if (gun_now) {
                auto now = std::chrono::steady_clock::now();
                auto since_gun = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_gun_midi_time_).count();
                if (since_gun >= 34) {
                    last_gun_midi_time_ = now;
                    uint8_t gnote = last_gun_note_;
                    uint16_t gkey = 0;
                    const BakedSample* gsample = get_or_bake_sample(43, gnote, gkey);
                    if (gsample) {
                        std::lock_guard<std::mutex> plock(proxy_mutex_);
                        int slot = -1;
                        for (int s = 0; s < kMaxProxyVoices; ++s) {
                            if (!proxy_voices_[s].active) { slot = s; break; }
                        }
                        if (slot >= 0) {
                            float gpan = config_.spatial_3d_audio
                                         ? (flight_tel_.is_external_cam ? flight_tel_.player_ext_pan : -0.24f)
                                         : 0.0f;
                            float gl = 1.0f, gr = 1.0f;
                            pan_to_lr(gpan, 1.0f, gl, gr);
                            ProxyVoice& v = proxy_voices_[slot];
                            v.active = true;
                            v.looping = false;
                            v.releasing = false;
                            v.is_gun_fallback = true;
                            v.bus = SfxBus::Weapons;
                            v.midi_ch = 2;
                            v.prog = 43;
                            v.note = gnote;
                            v.sample_key = gkey;
                            v.phase = 0.0;
                            v.phase_inc = 1.0;
                            v.env = 1.0f;
                            v.vel_gain = 1.0f;
                            v.gain_l = gl;
                            v.gain_r = gr;
                            v.target_gain_l = gl;
                            v.target_gain_r = gr;
                            v.birth_seq = ++voice_seq_;
                        }
                    }
                }
            }

            std::lock_guard<std::mutex> plock(proxy_mutex_);
            float eng_bus = config_.master_volume * config_.engine_bus_volume * radio_duck_env_;
            float wpn_bus = config_.master_volume * config_.weapons_bus_volume;
            float avn_bus = config_.master_volume * config_.avionics_bus_volume;
            uint32_t live_cnt = 0;

            float engine_mix[kFrames * 2]{};
            // Layer 1: Smooth Telemetry-Driven Engine, Afterburner, Wind & 3D Doppler Nearby Jet!
            if (proxy_on && config_.telemetry_flight_audio) {
                const auto& ft = flight_tel_;
                bool active_flt = ft.in_flight && !ft.paused;
                float rpm01 = std::clamp(ft.rpm_pct / 100.0f, 0.0f, 1.15f);
                float ext_dist_atten = ft.is_external_cam
                                       ? (1.0f / (1.0f + (ft.player_ext_dist_km / 0.35f) * (ft.player_ext_dist_km / 0.35f)))
                                       : 1.0f;
                float ext_pan = (config_.spatial_3d_audio && ft.is_external_cam) ? ft.player_ext_pan : 0.0f;

                // 1a. Cockpit Jetengine (prog 39 @ note 53)
                float ck_gain = (active_flt && !ft.is_external_cam) ? (0.30f + 0.72f * rpm01) * eng_bus : 0.0f;
                float ck_pitch = 0.80f + 0.44f * rpm01;
                mix_continuous_loop(tel_jet_cockpit_, static_cast<uint16_t>((39u << 7) | 53u),
                                    ck_gain, ck_gain, ck_pitch, engine_mix, kFrames);
                if (ck_gain > 0.001f) ++live_cnt;

                // 1b. External View JetOutside (prog 100 @ note 53) with 3D camera-relative pan & distance!
                float ex_g = (active_flt && ft.is_external_cam) ? (0.35f + 0.85f * rpm01) * ext_dist_atten * eng_bus : 0.0f;
                float ex_gl = ex_g, ex_gr = ex_g;
                pan_to_lr(ext_pan, ex_g, ex_gl, ex_gr);
                mix_continuous_loop(tel_jet_outside_, static_cast<uint16_t>((100u << 7) | 53u),
                                    ex_gl, ex_gr, 0.82f + 0.45f * rpm01, engine_mix, kFrames);
                if (ex_g > 0.001f) ++live_cnt;

                // 1c. Afterburner Roar (prog 87 @ note 53) driven smoothly by live AB Stage 1..5!
                float ab01 = (active_flt && ft.ab_stage > 0) ? (0.48f + 0.52f * (static_cast<float>(ft.ab_stage) / 5.0f)) : 0.0f;
                float ab_g = ab01 * (ft.is_external_cam ? (1.15f * ext_dist_atten) : 0.88f) * eng_bus;
                float ab_gl = ab_g, ab_gr = ab_g;
                pan_to_lr(ext_pan, ab_g, ab_gl, ab_gr);
                float ab_pitch = 0.94f + 0.04f * static_cast<float>(ft.ab_stage);
                mix_continuous_loop(tel_ab_, static_cast<uint16_t>((87u << 7) | 53u),
                                    ab_gl, ab_gr, ab_pitch, engine_mix, kFrames);
                if (ab_g > 0.001f) ++live_cnt;

                // 1d. Canopy Airflow & High-G Buffet (prog 108 @ note 60)
                // Wind is a cue, not a bed: silent through normal cruise / maneuvering, it only creeps in
                // at genuinely high KIAS (450+) or hard pulls (5.5G+). Both terms use a squared ramp so the
                // onset is gentle, and the combined level runs through a slow attack/release envelope so
                // jittery G-load samples from RAM never produce audible gusts.
                float spd_lin = std::clamp((ft.airspeed_kts - 450.0f) / 350.0f, 0.0f, 1.0f);
                float g_lin = std::clamp((std::abs(ft.g_load) - 5.5f) / 3.5f, 0.0f, 1.0f);
                float spd01 = spd_lin * spd_lin;
                float g_buffet = g_lin * g_lin;
                float wind_target = (active_flt && !ft.is_external_cam)
                                    ? std::min(1.0f, spd01 * 0.55f + g_buffet * 0.65f) : 0.0f;
                // ~250 ms attack / ~700 ms release at 1024-frame blocks (~23 ms each)
                wind_ctrl_env_ += (wind_target - wind_ctrl_env_) * (wind_target > wind_ctrl_env_ ? 0.089f : 0.033f);
                float wind_g = wind_ctrl_env_ * 0.32f * config_.wind_volume * eng_bus;
                float wind_pitch = 0.85f + 0.30f * spd01 + 0.12f * g_buffet;
                mix_continuous_loop(tel_wind_, static_cast<uint16_t>((108u << 7) | 60u),
                                    wind_g, wind_g, wind_pitch, engine_mix, kFrames);
                if (wind_g > 0.001f) ++live_cnt;

                // 1e. Real 3D Spatial + Doppler Nearby Aircraft / Passing MiG Flyby (prog 100 @ note 53)!
                float fb_g = 0.0f;
                float fb_pan = 0.0f;
                float fb_pitch = 1.0f;
                if (active_flt && config_.spatial_3d_audio && ft.has_nearby_jet && ft.nearby_jet_dist_km < 1.8f) {
                    float d_norm = ft.nearby_jet_dist_km / 0.24f;
                    fb_g = (1.15f / (1.0f + d_norm * d_norm)) * eng_bus;
                    fb_pan = ft.nearby_jet_pan;
                    fb_pitch = std::clamp(ft.nearby_jet_doppler, 0.68f, 1.42f);
                }
                float fb_gl = fb_g, fb_gr = fb_g;
                pan_to_lr(fb_pan, fb_g, fb_gl, fb_gr);
                mix_continuous_loop(tel_flyby_3d_, static_cast<uint16_t>((100u << 7) | 53u),
                                    fb_gl, fb_gr, fb_pitch, engine_mix, kFrames);
                if (fb_g > 0.002f) ++live_cnt;
            } else {
                // Fade telemetry loops smoothly to silence when leaving flight or toggling mode
                mix_continuous_loop(tel_jet_cockpit_, static_cast<uint16_t>((39u << 7) | 53u), 0, 0, 1.0f, engine_mix, kFrames);
                mix_continuous_loop(tel_jet_outside_, static_cast<uint16_t>((100u << 7) | 53u), 0, 0, 1.0f, engine_mix, kFrames);
                mix_continuous_loop(tel_ab_, static_cast<uint16_t>((87u << 7) | 53u), 0, 0, 1.0f, engine_mix, kFrames);
                wind_ctrl_env_ = 0.0f;
                mix_continuous_loop(tel_wind_, static_cast<uint16_t>((108u << 7) | 60u), 0, 0, 1.0f, engine_mix, kFrames);
                mix_continuous_loop(tel_flyby_3d_, static_cast<uint16_t>((100u << 7) | 53u), 0, 0, 1.0f, engine_mix, kFrames);
            }

            // Optional warm cockpit low-end body shelf (~180 Hz) on engine/weapons while preserving CM-32L timbre!
            if (config_.audio_warm_enhance) {
                for (int i = 0; i < kFrames; ++i) {
                    float el = engine_mix[i * 2 + 0];
                    float er = engine_mix[i * 2 + 1];
                    warm_lp_l_ += 0.026f * (el - warm_lp_l_);
                    warm_lp_r_ += 0.026f * (er - warm_lp_r_);
                    mix[i * 2 + 0] += el + warm_lp_l_ * 0.35f;
                    mix[i * 2 + 1] += er + warm_lp_r_ * 0.35f;
                }
            } else {
                for (int i = 0; i < kFrames * 2; ++i) mix[i] += engine_mix[i];
            }

            constexpr float kAtkStep = 1.0f / (44100.0f * 0.006f); // 6 ms click-free attack
            constexpr float kRelStep = 1.0f / (44100.0f * 0.035f); // 35 ms smooth release

            for (auto& v : proxy_voices_) {
                if (!v.active) continue;
                if (!config_.use_cm32l_sample_proxy) {
                    v.releasing = true;
                }
                auto it = baked_cache_.find(v.sample_key);
                if (it == baked_cache_.end() || !it->second.valid || it->second.num_frames < 2) {
                    v.active = false;
                    continue;
                }
                const BakedSample& smp = it->second;
                const float* pcm = smp.stereo_44k.data();
                const uint32_t max_f = smp.num_frames - 1;
                const double loop_len = (smp.looping && smp.loop_end > smp.loop_start)
                                        ? static_cast<double>(smp.loop_end - smp.loop_start) : 0.0;
                float bus_gain = (v.bus == SfxBus::Engine) ? eng_bus :
                                 (v.bus == SfxBus::Avionics) ? avn_bus : wpn_bus;

                for (int i = 0; i < kFrames; ++i) {
                    if (v.releasing) {
                        v.env -= kRelStep;
                        if (v.env <= 0.0f) {
                            v.env = 0.0f;
                            v.active = false;
                            break;
                        }
                    } else if (v.env < 1.0f) {
                        v.env = std::min(1.0f, v.env + kAtkStep);
                    }

                    v.gain_l += (v.target_gain_l - v.gain_l) * 0.01f;
                    v.gain_r += (v.target_gain_r - v.gain_r) * 0.01f;

                    uint32_t i0 = static_cast<uint32_t>(v.phase);
                    if (i0 >= max_f) {
                        if (v.looping && loop_len > 1.0) {
                            while (v.phase >= static_cast<double>(smp.loop_end)) v.phase -= loop_len;
                            i0 = static_cast<uint32_t>(v.phase);
                        } else {
                            v.active = false;
                            break;
                        }
                    }
                    uint32_t i1 = std::min(i0 + 1, max_f);
                    float frac = static_cast<float>(v.phase - static_cast<double>(i0));
                    float sl = pcm[i0 * 2 + 0] * (1.0f - frac) + pcm[i1 * 2 + 0] * frac;
                    float sr = pcm[i0 * 2 + 1] * (1.0f - frac) + pcm[i1 * 2 + 1] * frac;
                    float e = v.env * bus_gain;
                    mix[i * 2 + 0] += sl * v.gain_l * e;
                    mix[i * 2 + 1] += sr * v.gain_r * e;

                    v.phase += v.phase_inc;
                    if (v.looping && loop_len > 1.0 && v.phase >= static_cast<double>(smp.loop_end)) {
                        v.phase -= loop_len;
                    }
                }
                if (v.active) ++live_cnt;
            }
            proxy_active_voices_ = live_cnt;
        }

        // Dedicated non-blocking polyphonic M61A1 20mm Vulcan rotary cannon (6,000 RPM = 100 Hz burst!)
        // Bypasses Falcon 3.0's 1-slot DOS SFX priority mute so the gun ALWAYS roars over RWR/missiles/comms!
        if (!fast_boot_mute_ && config_.polyphonic_gun_sfx) {
            bool firing = gun_firing_.load(std::memory_order_relaxed);
            if (firing || gun_env_ > 0.001f) {
                float gun_gain = config_.master_volume * config_.gun_sfx_volume * 0.42f;
                constexpr float kInvRate = 1.0f / 44100.0f;
                constexpr float kRotaryHz = 100.0f; // 6,000 rounds/min = 100 rounds/sec!
                for (int i = 0; i < kFrames; ++i) {
                    float target = firing ? 1.0f : 0.0f;
                    float rate = firing ? (kInvRate / 0.012f) : (kInvRate / 0.045f);
                    gun_env_ += (target - gun_env_) * std::min(1.0f, rate);

                    gun_phase_ += kRotaryHz * kInvRate;
                    if (gun_phase_ >= 1.0f) gun_phase_ -= 1.0f;

                    // Deep, bassy 100 Hz F-16 M61A1 Vulcan cockpit rumble (warm low-passed thump, no harsh hiss)
                    float pulse = std::exp(-gun_phase_ * 5.0f);
                    float thump = std::sin(gun_phase_ * 6.2831853f) * 0.65f +
                                  std::sin(gun_phase_ * 12.5663706f) * 0.30f;

                    gun_rng_ ^= gun_rng_ << 13;
                    gun_rng_ ^= gun_rng_ >> 17;
                    gun_rng_ ^= gun_rng_ << 5;
                    float noise = (static_cast<float>(gun_rng_ & 0xFFFFu) / 32768.0f) - 1.0f;
                    gun_lp_ += 0.055f * (noise - gun_lp_); // Deep ~400 Hz warm bass cannon body
                    gun_hp_prev_ += 0.015f * (gun_lp_ - gun_hp_prev_);
                    float body = gun_lp_ - gun_hp_prev_ * 0.25f;

                    float sample = (thump * 0.55f + body * (0.65f + 0.95f * pulse)) * gun_env_ * gun_gain;
                    // Subtle cockpit left-side M61 cannon stereo placement (M61 sits in left wing root!)
                    mix[i * 2 + 0] += sample * 1.06f;
                    mix[i * 2 + 1] += sample * 0.94f;
                }
            }
        }

        // Gentle soft-limiter so overlapping CM-32L + SB16 Comms + M61 Vulcan never hard-clip
        for (int i = 0; i < kFrames * 2; ++i) {
            if (mix[i] > 0.95f || mix[i] < -0.95f) {
                mix[i] = std::tanh(mix[i]);
            }
        }

        SDL_PutAudioStreamData(stream_, mix, sizeof(mix));
    }
}

} // namespace f14

#pragma once

#include "audio/audio_engine.h"
#include "dos/vga.h"
#include "input/hotas_mapper.h"
#include "net/net_udp.h"
#include "render/modern_renderer.h"
#include "sim/f14_flight_model.h"

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct x86emu_s;
typedef struct x86emu_s x86emu_t;

namespace f14 {

struct CpuSnapshot {
    uint16_t cs = 0, ip = 0, ss = 0, sp = 0, ds = 0, es = 0;
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0, esi = 0, edi = 0, ebp = 0;
    uint32_t eflags = 0;
    uint64_t total_insns = 0;
    bool halted = false;
    bool exited = false;
    uint8_t exit_code = 0;
    int ems_pages_used = 0;
    int ems_pages_total = 0;
    int proc_depth = 0;
    std::string active_proc;
};

enum class FlightInputMode : int {
    AutoSeamless = 0,    // Both HOTAS & Keyboard work seamlessly on demand
    ForceHotas = 1,      // Always use pre-calibrated HOTAS axes (writes JOY.F14)
    ForceKeyboard = 2    // Always use Keyboard Only
};

class DosMachine {
public:
    static constexpr uint32_t MEM_SIZE = 0x110000; // 1MB + 64KB HMA
    static constexpr uint16_t EMS_FRAME_SEG = 0xD000;
    static constexpr uint32_t EMS_FRAME_ADDR = 0xD0000;
    static constexpr int EMS_PAGE_SIZE = 16384;
    static constexpr int EMS_TOTAL_PAGES = 1024; // 16 MB EMS (ample for all Fleet Defender textures, speech, RIO head!)

    DosMachine(AudioEngine& audio, HotasMapper& hotas, NetUdp& net);
    ~DosMachine();

    // Load either a .COM (e.g. F14.COM) or .EXE (e.g. F.EXE, F14.EXE, F14S.EXE)
    bool load_exe(const std::string& game_dir, const std::string& exe_name);
    const std::string& current_exe() const { return active_proc_name_; }
    const std::string& root_exe() const { return exe_name_; }
    const std::string& game_dir() const { return game_dir_; }
    void reset();

    // Live F-14 flight telemetry read from F14.EXE RAM
    FlightTelemetry read_flight_telemetry() const;
    void read_player_fm_extras(float& weight_lbs, float& throttle_01) const;
    void read_flight_audio_extras(float& rpm_pct, float& throttle_01, int& ab_stage, float& g_load) const;
    bool is_cannon_selected() const { return false; }

    const std::string& loaded_exe_sha256() const { return loaded_exe_sha256_; }
    bool loaded_exe_sha_verified() const { return loaded_exe_sha_verified_; }
    const std::string& detected_world_file() const { return detected_world_file_; }
    bool is_f14_sim_running() const { return in_3d_flight_session_; }

    void write_player_flight_state(const F14SimState& st, bool sync_forward_cam, bool write_throttle = false);
    bool harvest_cockpit_views(Cockpit3D& out);

    // Authoritative host flight-input control & JOY.F14 zero-calibration bypass
    void set_flight_input_mode(FlightInputMode mode) { flight_input_mode_ = mode; }
    FlightInputMode flight_input_mode() const { return flight_input_mode_; }
    bool apply_auto_joystick_calibration(bool write_joy_file = true, int force_mode = -1);
    uint8_t active_dos_joystick_mode() const;

    uint64_t step(uint64_t max_instructions);
    uint64_t step_realtime(double dt_sec);

    void set_cpu_speed_ips(uint64_t ips) { cpu_speed_ips_ = ips; }
    uint64_t cpu_speed_ips() const { return cpu_speed_ips_; }
    void set_flight_cpu_speed_ips(uint64_t ips) { flight_cpu_speed_ips_ = ips; }
    uint64_t flight_cpu_speed_ips() const { return flight_cpu_speed_ips_; }
    void set_auto_turbo(bool enabled) { auto_turbo_in_flight_ = enabled; }
    bool auto_turbo() const { return auto_turbo_in_flight_; }
    uint64_t effective_cpu_speed_ips() const;

    void set_skip_intro(bool enabled) { skip_intro_enabled_ = enabled; }
    bool skip_intro() const { return skip_intro_enabled_; }
    bool is_fast_booting() const { return fast_boot_active_; }

    void on_frame_tick();

    void push_key(uint8_t scancode, uint8_t ascii, bool pressed);
    void set_mouse_state(int x_320, int y_200, uint8_t buttons_mask);
    float kbd_stick_x() const { return kbd_stick_x_; }
    float kbd_stick_y() const { return kbd_stick_y_; }

    void render_vga_rgba_400(uint32_t* out_rgba) const;

    CpuSnapshot snapshot() const;
    const VgaDevice& vga() const { return vga_; }
    const std::deque<std::string>& log_lines() const { return log_lines_; }
    const uint8_t* raw_memory() const { return mem_.data(); }

    void set_paused(bool p) { paused_ = p; }
    bool paused() const { return paused_; }

    uint32_t handle_memio(x86emu_t* emu, uint32_t addr, uint32_t* val, unsigned type);
    int handle_intr(x86emu_t* emu, uint8_t num, unsigned type);

private:
    struct McbBlock {
        uint16_t seg = 0;       // data segment (header is seg - 1)
        uint16_t paras = 0;     // size in paragraphs
        bool free = true;
        uint16_t owner_psp = 0; // PSP segment that owns this block
    };

    struct ProcessFrame {
        uint16_t cs = 0, ip = 0, ss = 0, sp = 0, ds = 0, es = 0;
        uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0, esi = 0, edi = 0, ebp = 0;
        uint32_t eflags = 0;
        uint16_t psp_seg = 0;
        uint16_t dta_seg = 0;
        uint16_t dta_off = 0;
        uint16_t child_psp = 0;
        uint16_t child_env_seg = 0;
        std::string proc_name;
    };

    struct EmsHandle {
        bool in_use = false;
        uint16_t owner_psp = 0;
        std::string name;
        std::vector<uint8_t> data;
    };

    struct OpenFile {
        std::FILE* fp = nullptr;
        std::string host_path;
        bool is_console = false;
        uint16_t owner_psp = 0;
        ~OpenFile() {
            if (fp) {
                std::fclose(fp);
                fp = nullptr;
            }
        }
    };

    struct KeyEvent {
        uint8_t scancode;
        uint8_t ascii;
    };

    AudioEngine& audio_;
    HotasMapper& hotas_;
    NetUdp& net_;
    VgaDevice vga_;

    x86emu_t* emu_ = nullptr;
    std::vector<uint8_t> mem_;
    std::string game_dir_;
    std::string exe_name_;
    std::string active_proc_name_;
    std::string loaded_exe_sha256_;
    bool loaded_exe_sha_verified_ = false;
    std::string current_subdir_;

    // Memory allocator & DOS process stack (supports F14.COM -> F.EXE -> F14.EXE -> F14S.EXE!)
    std::vector<McbBlock> mcbs_;
    std::vector<ProcessFrame> proc_stack_;
    uint16_t psp_seg_ = 0x0100;
    uint16_t dta_seg_ = 0x0100;
    uint16_t dta_off_ = 0x0080;
    uint8_t last_child_exit_code_ = 0;
    uint16_t f14_dgroup_seg_ = 0; // Detected DGROUP segment when F14.EXE is loaded
    mutable uint32_t f14_dgroup_base_ = 0; // Signature-verified linear address of F14.EXE DGROUP
    struct PlaneDbEntry {
        int16_t enum_id = 0;
        std::string name;
        std::string model_3dz;
        int16_t bclass = 0;
    };
    std::vector<PlaneDbEntry> plane_db_;
    void load_plane_db();
    uint32_t find_f14_dgroup() const;

    // EMS 4.0 state
    std::array<EmsHandle, 64> ems_handles_{};
    struct EmsSlot {
        int handle = -1;
        int logical_page = -1;
    };
    std::array<EmsSlot, 4> ems_slots_{};
    std::unordered_map<int, std::array<EmsSlot, 4>> ems_saved_maps_;
    int ems_pages_used_ = 0;

    std::unordered_map<uint16_t, std::unique_ptr<OpenFile>> files_;
    uint16_t alloc_file_handle() const;

    std::vector<std::filesystem::directory_entry> find_results_;
    size_t find_index_ = 0;

    // Keyboard & Mouse state
    std::deque<KeyEvent> key_queue_;
    std::deque<uint8_t> pending_hw_scancodes_;
    uint8_t last_scancode_ = 0;
    uint8_t kbd_shift_flags_ = 0;
    bool kbd_up_ = false, kbd_down_ = false, kbd_left_ = false, kbd_right_ = false;
    float kbd_stick_x_ = 0.0f; // Virtual keyboard roll (-1..+1) for seamless arrow-key flight
    float kbd_stick_y_ = 0.0f; // Virtual keyboard pitch (-1=nose down..+1=nose up)
    void update_kbd_stick(double dt_sec);
    float effective_roll_axis() const;
    float effective_pitch_axis() const;
    int mouse_x_ = 160;
    int mouse_y_ = 100;
    int mouse_mickey_x_ = 0;
    int mouse_mickey_y_ = 0;
    bool mouse_mickey_odd_sent_ = false;
    uint8_t mouse_buttons_ = 0;
    int mouse_min_x_ = 0, mouse_max_x_ = 639;
    int mouse_min_y_ = 0, mouse_max_y_ = 199;
    uint16_t mouse_cb_mask_ = 0;
    uint16_t mouse_cb_seg_ = 0;
    uint16_t mouse_cb_off_ = 0;
    uint16_t pending_mouse_mask_ = 0;
    bool in_mouse_cb_ = false;
    uint16_t mouse_press_cnt_[3]{};
    uint16_t mouse_rel_cnt_[3]{};

    void invoke_mouse_callback(uint16_t event_mask);
    void deliver_hw_irq(uint8_t vec);

    // PIT / Joystick / Real-Time Decoupled Timing
    uint32_t bda_ticks_ = 0;
    uint64_t total_insns_ = 0;
    uint64_t cpu_speed_ips_ = 6000000ULL;        // Fleet Defender menus feel snappy at 6 MIPS
    uint64_t flight_cpu_speed_ips_ = 25000000ULL; // Auto-Turbo in 3D flight
    bool auto_turbo_in_flight_ = true;
    double pit_time_accum_ = 0.0;
    double bda_time_accum_ = 0.0;
    double vga_time_accum_ = 0.0;
    double insn_budget_accum_ = 0.0;
    uint64_t joy_trigger_insn_ = 0;
    uint16_t pit_counter0_ = 0;
    uint16_t pit_reload0_ = 0;
    bool pit_latch_hi_ = false;
    bool pit_write_hi_ = false;
    uint64_t pit_insn_accum_ = 0;

    // 8237 DMA Channels 1 & 3 (Sound Blaster DRQ 1 and stock GOG Fleet Defender DRQ 3!)
    uint16_t dma1_addr_ = 0;
    uint16_t dma1_count_ = 0;
    uint8_t dma1_page_ = 0;
    uint16_t dma3_addr_ = 0;
    uint16_t dma3_count_ = 0;
    uint8_t dma3_page_ = 0;
    uint8_t last_dma_ch_ = 1;
    bool dma_flipflop_ = false;
    uint64_t sb_irq_due_insn_ = 0;
    bool sb_irq_pending_ = false;

    bool paused_ = false;
    bool exited_ = false;
    uint8_t exit_code_ = 0;
    bool skip_intro_enabled_ = true;
    bool fast_boot_active_ = false;
    int fast_boot_stage_ = 0;
    uint64_t fast_boot_key_at_ = 0;
    FlightInputMode flight_input_mode_ = FlightInputMode::AutoSeamless;
    std::string detected_world_file_;
    bool in_3d_flight_session_ = false;
    double disk_turbo_sec_ = 0.0;
    std::deque<std::string> log_lines_;

    void log(const std::string& msg);
    uint8_t* ems_translate(uint32_t addr);
    uint8_t mem_read8(uint32_t addr);
    void mem_write8(uint32_t addr, uint8_t val);
    uint16_t mem_read16(uint32_t addr);
    void mem_write16(uint32_t addr, uint16_t val);

    uint8_t port_in8(uint16_t port);
    void port_out8(uint16_t port, uint8_t val);

    // Program loading helpers (initial & INT 21h AX=4B00h child EXEC)
    bool load_program_into_block(const std::string& host_path, const std::string& display_name,
                                 const std::string& cmdline_tail, uint16_t block_seg, uint16_t block_paras,
                                 uint16_t env_seg);
    void write_mcb_headers();
    bool exec_child_process(const std::string& dos_name, uint16_t pb_seg, uint16_t pb_off);
    void finish_current_process(uint8_t code);

    // HLE Interrupt handlers
    void int_10h();
    void int_11h();
    void int_12h();
    void int_15h();
    void int_16h();
    void int_1ah();
    void int_21h();
    void int_33h();
    void int_67h();
    void int_7ah();

    struct IpxListenEcb {
        uint16_t es = 0;
        uint16_t si = 0;
        uint16_t socket = 0;
    };
    std::vector<IpxListenEcb> ipx_listen_ecbs_;
    void poll_ipx_udp();

    std::string read_dos_string(uint16_t seg, uint16_t off, char term = '\0', size_t max_len = 256);
    std::string resolve_dos_path(const std::string& dos_path);
    uint16_t dos_alloc(uint16_t paras, uint16_t& max_avail, uint16_t owner = 0);
    bool dos_free(uint16_t seg);
    bool dos_resize(uint16_t seg, uint16_t new_paras, uint16_t& max_avail);
    void populate_find_dta(const std::filesystem::directory_entry& entry);
    void set_cf(bool carry);
};

} // namespace f14

#include "dos/dos_machine.h"

#include <x86emu.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace f14 {

namespace {
constexpr uint16_t kHleStubSeg = 0xF000;
constexpr uint16_t kEmmStubSeg = 0xE000;
constexpr uint16_t kRamTopSeg  = 0x9FFF;

std::string to_upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool wildcard_match(const std::string& pattern, const std::string& text) {
    size_t p = 0, t = 0, star = std::string::npos, match = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p; ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            match = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++match;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

std::string compute_sha256(const uint8_t* msg, size_t len) {
    static constexpr uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    auto rotr = [](uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); };
    uint32_t h[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    uint64_t bit_len = static_cast<uint64_t>(len) * 8ULL;
    size_t padded_len = ((len + 9 + 63) / 64) * 64;
    std::vector<uint8_t> buf(padded_len, 0);
    std::memcpy(buf.data(), msg, len);
    buf[len] = 0x80;
    for (int i = 0; i < 8; ++i) buf[padded_len - 1 - i] = static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFFu);
    uint32_t w[64];
    for (size_t off = 0; off < padded_len; off += 64) {
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(buf[off + i*4 + 0]) << 24) |
                   (static_cast<uint32_t>(buf[off + i*4 + 1]) << 16) |
                   (static_cast<uint32_t>(buf[off + i*4 + 2]) << 8)  |
                   (static_cast<uint32_t>(buf[off + i*4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 64);
}

bool is_known_f14_gold_sha(const std::string& sha) {
    return sha == "7fce98eb6021c39580fbb06c7393fb312c3feb4360152eeb032df43882fbc400" || // F14.COM
           sha == "d59ca5cf6a3970bff9067837c7d0c521c5ca0bf36f6f8aea86ff11303ab0f02c" || // F.EXE
           sha == "7959ddd91b2f8ac76a5ac1faf20899a43fe4adfe8249afe946ee5ebd60168e76" || // F14.EXE (stock Gold)
           sha == "f28f78b54467b81a5f0ed631410d50f2ac6c276a2230c832aac7f1c119814b76" || // F14.EXE (PixelWings Ocean Fix)
           sha == "a9cbe18c3e67367bc8f3585176948b0232e6fde13bfbf10af8c542d00156fa6b" || // F14S.EXE
           sha == "02750fd18415eba7ad77c83f951468d66fca9cfa3f870fd3a0b32340f429b7fd";   // R.EXE
}

unsigned s_memio_cb(x86emu_t* emu, u32 addr, u32* val, unsigned type) {
    auto* self = static_cast<DosMachine*>(emu->_private);
    return self->handle_memio(emu, addr, val, type);
}

int s_intr_cb(x86emu_t* emu, u8 num, unsigned type) {
    auto* self = static_cast<DosMachine*>(emu->_private);
    return self->handle_intr(emu, num, type);
}
} // namespace

DosMachine::DosMachine(AudioEngine& audio, HotasMapper& hotas, NetUdp& net)
    : audio_(audio), hotas_(hotas), net_(net), mem_(MEM_SIZE, 0) {
    reset();
}

DosMachine::~DosMachine() {
    if (emu_) {
        x86emu_done(emu_);
        emu_ = nullptr;
    }
}

void DosMachine::log(const std::string& msg) {
    log_lines_.push_back(msg);
    if (log_lines_.size() > 300) {
        log_lines_.pop_front();
    }
}

void DosMachine::reset() {
    if (emu_) {
        x86emu_done(emu_);
        emu_ = nullptr;
    }

    std::fill(mem_.begin(), mem_.end(), 0);
    vga_.reset();
    files_.clear();
    proc_stack_.clear();
    key_queue_.clear();
    pending_hw_scancodes_.clear();
    kbd_shift_flags_ = 0;
    kbd_up_ = kbd_down_ = kbd_left_ = kbd_right_ = false;
    kbd_stick_x_ = 0.0f;
    kbd_stick_y_ = 0.0f;
    mouse_x_ = 160;
    mouse_y_ = 100;
    mouse_mickey_x_ = 0;
    mouse_mickey_y_ = 0;
    mouse_mickey_odd_sent_ = false;
    mouse_buttons_ = 0;
    pending_mouse_mask_ = 0;
    in_mouse_cb_ = false;
    std::memset(mouse_press_cnt_, 0, sizeof(mouse_press_cnt_));
    std::memset(mouse_rel_cnt_, 0, sizeof(mouse_rel_cnt_));
    pit_time_accum_ = 0.0;
    bda_time_accum_ = 0.0;
    vga_time_accum_ = 0.0;
    insn_budget_accum_ = 0.0;
    pit_reload0_ = 0;
    pit_insn_accum_ = 0;
    ems_handles_.fill({});
    ems_slots_.fill({-1, -1});
    ems_saved_maps_.clear();
    ems_pages_used_ = 0;
    mcbs_.clear();
    total_insns_ = 0;
    bda_ticks_ = 0;
    exited_ = false;
    exit_code_ = 0;
    last_child_exit_code_ = 0;
    f14_dgroup_seg_ = 0;
    in_3d_flight_session_ = false;
    current_subdir_.clear();
    fast_boot_active_ = skip_intro_enabled_;
    fast_boot_stage_ = 0;
    fast_boot_key_at_ = 0;
    audio_.set_fast_boot_mute(fast_boot_active_);

    // Reserve EMS handle 0 (OS handle)
    ems_handles_[0].in_use = true;
    ems_handles_[0].name = "SYSTEM";

    emu_ = x86emu_new(0, 0);
    emu_->_private = this;
    x86emu_set_memio_handler(emu_, s_memio_cb);
    x86emu_set_intr_handler(emu_, s_intr_cb);

    // Populate IVT (0000:0000..03FF)
    for (int i = 0; i < 256; ++i) {
        uint16_t seg = (i == 0x67) ? kEmmStubSeg : kHleStubSeg;
        uint16_t off = (i == 0x67) ? 0x0000 : static_cast<uint16_t>(i * 4);
        mem_write16(i * 4 + 0, off);
        mem_write16(i * 4 + 2, seg);
        if (i == 0x33) {
            // DOS mouse detection checks that *(uint8_t*)IVT[33h] != 0xCF (IRET)!
            mem_write8((static_cast<uint32_t>(seg) << 4) + off + 0, 0x90); // NOP
            mem_write8((static_cast<uint32_t>(seg) << 4) + off + 1, 0xCF); // IRET
        } else {
            mem_write8((static_cast<uint32_t>(seg) << 4) + off, 0xCF); // IRET
        }
    }

    // EMMXXXX0 device driver header at E000:000A (checked by EMM.ASM & EMS.ASM)
    const char* emm_sig = "EMMXXXX0";
    std::memcpy(&mem_[(static_cast<uint32_t>(kEmmStubSeg) << 4) + 0x000A], emm_sig, 8);

    // BIOS Data Area (BDA at 0040:0000)
    mem_write16(0x0410, 0x0021); // Equipment word: VGA color, no FPU
    mem_write16(0x0413, 640);    // 640 KB conventional memory
    mem_write8(0x0449, 0x03);    // Video mode 03h
    mem_write16(0x044A, 80);     // Columns
    mem_write16(0x0463, 0x03D4); // CRTC base port
    mem_write8(0x0484, 24);      // Rows - 1
    mem_write16(0x0485, 16);     // Char height
    mem_write8(0x0489, 0x01);    // VGA active, color (bit 1 = 0, checked by MCGA.ASM!)
}

void DosMachine::write_mcb_headers() {
    for (size_t i = 0; i < mcbs_.size(); ++i) {
        const auto& b = mcbs_[i];
        if (b.seg == 0) continue;
        uint32_t hdr = static_cast<uint32_t>(b.seg - 1) << 4;
        if (hdr + 16 >= 0xA0000) continue;
        mem_[hdr + 0] = (i + 1 == mcbs_.size()) ? 'Z' : 'M';
        uint16_t owner = b.free ? 0 : (b.owner_psp ? b.owner_psp : b.seg);
        mem_[hdr + 1] = static_cast<uint8_t>(owner & 0xFF);
        mem_[hdr + 2] = static_cast<uint8_t>(owner >> 8);
        mem_[hdr + 3] = static_cast<uint8_t>(b.paras & 0xFF);
        mem_[hdr + 4] = static_cast<uint8_t>(b.paras >> 8);
    }
}

bool DosMachine::load_program_into_block(const std::string& host_path, const std::string& display_name,
                                         const std::string& cmdline_tail, uint16_t block_seg, uint16_t block_paras,
                                         uint16_t env_seg) {
    std::ifstream f(host_path, std::ios::binary);
    if (!f) {
        log("ERROR: Cannot open " + host_path);
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty()) {
        log("ERROR: Empty executable " + host_path);
        return false;
    }

    std::string sha = compute_sha256(data.data(), data.size());
    bool verified = is_known_f14_gold_sha(sha);
    if (proc_stack_.empty()) {
        loaded_exe_sha256_ = sha;
        loaded_exe_sha_verified_ = verified;
    }
    log("EXEC " + display_name + " (" + std::to_string(data.size()) + " B, sha=" + sha.substr(0, 12) +
        (verified ? " [VERIFIED F14 GOLD])" : ")"));

    // Populate environment block at env_seg
    uint32_t env_addr = static_cast<uint32_t>(env_seg) << 4;
    auto write_env = [&](const std::string& s) {
        std::memcpy(&mem_[env_addr], s.data(), s.size());
        env_addr += static_cast<uint32_t>(s.size());
        mem_[env_addr++] = 0;
    };
    write_env("PATH=C:\\");
    write_env("COMSPEC=C:\\COMMAND.COM");
    write_env("BLASTER=A220 I7 D1 T4");
    mem_[env_addr++] = 0; // Double-null end of env
    mem_[env_addr++] = 1; // Argv0 count = 1
    mem_[env_addr++] = 0;
    write_env("C:\\" + to_upper(display_name));

    // Populate PSP at block_seg
    psp_seg_ = block_seg;
    uint32_t psp_addr = static_cast<uint32_t>(psp_seg_) << 4;
    std::memset(&mem_[psp_addr], 0, 256);
    mem_write8(psp_addr + 0x00, 0xCD); // INT 20h
    mem_write8(psp_addr + 0x01, 0x20);
    uint16_t top_seg = static_cast<uint16_t>(std::min<uint32_t>(kRamTopSeg, static_cast<uint32_t>(block_seg) + block_paras));
    mem_write16(psp_addr + 0x02, top_seg);
    mem_write16(psp_addr + 0x2C, env_seg);

    // Copy command-line tail to PSP:0080 (Pascal length + text + '\r')
    std::string tail = cmdline_tail;
    if (tail.size() > 126) tail.resize(126);
    mem_write8(psp_addr + 0x80, static_cast<uint8_t>(tail.size()));
    for (size_t i = 0; i < tail.size(); ++i) {
        mem_write8(psp_addr + 0x81 + static_cast<uint32_t>(i), static_cast<uint8_t>(tail[i]));
    }
    mem_write8(psp_addr + 0x81 + static_cast<uint32_t>(tail.size()), '\r');

    dta_seg_ = psp_seg_;
    dta_off_ = 0x0080;
    active_proc_name_ = to_upper(display_name);

    bool is_mz = (data.size() >= 28 && ((data[0] == 'M' && data[1] == 'Z') || (data[0] == 'Z' && data[1] == 'M')));
    if (!is_mz) {
        // .COM binary: load raw at PSP:0100
        uint32_t load_addr = psp_addr + 0x0100;
        if (load_addr + data.size() >= 0xA0000) {
            log("ERROR: .COM image exceeds conventional memory");
            return false;
        }
        std::memcpy(&mem_[load_addr], data.data(), data.size());

        x86emu_set_seg_register(emu_, emu_->x86.R_CS_SEL, psp_seg_);
        emu_->x86.R_IP = 0x0100;
        x86emu_set_seg_register(emu_, emu_->x86.R_SS_SEL, psp_seg_);
        emu_->x86.R_SP = 0xFFFE;
        mem_write16((static_cast<uint32_t>(psp_seg_) << 4) + 0xFFFE, 0x0000);
        x86emu_set_seg_register(emu_, emu_->x86.R_DS_SEL, psp_seg_);
        x86emu_set_seg_register(emu_, emu_->x86.R_ES_SEL, psp_seg_);
        emu_->x86.R_AX = 0;
        emu_->x86.R_BX = 0;
        emu_->x86.R_CX = 0x00FF;
        emu_->x86.R_DX = psp_seg_;
        emu_->x86.R_SI = 0x0100;
        emu_->x86.R_DI = 0xFFFE;
        emu_->x86.R_BP = 0x091C;
        emu_->x86.R_EFLG = 0x0202; // IF=1
        log("Loaded .COM " + display_name + " @ " + ([&](){ std::ostringstream o; o << std::hex << std::uppercase << psp_seg_ << ":0100"; return o.str(); })());
        return true;
    }

    // MZ .EXE binary
    auto rd16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
    };
    uint16_t last_page = rd16(2);
    uint16_t pages     = rd16(4);
    uint16_t reloc_cnt = rd16(6);
    uint16_t hdr_paras = rd16(8);
    uint16_t min_alloc = rd16(10);
    uint16_t init_ss   = rd16(14);
    uint16_t init_sp   = rd16(16);
    uint16_t init_ip   = rd16(20);
    uint16_t init_cs   = rd16(22);
    uint16_t reloc_off = rd16(24);

    uint32_t hdr_size = static_cast<uint32_t>(hdr_paras) * 16u;
    uint32_t mz_size = static_cast<uint32_t>(pages - 1) * 512u + (last_page ? last_page : 512u);
    if (mz_size > data.size()) mz_size = static_cast<uint32_t>(data.size());
    if (hdr_size >= mz_size) {
        log("ERROR: Corrupt MZ header in " + display_name);
        return false;
    }

    uint32_t img_size = mz_size - hdr_size;
    uint16_t load_seg = static_cast<uint16_t>(psp_seg_ + 0x10);
    uint32_t load_addr = static_cast<uint32_t>(load_seg) << 4;
    if (load_addr + img_size >= 0xA0000) {
        log("ERROR: MZ image exceeds conventional memory for " + display_name);
        return false;
    }
    std::memcpy(&mem_[load_addr], data.data() + hdr_size, img_size);

    // Zero BSS / min_alloc area above image up to top of allocated block (prevent stale RAM in child EXEs!)
    uint32_t zero_end = std::min<uint32_t>(0xA0000, static_cast<uint32_t>(top_seg) << 4);
    if (load_addr + img_size < zero_end) {
        std::memset(&mem_[load_addr + img_size], 0, zero_end - (load_addr + img_size));
    }

    // Apply MZ relocations
    for (uint16_t i = 0; i < reloc_cnt; ++i) {
        uint16_t r_off = rd16(reloc_off + i * 4);
        uint16_t r_seg = rd16(reloc_off + i * 4 + 2);
        uint32_t target = (static_cast<uint32_t>(load_seg + r_seg) << 4) + r_off;
        uint16_t cur = mem_read16(target);
        mem_write16(target, static_cast<uint16_t>(cur + load_seg));
    }

    // Track F14.EXE DGROUP (= load_seg + 0x55F1, also verified via ReqdGees signature!)
    if (active_proc_name_.find("F14.EXE") != std::string::npos) {
        f14_dgroup_seg_ = static_cast<uint16_t>(load_seg + 0x55F1);
        f14_dgroup_base_ = static_cast<uint32_t>(f14_dgroup_seg_) << 4;
        in_3d_flight_session_ = true;
    } else if (active_proc_name_.find("F.EXE") != std::string::npos) {
        in_3d_flight_session_ = false;
        f14_dgroup_base_ = 0;
    }

    x86emu_set_seg_register(emu_, emu_->x86.R_CS_SEL, static_cast<uint16_t>(load_seg + init_cs));
    emu_->x86.R_IP = init_ip;
    x86emu_set_seg_register(emu_, emu_->x86.R_SS_SEL, static_cast<uint16_t>(load_seg + init_ss));
    emu_->x86.R_SP = init_sp;
    x86emu_set_seg_register(emu_, emu_->x86.R_DS_SEL, psp_seg_);
    x86emu_set_seg_register(emu_, emu_->x86.R_ES_SEL, psp_seg_);
    emu_->x86.R_AX = 0;
    emu_->x86.R_BX = 0;
    emu_->x86.R_CX = 0x00FF;
    emu_->x86.R_DX = psp_seg_;
    emu_->x86.R_SI = init_ip;
    emu_->x86.R_DI = init_sp;
    emu_->x86.R_BP = 0x091C;
    emu_->x86.R_EFLG = 0x0202; // IF=1

    std::ostringstream oss;
    oss << "Loaded MZ " << display_name << " (" << img_size << " B, " << reloc_cnt
        << " relocs, min=" << min_alloc << "p) @ "
        << std::hex << std::uppercase << emu_->x86.R_CS << ":" << emu_->x86.R_IP
        << " SS:SP=" << emu_->x86.R_SS << ":" << emu_->x86.R_SP << " PSP=" << psp_seg_;
    log(oss.str());
    return true;
}

bool DosMachine::load_exe(const std::string& game_dir, const std::string& exe_name) {
    reset();
    game_dir_ = game_dir;
    exe_name_ = exe_name;

    std::string host_path = resolve_dos_path(exe_name);
    uint16_t env_seg = 0x00C0;
    uint16_t init_psp = 0x0100;
    uint16_t total_paras = static_cast<uint16_t>(kRamTopSeg - init_psp);

    mcbs_.clear();
    mcbs_.push_back({init_psp, total_paras, false, init_psp});
    write_mcb_headers();

    if (!load_program_into_block(host_path, exe_name, "", init_psp, total_paras, env_seg)) {
        return false;
    }
    load_plane_db();

    if (flight_input_mode_ != FlightInputMode::ForceKeyboard && hotas_.has_any_axis_bound()) {
        apply_auto_joystick_calibration(true);
    }
    return true;
}

bool DosMachine::exec_child_process(const std::string& dos_name, uint16_t pb_seg, uint16_t pb_off) {
    std::string upper = to_upper(dos_name);
    uint32_t pb = (static_cast<uint32_t>(pb_seg) << 4) + pb_off;
    uint16_t cmd_off = mem_read16(pb + 2);
    uint16_t cmd_seg = mem_read16(pb + 4);

    // Read Pascal-length command-line tail
    std::string tail;
    if (cmd_seg != 0 || cmd_off != 0) {
        uint32_t ca = (static_cast<uint32_t>(cmd_seg) << 4) + cmd_off;
        uint8_t len = mem_read8(ca);
        if (len > 126) len = 126;
        for (uint8_t i = 0; i < len; ++i) {
            char c = static_cast<char>(mem_read8(ca + 1 + i));
            if (c == '\r' || c == '\0') break;
            tail.push_back(c);
        }
    }

    log("DOS 4B00 EXEC: " + dos_name + " tail=\"" + tail + "\"");

    // Fast-Boot Skip Intro: skip PLAYFLC.EXE (open.flc) cleanly when Skip Intro is enabled!
    if (skip_intro_enabled_ && upper.find("PLAYFLC") != std::string::npos) {
        log("FAST-BOOT: Skipped intro animation (" + dos_name + ")");
        last_child_exit_code_ = 0;
        fast_boot_stage_ = 1;
        set_cf(false);
        return true;
    }

    std::string host_path = resolve_dos_path(dos_name);
    if (!fs::exists(host_path)) {
        log("DOS 4B00 FAIL not found: " + dos_name);
        set_cf(true);
        emu_->x86.R_AX = 2; // File not found
        return false;
    }

    // Allocate 32 paragraphs (512 B) for child environment block, then largest remaining free block for child program
    uint16_t max_av = 0;
    uint16_t child_env = dos_alloc(0x20, max_av, 0xFFFF);
    if (child_env == 0) {
        log("DOS 4B00 FAIL out of memory for child env");
        set_cf(true);
        emu_->x86.R_AX = 8;
        return false;
    }

    // Find largest free block
    uint16_t largest = 0;
    for (const auto& b : mcbs_) {
        if (b.free && b.paras > largest) largest = b.paras;
    }
    if (largest < 0x100) {
        dos_free(child_env);
        log("DOS 4B00 FAIL insufficient RAM (largest=" + std::to_string(largest) + "p)");
        set_cf(true);
        emu_->x86.R_AX = 8;
        return false;
    }
    uint16_t child_psp = dos_alloc(largest, max_av, 0xFFFF);
    if (child_psp == 0) {
        dos_free(child_env);
        set_cf(true);
        emu_->x86.R_AX = 8;
        return false;
    }
    // Tag both blocks as owned by child_psp
    for (auto& b : mcbs_) {
        if (b.seg == child_env || b.seg == child_psp) b.owner_psp = child_psp;
    }
    write_mcb_headers();

    // Save parent ProcessFrame (CS:IP already points right after INT 21h!)
    ProcessFrame pf;
    pf.cs = emu_->x86.R_CS;
    pf.ip = emu_->x86.R_IP;
    pf.ss = emu_->x86.R_SS;
    pf.sp = emu_->x86.R_SP;
    pf.ds = emu_->x86.R_DS;
    pf.es = emu_->x86.R_ES;
    pf.eax = emu_->x86.R_EAX;
    pf.ebx = emu_->x86.R_EBX;
    pf.ecx = emu_->x86.R_ECX;
    pf.edx = emu_->x86.R_EDX;
    pf.esi = emu_->x86.R_ESI;
    pf.edi = emu_->x86.R_EDI;
    pf.ebp = emu_->x86.R_EBP;
    pf.eflags = emu_->x86.R_EFLG & ~1u; // Clear CF on return to parent
    pf.psp_seg = psp_seg_;
    pf.dta_seg = dta_seg_;
    pf.dta_off = dta_off_;
    pf.child_psp = child_psp;
    pf.child_env_seg = child_env;
    pf.proc_name = active_proc_name_;
    proc_stack_.push_back(pf);

    if (!load_program_into_block(host_path, dos_name, tail, child_psp, largest, child_env)) {
        proc_stack_.pop_back();
        dos_free(child_psp);
        dos_free(child_env);
        set_cf(true);
        emu_->x86.R_AX = 2;
        return false;
    }

    // Trigger disk turbo burst during child process startup
    disk_turbo_sec_ = std::max(disk_turbo_sec_, 1.5);
    return true;
}

void DosMachine::finish_current_process(uint8_t code) {
    last_child_exit_code_ = code;
    if (proc_stack_.empty()) {
        exited_ = true;
        exit_code_ = code;
        log("DOS EXIT (root) code=" + std::to_string(code));
        x86emu_stop(emu_);
        return;
    }

    ProcessFrame pf = proc_stack_.back();
    proc_stack_.pop_back();
    uint16_t dead_psp = pf.child_psp;
    log("DOS CHILD EXIT " + active_proc_name_ + " code=" + std::to_string(code) + " -> returning to " + pf.proc_name);

    // 1. Close any files opened by the exiting child
    for (auto it = files_.begin(); it != files_.end(); ) {
        if (it->second->owner_psp == dead_psp) {
            it = files_.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Release any EMS handles leaked by the exiting child
    for (size_t i = 1; i < ems_handles_.size(); ++i) {
        if (ems_handles_[i].in_use && ems_handles_[i].owner_psp == dead_psp) {
            ems_pages_used_ -= static_cast<int>(ems_handles_[i].data.size() / EMS_PAGE_SIZE);
            ems_handles_[i] = {};
            for (auto& s : ems_slots_) {
                if (s.handle == static_cast<int>(i)) s = {-1, -1};
            }
        }
    }

    // 3. Reset any IVT vectors pointing into segments owned by the exiting child back to HLE stubs
    for (int vec = 0; vec < 256; ++vec) {
        uint16_t seg = mem_read16(vec * 4 + 2);
        if (seg >= dead_psp && seg < 0xA000) {
            uint16_t stub_seg = (vec == 0x67) ? kEmmStubSeg : kHleStubSeg;
            uint16_t stub_off = (vec == 0x67) ? 0x0000 : static_cast<uint16_t>(vec * 4);
            mem_write16(vec * 4 + 0, stub_off);
            mem_write16(vec * 4 + 2, stub_seg);
        }
    }
    if (mouse_cb_seg_ >= dead_psp && mouse_cb_seg_ < 0xA000) {
        mouse_cb_seg_ = 0;
        mouse_cb_off_ = 0;
        mouse_cb_mask_ = 0;
    }

    // 4. Free all MCB blocks owned by the exiting child
    std::vector<uint16_t> to_free;
    for (const auto& b : mcbs_) {
        if (!b.free && b.owner_psp == dead_psp) to_free.push_back(b.seg);
    }
    for (uint16_t s : to_free) dos_free(s);

    // 5. Restore parent registers, PSP, DTA
    psp_seg_ = pf.psp_seg;
    dta_seg_ = pf.dta_seg;
    dta_off_ = pf.dta_off;
    active_proc_name_ = pf.proc_name;
    in_3d_flight_session_ = (active_proc_name_.find("F14.EXE") != std::string::npos);
    if (!in_3d_flight_session_) f14_dgroup_seg_ = 0;

    x86emu_set_seg_register(emu_, emu_->x86.R_CS_SEL, pf.cs);
    emu_->x86.R_IP = pf.ip;
    x86emu_set_seg_register(emu_, emu_->x86.R_SS_SEL, pf.ss);
    emu_->x86.R_SP = pf.sp;
    x86emu_set_seg_register(emu_, emu_->x86.R_DS_SEL, pf.ds);
    x86emu_set_seg_register(emu_, emu_->x86.R_ES_SEL, pf.es);
    emu_->x86.R_EAX = pf.eax;
    emu_->x86.R_EBX = pf.ebx;
    emu_->x86.R_ECX = pf.ecx;
    emu_->x86.R_EDX = pf.edx;
    emu_->x86.R_ESI = pf.esi;
    emu_->x86.R_EDI = pf.edi;
    emu_->x86.R_EBP = pf.ebp;
    emu_->x86.R_EFLG = pf.eflags & ~1u; // CF = 0 (success)
}

uint8_t* DosMachine::ems_translate(uint32_t addr) {
    if (addr >= EMS_FRAME_ADDR && addr < EMS_FRAME_ADDR + 4 * EMS_PAGE_SIZE) {
        int slot_idx = static_cast<int>((addr - EMS_FRAME_ADDR) / EMS_PAGE_SIZE);
        uint32_t page_off = (addr - EMS_FRAME_ADDR) % EMS_PAGE_SIZE;
        const auto& slot = ems_slots_[slot_idx];
        if (slot.handle >= 0 && slot.handle < static_cast<int>(ems_handles_.size()) &&
            ems_handles_[slot.handle].in_use) {
            auto& h = ems_handles_[slot.handle];
            size_t byte_off = static_cast<size_t>(slot.logical_page) * EMS_PAGE_SIZE + page_off;
            if (byte_off < h.data.size()) {
                return &h.data[byte_off];
            }
        }
    }
    return &mem_[addr % MEM_SIZE];
}

uint8_t DosMachine::mem_read8(uint32_t addr) {
    if (addr >= 0xA0000 && addr < 0xB0000) {
        return vga_.vram_read(static_cast<uint16_t>(addr - 0xA0000));
    }
    return *ems_translate(addr);
}

void DosMachine::mem_write8(uint32_t addr, uint8_t val) {
    if (addr >= 0xA0000 && addr < 0xB0000) {
        vga_.vram_write(static_cast<uint16_t>(addr - 0xA0000), val);
        return;
    }
    *ems_translate(addr) = val;
}

uint16_t DosMachine::mem_read16(uint32_t addr) {
    return static_cast<uint16_t>(mem_read8(addr) | (static_cast<uint16_t>(mem_read8(addr + 1)) << 8));
}

void DosMachine::mem_write16(uint32_t addr, uint16_t val) {
    mem_write8(addr, static_cast<uint8_t>(val & 0xFF));
    mem_write8(addr + 1, static_cast<uint8_t>(val >> 8));
}

uint8_t DosMachine::port_in8(uint16_t port) {
    if (port >= 0x3B0 && port <= 0x3DF) return vga_.port_read(port);
    if (port == 0x330 || port == 0x331) return audio_.mpu401_read(port);
    if (port >= 0x220 && port <= 0x22F) return audio_.sb16_read(port);
    if (port == 0x388 || port == 0x389) return audio_.adlib_read(port);

    switch (port) {
        case 0x40: {
            pit_counter0_ -= 17;
            pit_latch_hi_ = !pit_latch_hi_;
            return pit_latch_hi_ ? static_cast<uint8_t>(pit_counter0_ & 0xFF)
                                 : static_cast<uint8_t>(pit_counter0_ >> 8);
        }
        case 0x60: return last_scancode_;
        case 0x61: return 0x20;
        case 0x64: return key_queue_.empty() ? 0x00 : 0x01;
        case 0x201: {
            uint8_t btns = static_cast<uint8_t>((~hotas_.gameport_buttons() & 0x0F) << 4);
            uint64_t cur_tsc = emu_ ? emu_->x86.R_TSC : total_insns_;
            uint64_t elapsed = (cur_tsc >= joy_trigger_insn_) ? (cur_tsc - joy_trigger_insn_) : 0;
            uint8_t axes = 0;
            for (int i = 0; i < 4; ++i) {
                uint64_t threshold = static_cast<uint64_t>(hotas_.gameport_axis(i)) * 4;
                if (elapsed < threshold) axes |= (1u << i);
            }
            return btns | axes;
        }
        case 0x20: case 0x21: case 0xA0: case 0xA1: return 0x00;
        default: return 0xFF;
    }
}

void DosMachine::port_out8(uint16_t port, uint8_t val) {
    if (port >= 0x3B0 && port <= 0x3DF) { vga_.port_write(port, val); return; }
    if (port == 0x330 || port == 0x331) { audio_.mpu401_write(port, val); return; }
    if (port >= 0x220 && port <= 0x22F) {
        uint32_t reload = (pit_reload0_ == 0) ? 65536u : static_cast<uint32_t>(pit_reload0_);
        double pit_hz = 1193182.0 / static_cast<double>(reload);
        audio_.sb16_write(port, val, pit_hz);
        int sr = 11025;
        uint32_t dma_len = audio_.consume_sb_dma_request(sr);
        if (dma_len > 0) {
            uint8_t  page = (last_dma_ch_ == 3) ? dma3_page_ : dma1_page_;
            uint16_t addr = (last_dma_ch_ == 3) ? dma3_addr_ : dma1_addr_;
            uint32_t phys = (static_cast<uint32_t>(page) << 16) | addr;
            std::vector<uint8_t> pcm(dma_len);
            for (uint32_t i = 0; i < dma_len; ++i) {
                pcm[i] = mem_read8((phys + i) & 0xFFFFF);
            }
            audio_.push_sb_pcm8(pcm.data(), pcm.size(), sr);
            uint64_t delay_insns = std::max<uint64_t>(5000, (static_cast<uint64_t>(dma_len) * 3000000ULL) / std::max(1, sr));
            sb_irq_due_insn_ = total_insns_ + std::min<uint64_t>(delay_insns, 600000ULL);
            sb_irq_pending_ = true;
        }
        return;
    }
    if (port == 0x0C) { dma_flipflop_ = false; return; }
    if (port == 0x02) {
        if (!dma_flipflop_) dma1_addr_ = (dma1_addr_ & 0xFF00) | val;
        else dma1_addr_ = (dma1_addr_ & 0x00FF) | (static_cast<uint16_t>(val) << 8);
        dma_flipflop_ = !dma_flipflop_;
        last_dma_ch_ = 1;
        return;
    }
    if (port == 0x03) {
        if (!dma_flipflop_) dma1_count_ = (dma1_count_ & 0xFF00) | val;
        else dma1_count_ = (dma1_count_ & 0x00FF) | (static_cast<uint16_t>(val) << 8);
        dma_flipflop_ = !dma_flipflop_;
        last_dma_ch_ = 1;
        return;
    }
    if (port == 0x83) { dma1_page_ = val; last_dma_ch_ = 1; return; }
    // DMA Channel 3 (used by stock GOG Fleet Defender CONFIG.SND DRQ=3!)
    if (port == 0x06) {
        if (!dma_flipflop_) dma3_addr_ = (dma3_addr_ & 0xFF00) | val;
        else dma3_addr_ = (dma3_addr_ & 0x00FF) | (static_cast<uint16_t>(val) << 8);
        dma_flipflop_ = !dma_flipflop_;
        last_dma_ch_ = 3;
        return;
    }
    if (port == 0x07) {
        if (!dma_flipflop_) dma3_count_ = (dma3_count_ & 0xFF00) | val;
        else dma3_count_ = (dma3_count_ & 0x00FF) | (static_cast<uint16_t>(val) << 8);
        dma_flipflop_ = !dma_flipflop_;
        last_dma_ch_ = 3;
        return;
    }
    if (port == 0x82) { dma3_page_ = val; last_dma_ch_ = 3; return; }
    if (port == 0x388 || port == 0x389) { audio_.adlib_write(port, val); return; }
    if (port == 0x43) {
        if ((val & 0xC0) == 0) {
            uint8_t access = (val >> 4) & 0x03;
            if (access != 0) pit_write_hi_ = false;
            pit_latch_hi_ = false;
        }
        return;
    }
    if (port == 0x40) {
        if (!pit_write_hi_) {
            pit_reload0_ = (pit_reload0_ & 0xFF00) | val;
            pit_write_hi_ = true;
        } else {
            pit_reload0_ = (pit_reload0_ & 0x00FF) | (static_cast<uint16_t>(val) << 8);
            pit_write_hi_ = false;
        }
        return;
    }
    if (port == 0x201) {
        joy_trigger_insn_ = emu_ ? emu_->x86.R_TSC : total_insns_;
    }
}

uint32_t DosMachine::handle_memio(x86emu_t* /*emu*/, uint32_t addr, uint32_t* val, unsigned type) {
    unsigned op = type & ~0xFFu;
    unsigned size = type & 0xFFu;
    if (op == X86EMU_MEMIO_R || op == X86EMU_MEMIO_X) {
        if (size == X86EMU_MEMIO_8) *val = mem_read8(addr);
        else if (size == X86EMU_MEMIO_16) *val = mem_read16(addr);
        else if (size == X86EMU_MEMIO_32) {
            *val = static_cast<uint32_t>(mem_read16(addr)) | (static_cast<uint32_t>(mem_read16(addr + 2)) << 16);
        }
        return 0;
    }
    if (op == X86EMU_MEMIO_W) {
        if (size == X86EMU_MEMIO_8) mem_write8(addr, static_cast<uint8_t>(*val));
        else if (size == X86EMU_MEMIO_16) mem_write16(addr, static_cast<uint16_t>(*val));
        else if (size == X86EMU_MEMIO_32) {
            mem_write16(addr, static_cast<uint16_t>(*val & 0xFFFF));
            mem_write16(addr + 2, static_cast<uint16_t>(*val >> 16));
        }
        return 0;
    }
    if (op == X86EMU_MEMIO_I) {
        uint16_t port = static_cast<uint16_t>(addr);
        if (size == X86EMU_MEMIO_8) *val = port_in8(port);
        else if (size == X86EMU_MEMIO_16) {
            *val = static_cast<uint32_t>(port_in8(port)) | (static_cast<uint32_t>(port_in8(port + 1)) << 8);
        } else *val = 0xFFFFFFFFu;
        return 0;
    }
    if (op == X86EMU_MEMIO_O) {
        uint16_t port = static_cast<uint16_t>(addr);
        if (size == X86EMU_MEMIO_8) port_out8(port, static_cast<uint8_t>(*val));
        else if (size == X86EMU_MEMIO_16) {
            port_out8(port, static_cast<uint8_t>(*val & 0xFF));
            port_out8(port + 1, static_cast<uint8_t>((*val >> 8) & 0xFF));
        }
        return 0;
    }
    return 0;
}

int DosMachine::handle_intr(x86emu_t* /*emu*/, uint8_t num, unsigned /*type*/) {
    uint16_t ivt_seg = mem_read16(num * 4 + 2);
    if (ivt_seg != kHleStubSeg && ivt_seg != kEmmStubSeg) {
        return 0; // Dispatch to guest's real-mode ISR
    }
    switch (num) {
        case 0x06: {
            uint32_t pc = (static_cast<uint32_t>(emu_->x86.R_CS) << 4) + emu_->x86.R_IP;
            uint8_t prev = mem_read8(pc - 1);
            if (prev >= 0xD8 && prev <= 0xDF) {
                uint8_t modrm = mem_read8(pc);
                uint8_t mod = (modrm >> 6) & 3;
                uint8_t rm = modrm & 7;
                uint16_t extra = 1;
                if (mod == 0 && rm == 6) extra += 2;
                else if (mod == 1) extra += 1;
                else if (mod == 2) extra += 2;
                emu_->x86.R_IP = static_cast<uint16_t>(emu_->x86.R_IP + extra);
                return 1;
            }
            std::ostringstream oss;
            oss << "Invalid Opcode INT 0x06 @ " << std::hex << std::uppercase
                << emu_->x86.R_CS << ":" << emu_->x86.R_IP << " prev=" << static_cast<int>(prev);
            log(oss.str());
            return 1;
        }
        case 0x10: int_10h(); return 1;
        case 0x11: int_11h(); return 1;
        case 0x12: int_12h(); return 1;
        case 0x15: int_15h(); return 1;
        case 0x16: int_16h(); return 1;
        case 0x1A: int_1ah(); return 1;
        case 0x20: finish_current_process(0); return 1;
        case 0x21: int_21h(); return 1;
        case 0x2F:
            if (emu_->x86.R_AX == 0x7A00) {
                emu_->x86.R_AL = 0xFF;
                constexpr uint16_t kIpxOff = 0x03E0;
                uint32_t stub = (static_cast<uint32_t>(kHleStubSeg) << 4) + kIpxOff;
                mem_write8(stub + 0, 0xCD);
                mem_write8(stub + 1, 0x7A);
                mem_write8(stub + 2, 0xCB);
                x86emu_set_seg_register(emu_, emu_->x86.R_ES_SEL, kHleStubSeg);
                emu_->x86.R_DI = kIpxOff;
            } else {
                emu_->x86.R_AL = 0;
            }
            return 1;
        case 0x33: int_33h(); return 1;
        case 0x67: int_67h(); return 1;
        case 0x7A: int_7ah(); return 1;
        case 0xFF: x86emu_stop(emu_); return 1;
        default: {
            std::ostringstream oss;
            oss << "Unhandled HLE INT 0x" << std::hex << std::uppercase << static_cast<int>(num)
                << " AX=" << emu_->x86.R_AX << " @ " << emu_->x86.R_CS << ":" << emu_->x86.R_IP;
            log(oss.str());
            return 1;
        }
    }
}

void DosMachine::set_cf(bool carry) {
    if (carry) emu_->x86.R_EFLG |= 1u;
    else emu_->x86.R_EFLG &= ~1u;
}

std::string DosMachine::read_dos_string(uint16_t seg, uint16_t off, char term, size_t max_len) {
    std::string out;
    uint32_t base = (static_cast<uint32_t>(seg) << 4) + off;
    for (size_t i = 0; i < max_len; ++i) {
        char c = static_cast<char>(mem_read8(base + static_cast<uint32_t>(i)));
        if (c == term) break;
        out.push_back(c);
    }
    return out;
}

uint16_t DosMachine::alloc_file_handle() const {
    for (uint16_t h = 5; h < 255; ++h) {
        if (files_.find(h) == files_.end()) return h;
    }
    return 0xFFFF;
}

std::string DosMachine::resolve_dos_path(const std::string& dos_path) {
    std::string p = dos_path;
    bool absolute = false;
    if (p.size() >= 2 && p[1] == ':') p = p.substr(2);
    if (!p.empty() && (p[0] == '\\' || p[0] == '/')) {
        absolute = true;
        while (!p.empty() && (p[0] == '\\' || p[0] == '/')) p.erase(p.begin());
    }
    std::replace(p.begin(), p.end(), '\\', '/');
    fs::path base = fs::path(game_dir_);
    if (!absolute && !current_subdir_.empty()) {
        base /= current_subdir_;
    }
    fs::path candidate = (base / p).lexically_normal();
    return candidate.string();
}

uint16_t DosMachine::dos_alloc(uint16_t paras, uint16_t& max_avail, uint16_t owner) {
    max_avail = 0;
    for (size_t i = 0; i < mcbs_.size(); ++i) {
        if (mcbs_[i].free) {
            max_avail = std::max(max_avail, mcbs_[i].paras);
            if (mcbs_[i].paras >= paras) {
                uint16_t seg = mcbs_[i].seg;
                uint16_t rem = static_cast<uint16_t>(mcbs_[i].paras - paras);
                mcbs_[i].free = false;
                mcbs_[i].owner_psp = owner ? owner : psp_seg_;
                // Only split if rem > 1 (1 para for new MCB header + >=1 para data).
                // If rem == 1, keep the extra paragraph in mcbs_[i] so total RAM is strictly conserved!
                if (rem > 1) {
                    mcbs_[i].paras = paras;
                    mcbs_.insert(mcbs_.begin() + static_cast<ptrdiff_t>(i + 1),
                                 {static_cast<uint16_t>(seg + paras + 1), static_cast<uint16_t>(rem - 1), true, 0});
                }
                write_mcb_headers();
                return seg;
            }
        }
    }
    return 0;
}

bool DosMachine::dos_free(uint16_t seg) {
    for (size_t i = 0; i < mcbs_.size(); ++i) {
        if (mcbs_[i].seg == seg) {
            mcbs_[i].free = true;
            mcbs_[i].owner_psp = 0;
            if (i + 1 < mcbs_.size() && mcbs_[i + 1].free) {
                mcbs_[i].paras = static_cast<uint16_t>(mcbs_[i].paras + mcbs_[i + 1].paras + 1);
                mcbs_.erase(mcbs_.begin() + static_cast<ptrdiff_t>(i + 1));
            }
            if (i > 0 && mcbs_[i - 1].free) {
                mcbs_[i - 1].paras = static_cast<uint16_t>(mcbs_[i - 1].paras + mcbs_[i].paras + 1);
                mcbs_.erase(mcbs_.begin() + static_cast<ptrdiff_t>(i));
            }
            write_mcb_headers();
            return true;
        }
    }
    return false;
}

bool DosMachine::dos_resize(uint16_t seg, uint16_t new_paras, uint16_t& max_avail) {
    max_avail = 0;
    for (size_t i = 0; i < mcbs_.size(); ++i) {
        if (mcbs_[i].seg == seg) {
            if (i + 1 < mcbs_.size() && mcbs_[i + 1].free) {
                mcbs_[i].paras = static_cast<uint16_t>(mcbs_[i].paras + mcbs_[i + 1].paras + 1);
                mcbs_.erase(mcbs_.begin() + static_cast<ptrdiff_t>(i + 1));
            }
            max_avail = mcbs_[i].paras;
            if (new_paras > mcbs_[i].paras) {
                write_mcb_headers();
                return false;
            }
            uint16_t rem = static_cast<uint16_t>(mcbs_[i].paras - new_paras);
            // Strictly conserve total paragraphs when rem == 1 (cannot fit a 1-para header + >=1-para block)
            if (rem > 1) {
                mcbs_[i].paras = new_paras;
                mcbs_.insert(mcbs_.begin() + static_cast<ptrdiff_t>(i + 1),
                             {static_cast<uint16_t>(seg + new_paras + 1), static_cast<uint16_t>(rem - 1), true, 0});
            }
            write_mcb_headers();
            return true;
        }
    }
    return false;
}

void DosMachine::int_10h() {
    uint8_t ah = emu_->x86.R_AH;
    switch (ah) {
        case 0x00:
            vga_.set_mode(emu_->x86.R_AL);
            mem_write8(0x0449, vga_.mode());
            if (vga_.mode() == 0x13) std::memset(&mem_[0xA0000], 0, 64000);
            break;
        case 0x0E:
            vga_.tty_putc(mem_.data(), static_cast<char>(emu_->x86.R_AL));
            break;
        case 0x0F:
            emu_->x86.R_AL = vga_.mode();
            emu_->x86.R_AH = (vga_.mode() == 0x13) ? 40 : 80;
            emu_->x86.R_BH = 0;
            break;
        case 0x10: {
            uint8_t al = emu_->x86.R_AL;
            if (al == 0x10) {
                vga_.set_dac_color(static_cast<uint8_t>(emu_->x86.R_BX), emu_->x86.R_DH, emu_->x86.R_CH, emu_->x86.R_CL);
            } else if (al == 0x12) {
                uint16_t start = emu_->x86.R_BX;
                uint16_t count = emu_->x86.R_CX;
                uint32_t addr = (static_cast<uint32_t>(emu_->x86.R_ES) << 4) + emu_->x86.R_DX;
                for (uint16_t i = 0; i < count; ++i) {
                    vga_.set_dac_color(static_cast<uint8_t>(start + i),
                                       mem_read8(addr + i * 3 + 0),
                                       mem_read8(addr + i * 3 + 1),
                                       mem_read8(addr + i * 3 + 2));
                }
            } else if (al == 0x15) {
                uint8_t r, g, b;
                vga_.get_dac_color(static_cast<uint8_t>(emu_->x86.R_BX), r, g, b);
                emu_->x86.R_DH = r; emu_->x86.R_CH = g; emu_->x86.R_CL = b;
            } else if (al == 0x17) {
                uint16_t start = emu_->x86.R_BX;
                uint16_t count = emu_->x86.R_CX;
                uint32_t addr = (static_cast<uint32_t>(emu_->x86.R_ES) << 4) + emu_->x86.R_DX;
                for (uint16_t i = 0; i < count; ++i) {
                    uint8_t r, g, b;
                    vga_.get_dac_color(static_cast<uint8_t>(start + i), r, g, b);
                    mem_write8(addr + i * 3 + 0, r);
                    mem_write8(addr + i * 3 + 1, g);
                    mem_write8(addr + i * 3 + 2, b);
                }
            }
            break;
        }
        case 0x12: // Alternate function select (EGA/VGA info)
            if (emu_->x86.R_BL == 0x10) {
                emu_->x86.R_BH = 0x00; // Color mode
                emu_->x86.R_BL = 0x03; // 256 KB VGA
                emu_->x86.R_CH = 0x00;
                emu_->x86.R_CL = 0x09;
            }
            break;
        case 0x1A:
            if (emu_->x86.R_AL == 0x00) {
                emu_->x86.R_AL = 0x1A;
                emu_->x86.R_BL = 0x08; // VGA color
                emu_->x86.R_BH = 0x00;
            }
            break;
        default: break;
    }
}

void DosMachine::int_11h() { emu_->x86.R_AX = mem_read16(0x0410); }
void DosMachine::int_12h() { emu_->x86.R_AX = 640; }

void DosMachine::int_15h() {
    uint8_t ah = emu_->x86.R_AH;
    if (ah == 0x88) {
        emu_->x86.R_AX = 0; // No XMS; use EMS
        set_cf(false);
    } else if (ah == 0x84) {
        // Fleet Defender's JOYSTX.ASM probes ColoSpec via AX=8400h DX=00F0h (expects AX='SG', BX!=0),
        // then reads buttons via AX=8400h DX=0 and instant 16-bit axes via AX=8401h DX=1!
        // Calibrated to exact JOY.F14 range: X/Y/Rudder = 15..840 (center 427), Throttle = 17..836 (836=idle, 17=AB)!
        if (emu_->x86.R_DX == 0x00F0) {
            emu_->x86.R_AX = 0x5347; // 'SG' (MicroProse ColoSpec fast-read signature)
            emu_->x86.R_BX = 0x0201;
            set_cf(false);
        } else if (emu_->x86.R_DX == 0) {
            emu_->x86.R_AL = static_cast<uint8_t>((~hotas_.gameport_buttons() & 0x0F) << 4);
            set_cf(false);
        } else if (emu_->x86.R_DX == 1) {
            auto map_centered = [](float v) -> uint16_t {
                float c = std::clamp(v, -1.0f, 1.0f);
                return static_cast<uint16_t>(std::lround(427.0f + c * 412.0f));
            };
            float roll  = effective_roll_axis();
            float pitch = effective_pitch_axis();
            float yaw   = hotas_.is_axis_active(FlightAxis::Yaw)      ? hotas_.axis_value(FlightAxis::Yaw)      : 0.0f;
            float thr   = hotas_.is_axis_active(FlightAxis::Throttle) ? hotas_.axis_value(FlightAxis::Throttle) : 0.65f;
            emu_->x86.R_AX = map_centered(roll);
            emu_->x86.R_BX = map_centered(pitch);
            emu_->x86.R_CX = map_centered(yaw);
            // Throttle in JOY.F14: 836 = 0% idle, 17 = 100% MAX AB
            emu_->x86.R_DX = static_cast<uint16_t>(std::lround(836.0f - std::clamp(thr, 0.0f, 1.0f) * 819.0f));
            set_cf(false);
        } else {
            set_cf(true);
            emu_->x86.R_AH = 0x86;
        }
    } else {
        set_cf(true);
        emu_->x86.R_AH = 0x86;
    }
}

void DosMachine::int_16h() {
    uint8_t ah = emu_->x86.R_AH;
    if (ah == 0x00 || ah == 0x10) {
        if (!key_queue_.empty()) {
            auto k = key_queue_.front();
            key_queue_.pop_front();
            emu_->x86.R_AH = k.scancode;
            emu_->x86.R_AL = k.ascii;
        } else {
            emu_->x86.R_AX = 0;
        }
    } else if (ah == 0x01 || ah == 0x11) {
        if (!key_queue_.empty()) {
            auto k = key_queue_.front();
            emu_->x86.R_AH = k.scancode;
            emu_->x86.R_AL = k.ascii;
            emu_->x86.R_EFLG &= ~0x40u; // ZF=0
        } else {
            emu_->x86.R_EFLG |= 0x40u;  // ZF=1
        }
    } else if (ah == 0x02 || ah == 0x12) {
        emu_->x86.R_AL = kbd_shift_flags_;
    }
}

void DosMachine::int_1ah() {
    uint8_t ah = emu_->x86.R_AH;
    if (ah == 0x00) {
        emu_->x86.R_CX = static_cast<uint16_t>(bda_ticks_ >> 16);
        emu_->x86.R_DX = static_cast<uint16_t>(bda_ticks_ & 0xFFFF);
        emu_->x86.R_AL = 0;
    } else if (ah == 0x01) {
        bda_ticks_ = (static_cast<uint32_t>(emu_->x86.R_CX) << 16) | emu_->x86.R_DX;
    }
}

void DosMachine::int_33h() {
    uint16_t ax = emu_->x86.R_AX;
    switch (ax) {
        case 0x0000:
        case 0x0021: // Software reset
            // Microsoft Mouse Driver reset ALWAYS centers the cursor to (320, 100) in
            // 640x200 Mode 13h virtual coordinates (CX = 0x0140, CH = 1).
            // MicroProse MOUSE.ASM (F14.EXE / F14S.EXE) immediately reads INT 33h AX=0003h
            // and stores MouseShift = CH! If CX were >= 512 (CH=2), X would be shifted >> 2
            // (half-width screen!); if CX < 256 (CH=0), X would be unshifted (2x too fast).
            emu_->x86.R_AX = 0xFFFF;
            emu_->x86.R_BX = 2;
            mouse_x_ = 160;
            mouse_y_ = 100;
            mouse_mickey_x_ = 0;
            mouse_mickey_y_ = 0;
            mouse_mickey_odd_sent_ = false;
            mouse_min_x_ = 0; mouse_max_x_ = 639;
            mouse_min_y_ = 0; mouse_max_y_ = 199;
            mouse_cb_mask_ = 0;
            mouse_cb_seg_ = 0;
            mouse_cb_off_ = 0;
            pending_mouse_mask_ = 0;
            std::memset(mouse_press_cnt_, 0, sizeof(mouse_press_cnt_));
            std::memset(mouse_rel_cnt_, 0, sizeof(mouse_rel_cnt_));
            break;
        case 0x0001: case 0x0002: break;
        case 0x0003:
            emu_->x86.R_BX = mouse_buttons_;
            emu_->x86.R_CX = static_cast<uint16_t>(std::clamp(mouse_x_ * 2, mouse_min_x_, mouse_max_x_));
            emu_->x86.R_DX = static_cast<uint16_t>(std::clamp(mouse_y_, mouse_min_y_, mouse_max_y_));
            break;
        case 0x0004:
            mouse_x_ = std::clamp(static_cast<int>(emu_->x86.R_CX) / 2, 0, 319);
            mouse_y_ = std::clamp(static_cast<int>(emu_->x86.R_DX), 0, 199);
            break;
        case 0x0005: {
            uint16_t btn = emu_->x86.R_BX;
            emu_->x86.R_AX = mouse_buttons_;
            emu_->x86.R_BX = (btn < 3) ? mouse_press_cnt_[btn] : 0;
            if (btn < 3) mouse_press_cnt_[btn] = 0;
            emu_->x86.R_CX = static_cast<uint16_t>(std::clamp(mouse_x_ * 2, mouse_min_x_, mouse_max_x_));
            emu_->x86.R_DX = static_cast<uint16_t>(std::clamp(mouse_y_, mouse_min_y_, mouse_max_y_));
            break;
        }
        case 0x0006: {
            uint16_t btn = emu_->x86.R_BX;
            emu_->x86.R_AX = mouse_buttons_;
            emu_->x86.R_BX = (btn < 3) ? mouse_rel_cnt_[btn] : 0;
            if (btn < 3) mouse_rel_cnt_[btn] = 0;
            emu_->x86.R_CX = static_cast<uint16_t>(std::clamp(mouse_x_ * 2, mouse_min_x_, mouse_max_x_));
            emu_->x86.R_DX = static_cast<uint16_t>(std::clamp(mouse_y_, mouse_min_y_, mouse_max_y_));
            break;
        }
        case 0x0007: mouse_min_x_ = emu_->x86.R_CX; mouse_max_x_ = emu_->x86.R_DX; break;
        case 0x0008: mouse_min_y_ = emu_->x86.R_CX; mouse_max_y_ = emu_->x86.R_DX; break;
        case 0x000B: {
            // Read Motion Counters (relative mickeys since last call).
            // F.EXE (Main Menu / Career UI) drives its cursor exclusively via AX=000Bh.
            // Note: F.EXE auto-detects 1:1 mickeys vs 8-mickey drivers by checking if bit 0
            // of dx or dy is ever odd; until it sees an odd delta it divides by 8!
            // Guarantee the very first non-zero delta is odd (carrying the +/-1 remainder to
            // the next poll) so F.EXE latches 1:1 pixel mode immediately with zero drift.
            int mx = mouse_mickey_x_;
            int my = mouse_mickey_y_;
            if (!mouse_mickey_odd_sent_ && (mx != 0 || my != 0)) {
                if (((mx & 1) == 0) && ((my & 1) == 0)) {
                    if (mx != 0) {
                        int send_x = (mx > 0) ? (mx - 1) : (mx + 1);
                        mouse_mickey_x_ = mx - send_x;
                        mouse_mickey_y_ = 0;
                        mx = send_x;
                    } else {
                        int send_y = (my > 0) ? (my - 1) : (my + 1);
                        mouse_mickey_y_ = my - send_y;
                        mouse_mickey_x_ = 0;
                        my = send_y;
                    }
                } else {
                    mouse_mickey_x_ = 0;
                    mouse_mickey_y_ = 0;
                }
                mouse_mickey_odd_sent_ = true;
            } else {
                mouse_mickey_x_ = 0;
                mouse_mickey_y_ = 0;
            }
            emu_->x86.R_CX = static_cast<uint16_t>(static_cast<int16_t>(mx));
            emu_->x86.R_DX = static_cast<uint16_t>(static_cast<int16_t>(my));
            break;
        }
        case 0x000C:
            mouse_cb_mask_ = emu_->x86.R_CX;
            mouse_cb_seg_ = emu_->x86.R_ES;
            mouse_cb_off_ = emu_->x86.R_DX;
            break;
        case 0x000F: case 0x0013: case 0x001A: break;
        case 0x0014: {
            uint16_t om = mouse_cb_mask_, os = mouse_cb_seg_, oo = mouse_cb_off_;
            mouse_cb_mask_ = emu_->x86.R_CX;
            mouse_cb_seg_ = emu_->x86.R_ES;
            mouse_cb_off_ = emu_->x86.R_DX;
            emu_->x86.R_CX = om;
            x86emu_set_seg_register(emu_, emu_->x86.R_ES_SEL, os);
            emu_->x86.R_DX = oo;
            break;
        }
        case 0x0024: // Get software version, mouse type, and IRQ number (queried by F.EXE)
            emu_->x86.R_BX = 0x0814; // Microsoft Mouse Driver v8.20
            emu_->x86.R_CX = 0x0400; // CH=4 (PS/2 mouse), CL=0 (PS/2 IRQ)
            break;
        default: break;
    }
}

void DosMachine::populate_find_dta(const fs::directory_entry& entry) {
    uint32_t dta = (static_cast<uint32_t>(dta_seg_) << 4) + dta_off_;
    std::memset(&mem_[dta], 0, 43);
    uint8_t attr = entry.is_directory() ? 0x10 : 0x20;
    uint32_t fsize = entry.is_regular_file() ? static_cast<uint32_t>(entry.file_size()) : 0;
    mem_write8(dta + 21, attr);
    mem_write16(dta + 26, static_cast<uint16_t>(fsize & 0xFFFF));
    mem_write16(dta + 28, static_cast<uint16_t>(fsize >> 16));
    std::string fname = to_upper(entry.path().filename().string());
    if (fname.size() > 12) fname.resize(12);
    std::memcpy(&mem_[dta + 30], fname.c_str(), fname.size() + 1);
}

void DosMachine::int_21h() {
    uint8_t ah = emu_->x86.R_AH;
    set_cf(false);

    switch (ah) {
        case 0x00: finish_current_process(0); break;
        case 0x02: vga_.tty_putc(mem_.data(), static_cast<char>(emu_->x86.R_DL)); break;
        case 0x06:
            if (emu_->x86.R_DL == 0xFF) {
                if (!key_queue_.empty()) {
                    emu_->x86.R_AL = key_queue_.front().ascii;
                    key_queue_.pop_front();
                    emu_->x86.R_EFLG &= ~0x40u;
                } else {
                    emu_->x86.R_AL = 0;
                    emu_->x86.R_EFLG |= 0x40u;
                }
            } else {
                vga_.tty_putc(mem_.data(), static_cast<char>(emu_->x86.R_DL));
            }
            break;
        case 0x01: case 0x07: case 0x08:
            if (!key_queue_.empty()) {
                emu_->x86.R_AL = key_queue_.front().ascii;
                key_queue_.pop_front();
            } else {
                emu_->x86.R_AL = 0;
            }
            break;
        case 0x09: {
            std::string s = read_dos_string(emu_->x86.R_DS, emu_->x86.R_DX, '$', 512);
            for (char c : s) vga_.tty_putc(mem_.data(), c);
            log("DOS PRINT: " + s);
            break;
        }
        case 0x0B: emu_->x86.R_AL = key_queue_.empty() ? 0x00 : 0xFF; break;
        case 0x0E: emu_->x86.R_AL = 4; break;
        case 0x19: emu_->x86.R_AL = 2; break; // C:
        case 0x1A: dta_seg_ = emu_->x86.R_DS; dta_off_ = emu_->x86.R_DX; break;
        case 0x25: {
            uint8_t vec = emu_->x86.R_AL;
            mem_write16(vec * 4 + 0, emu_->x86.R_DX);
            mem_write16(vec * 4 + 2, emu_->x86.R_DS);
            std::ostringstream oss;
            oss << "SET IVT 0x" << std::hex << std::uppercase << static_cast<int>(vec)
                << " -> " << emu_->x86.R_DS << ":" << emu_->x86.R_DX;
            log(oss.str());
            break;
        }
        case 0x2A: {
            std::time_t t = std::time(nullptr);
            std::tm tm_now{};
            localtime_s(&tm_now, &t);
            emu_->x86.R_CX = static_cast<uint16_t>(tm_now.tm_year + 1900);
            emu_->x86.R_DH = static_cast<uint8_t>(tm_now.tm_mon + 1);
            emu_->x86.R_DL = static_cast<uint8_t>(tm_now.tm_mday);
            emu_->x86.R_AL = static_cast<uint8_t>(tm_now.tm_wday);
            break;
        }
        case 0x2C: {
            std::time_t t = std::time(nullptr);
            std::tm tm_now{};
            localtime_s(&tm_now, &t);
            emu_->x86.R_CH = static_cast<uint8_t>(tm_now.tm_hour);
            emu_->x86.R_CL = static_cast<uint8_t>(tm_now.tm_min);
            emu_->x86.R_DH = static_cast<uint8_t>(tm_now.tm_sec);
            emu_->x86.R_DL = 0;
            break;
        }
        case 0x2D: // Set System Time (used by F.EXE on exit)
            emu_->x86.R_AL = 0x00;
            break;
        case 0x2F:
            x86emu_set_seg_register(emu_, emu_->x86.R_ES_SEL, dta_seg_);
            emu_->x86.R_BX = dta_off_;
            break;
        case 0x30:
            emu_->x86.R_AL = 5; emu_->x86.R_AH = 0; emu_->x86.R_BX = 0; emu_->x86.R_CX = 0;
            break;
        case 0x33: // Ctrl-Break check
            if (emu_->x86.R_AL == 0x00) emu_->x86.R_DL = 0;
            break;
        case 0x35: {
            uint8_t vec = emu_->x86.R_AL;
            emu_->x86.R_BX = mem_read16(vec * 4 + 0);
            x86emu_set_seg_register(emu_, emu_->x86.R_ES_SEL, mem_read16(vec * 4 + 2));
            break;
        }
        case 0x36: // Get free disk space
            emu_->x86.R_AX = 64;
            emu_->x86.R_BX = 32768;
            emu_->x86.R_CX = 512;
            emu_->x86.R_DX = 65535;
            break;
        case 0x3B: { // CHDIR
            std::string dir = read_dos_string(emu_->x86.R_DS, emu_->x86.R_DX, '\0');
            if (dir == "\\" || dir == "/" || dir == "C:\\") {
                current_subdir_.clear();
            } else {
                std::string resolved = resolve_dos_path(dir);
                if (fs::exists(resolved) && fs::is_directory(resolved)) {
                    std::error_code ec;
                    fs::path rel = fs::relative(resolved, game_dir_, ec);
                    current_subdir_ = (!ec && rel != ".") ? rel.string() : "";
                } else {
                    set_cf(true);
                    emu_->x86.R_AX = 3;
                }
            }
            break;
        }
        case 0x39: { // MKDIR (used by F14S.EXE Scenario Builder)
            std::string dos_name = read_dos_string(emu_->x86.R_DS, emu_->x86.R_DX, '\0');
            std::error_code ec;
            fs::path p = resolve_dos_path(dos_name);
            if (!fs::exists(p, ec) && !fs::create_directories(p, ec)) {
                set_cf(true); emu_->x86.R_AX = 5;
            }
            break;
        }
        case 0x3A: { // RMDIR
            std::string dos_name = read_dos_string(emu_->x86.R_DS, emu_->x86.R_DX, '\0');
            std::error_code ec;
            if (!fs::remove(resolve_dos_path(dos_name), ec)) {
                set_cf(true); emu_->x86.R_AX = 5;
            }
            break;
        }
        case 0x3C:
        case 0x3D: {
            std::string dos_name = read_dos_string(emu_->x86.R_DS, emu_->x86.R_DX, '\0');
            std::string upper = to_upper(dos_name);
            if (upper == "CON" || upper == "EMMXXXX0") {
                auto of = std::make_unique<OpenFile>();
                of->is_console = true;
                of->host_path = upper;
                of->owner_psp = psp_seg_;
                uint16_t h = alloc_file_handle();
                files_[h] = std::move(of);
                emu_->x86.R_AX = h;
                break;
            }
            std::string host_path = resolve_dos_path(dos_name);
            auto of = std::make_unique<OpenFile>();
            of->host_path = host_path;
            of->owner_psp = psp_seg_;
            const char* fmode = "rb";
            if (ah == 0x3C) {
                fmode = "w+b";
            } else {
                uint8_t acc = emu_->x86.R_AL & 0x03;
                fmode = (acc == 0) ? "rb" : "r+b";
            }
            of->fp = std::fopen(host_path.c_str(), fmode);
            if (!of->fp && ah == 0x3D && (emu_->x86.R_AL & 0x03) == 1) {
                of->fp = std::fopen(host_path.c_str(), "wb");
            }
            if (!of->fp) {
                set_cf(true);
                emu_->x86.R_AX = 2;
                log("DOS OPEN FAIL: " + dos_name);
            } else {
                uint16_t h = alloc_file_handle();
                files_[h] = std::move(of);
                emu_->x86.R_AX = h;
                log("DOS OPEN OK: " + dos_name + " -> h=" + std::to_string(h));

                // Auto-detect loaded .WLD theater!
                if (upper.size() > 4 && upper.substr(upper.size() - 4) == ".WLD") {
                    detected_world_file_ = host_path;
                    in_3d_flight_session_ = true;
                    log("AUTO-DETECTED WORLD: " + dos_name);
                }
                // Fast-boot completion once F.EXE main menu art loads
                if (fast_boot_active_ &&
                    (upper.find("MAIN") != std::string::npos || upper.find("CARRIER") != std::string::npos ||
                     upper.find("PITS") != std::string::npos || upper.find("ROSTER") != std::string::npos)) {
                    fast_boot_stage_ = 3;
                    fast_boot_key_at_ = total_insns_ + 2000000ULL;
                }
                disk_turbo_sec_ = std::max(disk_turbo_sec_, 0.45);
            }
            break;
        }
        case 0x3E: {
            uint16_t h = emu_->x86.R_BX;
            if (h >= 5) {
                auto it = files_.find(h);
                if (it != files_.end()) files_.erase(it);
                else { set_cf(true); emu_->x86.R_AX = 6; }
            }
            break;
        }
        case 0x3F: {
            uint16_t h = emu_->x86.R_BX;
            uint16_t count = emu_->x86.R_CX;
            uint32_t dst = (static_cast<uint32_t>(emu_->x86.R_DS) << 4) + emu_->x86.R_DX;
            auto it = files_.find(h);
            if (it == files_.end()) {
                set_cf(true); emu_->x86.R_AX = 6; break;
            }
            if (it->second->is_console || !it->second->fp) {
                emu_->x86.R_AX = 0; break;
            }
            std::vector<uint8_t> buf(count);
            size_t got = std::fread(buf.data(), 1, count, it->second->fp);
            for (size_t i = 0; i < got; ++i) mem_write8(dst + static_cast<uint32_t>(i), buf[i]);
            if (got >= 512) disk_turbo_sec_ = std::max(disk_turbo_sec_, 0.35);
            emu_->x86.R_AX = static_cast<uint16_t>(got);
            break;
        }
        case 0x40: {
            uint16_t h = emu_->x86.R_BX;
            uint16_t count = emu_->x86.R_CX;
            uint32_t src = (static_cast<uint32_t>(emu_->x86.R_DS) << 4) + emu_->x86.R_DX;
            if (h == 1 || h == 2) {
                std::string s;
                for (uint16_t i = 0; i < count; ++i) {
                    char c = static_cast<char>(mem_read8(src + i));
                    vga_.tty_putc(mem_.data(), c);
                    s.push_back(c);
                }
                log("STDOUT: " + s);
                emu_->x86.R_AX = count;
                break;
            }
            auto it = files_.find(h);
            if (it == files_.end() || !it->second->fp) {
                set_cf(true); emu_->x86.R_AX = 6; break;
            }
            std::vector<uint8_t> buf(count);
            for (uint16_t i = 0; i < count; ++i) buf[i] = mem_read8(src + i);
            size_t written = std::fwrite(buf.data(), 1, count, it->second->fp);
            std::fflush(it->second->fp);
            emu_->x86.R_AX = static_cast<uint16_t>(written);
            break;
        }
        case 0x41: {
            std::string dos_name = read_dos_string(emu_->x86.R_DS, emu_->x86.R_DX, '\0');
            std::error_code ec;
            if (!fs::remove(resolve_dos_path(dos_name), ec)) {
                set_cf(true); emu_->x86.R_AX = 2;
            }
            break;
        }
        case 0x42: {
            uint16_t h = emu_->x86.R_BX;
            auto it = files_.find(h);
            if (it == files_.end() || !it->second->fp) {
                set_cf(true); emu_->x86.R_AX = 6; break;
            }
            int32_t offset = static_cast<int32_t>((static_cast<uint32_t>(emu_->x86.R_CX) << 16) | emu_->x86.R_DX);
            int whence = (emu_->x86.R_AL == 1) ? SEEK_CUR : (emu_->x86.R_AL == 2 ? SEEK_END : SEEK_SET);
            if (std::fseek(it->second->fp, offset, whence) != 0) {
                set_cf(true); emu_->x86.R_AX = 1; break;
            }
            long pos = std::ftell(it->second->fp);
            if (pos < 0) pos = 0;
            uint32_t u = static_cast<uint32_t>(pos);
            emu_->x86.R_DX = static_cast<uint16_t>(u >> 16);
            emu_->x86.R_AX = static_cast<uint16_t>(u & 0xFFFF);
            break;
        }
        case 0x43: {
            std::string host = resolve_dos_path(read_dos_string(emu_->x86.R_DS, emu_->x86.R_DX));
            if (fs::exists(host)) emu_->x86.R_CX = fs::is_directory(host) ? 0x10 : 0x20;
            else { set_cf(true); emu_->x86.R_AX = 2; }
            break;
        }
        case 0x44: {
            uint8_t al = emu_->x86.R_AL;
            uint16_t h = emu_->x86.R_BX;
            if (al == 0x00) {
                if (h <= 2) emu_->x86.R_DX = 0x0083;
                else {
                    auto it = files_.find(h);
                    emu_->x86.R_DX = (it != files_.end() && it->second->is_console) ? 0x0080 : 0x0002;
                }
            } else if (al == 0x07) {
                emu_->x86.R_AL = 0xFF;
            }
            break;
        }
        case 0x45: {
            uint16_t h = emu_->x86.R_BX;
            if (h <= 4) {
                auto dup = std::make_unique<OpenFile>();
                dup->is_console = true; dup->owner_psp = psp_seg_;
                uint16_t nh = alloc_file_handle();
                files_[nh] = std::move(dup);
                emu_->x86.R_AX = nh;
                break;
            }
            auto it = files_.find(h);
            if (it == files_.end()) { set_cf(true); emu_->x86.R_AX = 6; }
            else {
                auto dup = std::make_unique<OpenFile>();
                dup->host_path = it->second->host_path;
                dup->is_console = it->second->is_console;
                dup->owner_psp = psp_seg_;
                if (!dup->is_console && it->second->fp) {
                    long cur = std::ftell(it->second->fp);
                    dup->fp = std::fopen(dup->host_path.c_str(), "r+b");
                    if (!dup->fp) dup->fp = std::fopen(dup->host_path.c_str(), "rb");
                    if (dup->fp && cur >= 0) std::fseek(dup->fp, cur, SEEK_SET);
                }
                uint16_t nh = alloc_file_handle();
                files_[nh] = std::move(dup);
                emu_->x86.R_AX = nh;
            }
            break;
        }
        case 0x46: {
            uint16_t h1 = emu_->x86.R_BX, h2 = emu_->x86.R_CX;
            if (h1 <= 4 || h2 <= 4) break;
            auto it = files_.find(h1);
            if (it == files_.end()) { set_cf(true); emu_->x86.R_AX = 6; }
            else {
                auto dup = std::make_unique<OpenFile>();
                dup->host_path = it->second->host_path;
                dup->is_console = it->second->is_console;
                dup->owner_psp = psp_seg_;
                if (!dup->is_console && it->second->fp) {
                    long cur = std::ftell(it->second->fp);
                    dup->fp = std::fopen(dup->host_path.c_str(), "r+b");
                    if (!dup->fp) dup->fp = std::fopen(dup->host_path.c_str(), "rb");
                    if (dup->fp && cur >= 0) std::fseek(dup->fp, cur, SEEK_SET);
                }
                files_[h2] = std::move(dup);
            }
            break;
        }
        case 0x47: {
            uint32_t dst = (static_cast<uint32_t>(emu_->x86.R_DS) << 4) + emu_->x86.R_SI;
            std::string sub = current_subdir_;
            std::replace(sub.begin(), sub.end(), '/', '\\');
            for (size_t i = 0; i < sub.size() && i < 63; ++i) mem_write8(dst + static_cast<uint32_t>(i), static_cast<uint8_t>(sub[i]));
            mem_write8(dst + static_cast<uint32_t>(std::min<size_t>(sub.size(), 63)), 0);
            break;
        }
        case 0x48: {
            uint16_t max_av = 0;
            uint16_t seg = dos_alloc(emu_->x86.R_BX, max_av);
            if (seg != 0) emu_->x86.R_AX = seg;
            else { set_cf(true); emu_->x86.R_AX = 8; emu_->x86.R_BX = max_av; }
            break;
        }
        case 0x49:
            if (!dos_free(emu_->x86.R_ES)) { set_cf(true); emu_->x86.R_AX = 9; }
            break;
        case 0x4A: {
            uint16_t max_av = 0;
            if (!dos_resize(emu_->x86.R_ES, emu_->x86.R_BX, max_av)) {
                set_cf(true); emu_->x86.R_AX = 8; emu_->x86.R_BX = max_av;
            }
            break;
        }
        case 0x4B: {
            uint8_t al = emu_->x86.R_AL;
            std::string dos_name = read_dos_string(emu_->x86.R_DS, emu_->x86.R_DX, '\0');
            if (al == 0x00) {
                exec_child_process(dos_name, emu_->x86.R_ES, emu_->x86.R_BX);
            } else if (al == 0x03) {
                std::string host_path = resolve_dos_path(dos_name);
                uint32_t pb = (static_cast<uint32_t>(emu_->x86.R_ES) << 4) + emu_->x86.R_BX;
                uint16_t ovl_load_seg = mem_read16(pb + 0);
                uint16_t ovl_reloc_fac = mem_read16(pb + 2);
                std::ifstream f(host_path, std::ios::binary);
                if (!f) {
                    set_cf(true); emu_->x86.R_AX = 2;
                    log("DOS 4B03 FAIL open: " + dos_name);
                    break;
                }
                std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                uint32_t dst_base = static_cast<uint32_t>(ovl_load_seg) << 4;
                if (data.size() >= 28 && data[0] == 'M' && data[1] == 'Z') {
                    auto rd16 = [&](size_t off) -> uint16_t { return static_cast<uint16_t>(data[off] | (data[off + 1] << 8)); };
                    uint16_t last_p = rd16(2), pages = rd16(4), reloc_cnt = rd16(6), hdr_paras = rd16(8), reloc_off = rd16(24);
                    uint32_t hdr_size = static_cast<uint32_t>(hdr_paras) * 16u;
                    uint32_t mz_size = static_cast<uint32_t>(pages - 1) * 512u + (last_p ? last_p : 512u);
                    if (mz_size > data.size()) mz_size = static_cast<uint32_t>(data.size());
                    uint32_t img_size = mz_size - hdr_size;
                    for (uint32_t i = 0; i < img_size; ++i) mem_write8(dst_base + i, data[hdr_size + i]);
                    for (uint16_t i = 0; i < reloc_cnt; ++i) {
                        uint16_t r_off = rd16(reloc_off + i * 4);
                        uint16_t r_seg = rd16(reloc_off + i * 4 + 2);
                        uint32_t target = (static_cast<uint32_t>(ovl_load_seg + r_seg) << 4) + r_off;
                        mem_write16(target, static_cast<uint16_t>(mem_read16(target) + ovl_reloc_fac));
                    }
                    log("DOS 4B03 MZ OVERLAY: " + dos_name + " (" + std::to_string(img_size) + " B)");
                } else {
                    for (size_t i = 0; i < data.size(); ++i) mem_write8(dst_base + static_cast<uint32_t>(i), data[i]);
                    log("DOS 4B03 RAW OVERLAY: " + dos_name);
                }
            } else {
                set_cf(true); emu_->x86.R_AX = 1;
            }
            break;
        }
        case 0x4C:
            finish_current_process(emu_->x86.R_AL);
            break;
        case 0x4D: // Get return code of child process
            emu_->x86.R_AX = last_child_exit_code_; // AH=0 (normal termination), AL=exit code
            break;
        case 0x4E: {
            std::string pat = read_dos_string(emu_->x86.R_DS, emu_->x86.R_DX);
            std::string resolved = resolve_dos_path(pat);
            fs::path p(resolved);
            fs::path dir = p.has_parent_path() ? p.parent_path() : fs::path(game_dir_);
            std::string mask = to_upper(p.filename().string());
            find_results_.clear();
            find_index_ = 0;
            if (fs::exists(dir) && fs::is_directory(dir)) {
                for (const auto& entry : fs::directory_iterator(dir)) {
                    if (wildcard_match(mask, to_upper(entry.path().filename().string()))) {
                        find_results_.push_back(entry);
                    }
                }
            }
            if (!find_results_.empty()) populate_find_dta(find_results_[find_index_++]);
            else { set_cf(true); emu_->x86.R_AX = 18; }
            break;
        }
        case 0x4F:
            if (find_index_ < find_results_.size()) populate_find_dta(find_results_[find_index_++]);
            else { set_cf(true); emu_->x86.R_AX = 18; }
            break;
        case 0x50: // Set PSP
            psp_seg_ = emu_->x86.R_BX;
            break;
        case 0x51:
        case 0x62:
            emu_->x86.R_BX = psp_seg_;
            break;
        case 0x52: { // Get List of Lists (SYSVARS): ES:[BX-2] = segment of first MCB!
            uint16_t first_mcb = mcbs_.empty() ? 0x00FF : static_cast<uint16_t>(mcbs_[0].seg - 1);
            write_mcb_headers();
            // Place SYSVARS at 0060:0002, with first_mcb_seg at 0060:0000 (ES:[BX-2])
            std::memset(&mem_[0x0600], 0, 64);
            mem_write16(0x0600, first_mcb);
            x86emu_set_seg_register(emu_, emu_->x86.R_ES_SEL, 0x0060);
            emu_->x86.R_BX = 0x0002;
            break;
        }
        case 0x57: // Get/Set file date & time
            if (emu_->x86.R_AL == 0x00) { emu_->x86.R_CX = 0x6000; emu_->x86.R_DX = 0x1D2C; }
            break;
        case 0x58:
            if (emu_->x86.R_AL == 0x00) emu_->x86.R_AX = 0;
            else if (emu_->x86.R_AL == 0x02) emu_->x86.R_AL = 0;
            break;
        default: {
            std::ostringstream oss;
            oss << "Unhandled INT 21h AH=0x" << std::hex << std::uppercase << static_cast<int>(ah)
                << " AX=" << emu_->x86.R_AX;
            log(oss.str());
            break;
        }
    }
}

void DosMachine::int_67h() {
    uint8_t ah = emu_->x86.R_AH;
    switch (ah) {
        case 0x40: emu_->x86.R_AH = 0x00; break;
        case 0x41: emu_->x86.R_BX = EMS_FRAME_SEG; emu_->x86.R_AH = 0x00; break;
        case 0x42:
            emu_->x86.R_BX = static_cast<uint16_t>(EMS_TOTAL_PAGES - ems_pages_used_);
            emu_->x86.R_DX = static_cast<uint16_t>(EMS_TOTAL_PAGES);
            emu_->x86.R_AH = 0x00;
            break;
        case 0x43: {
            uint16_t count = emu_->x86.R_BX;
            if (count == 0 || ems_pages_used_ + count > EMS_TOTAL_PAGES) {
                emu_->x86.R_AH = (count == 0) ? 0x89 : 0x87; break;
            }
            int free_h = -1;
            for (int i = 1; i < static_cast<int>(ems_handles_.size()); ++i) {
                if (!ems_handles_[i].in_use) { free_h = i; break; }
            }
            if (free_h < 0) { emu_->x86.R_AH = 0x85; break; }
            ems_handles_[free_h].in_use = true;
            ems_handles_[free_h].owner_psp = psp_seg_;
            ems_handles_[free_h].data.assign(static_cast<size_t>(count) * EMS_PAGE_SIZE, 0);
            ems_pages_used_ += count;
            emu_->x86.R_DX = static_cast<uint16_t>(free_h);
            emu_->x86.R_AH = 0x00;
            break;
        }
        case 0x44: {
            uint8_t phys = emu_->x86.R_AL;
            uint16_t logical = emu_->x86.R_BX;
            uint16_t handle = emu_->x86.R_DX;
            if (phys >= 4) { emu_->x86.R_AH = 0x8B; break; }
            if (logical == 0xFFFF) { ems_slots_[phys] = {-1, -1}; emu_->x86.R_AH = 0x00; break; }
            if (handle >= ems_handles_.size() || !ems_handles_[handle].in_use) { emu_->x86.R_AH = 0x83; break; }
            if (static_cast<size_t>(logical) * EMS_PAGE_SIZE >= ems_handles_[handle].data.size()) { emu_->x86.R_AH = 0x8A; break; }
            ems_slots_[phys] = {static_cast<int>(handle), static_cast<int>(logical)};
            emu_->x86.R_AH = 0x00;
            break;
        }
        case 0x45: {
            uint16_t handle = emu_->x86.R_DX;
            if (handle == 0 || handle >= ems_handles_.size() || !ems_handles_[handle].in_use) { emu_->x86.R_AH = 0x83; break; }
            ems_pages_used_ -= static_cast<int>(ems_handles_[handle].data.size() / EMS_PAGE_SIZE);
            ems_handles_[handle] = {};
            for (auto& s : ems_slots_) if (s.handle == handle) s = {-1, -1};
            emu_->x86.R_AH = 0x00;
            break;
        }
        case 0x46: emu_->x86.R_AL = 0x40; emu_->x86.R_AH = 0x00; break;
        case 0x47: ems_saved_maps_[emu_->x86.R_DX] = ems_slots_; emu_->x86.R_AH = 0x00; break;
        case 0x48: {
            auto it = ems_saved_maps_.find(emu_->x86.R_DX);
            if (it != ems_saved_maps_.end()) { ems_slots_ = it->second; ems_saved_maps_.erase(it); }
            emu_->x86.R_AH = 0x00;
            break;
        }
        case 0x4B: {
            uint16_t cnt = 0;
            for (const auto& h : ems_handles_) if (h.in_use) cnt++;
            emu_->x86.R_BX = cnt; emu_->x86.R_AH = 0x00;
            break;
        }
        case 0x4C: {
            uint16_t handle = emu_->x86.R_DX;
            if (handle < ems_handles_.size() && ems_handles_[handle].in_use) {
                emu_->x86.R_BX = static_cast<uint16_t>(ems_handles_[handle].data.size() / EMS_PAGE_SIZE);
                emu_->x86.R_AH = 0x00;
            } else emu_->x86.R_AH = 0x83;
            break;
        }
        case 0x50: {
            uint8_t sub = emu_->x86.R_AL;
            uint16_t count = emu_->x86.R_CX;
            uint16_t handle = emu_->x86.R_DX;
            uint32_t ptr = (static_cast<uint32_t>(emu_->x86.R_DS) << 4) + emu_->x86.R_SI;
            for (uint16_t i = 0; i < count; ++i) {
                uint16_t logical = mem_read16(ptr + i * 4);
                uint16_t phys_or_seg = mem_read16(ptr + i * 4 + 2);
                int phys = (sub == 0) ? phys_or_seg : ((phys_or_seg - EMS_FRAME_SEG) / 0x0400);
                if (phys >= 0 && phys < 4) {
                    if (logical == 0xFFFF) ems_slots_[phys] = {-1, -1};
                    else ems_slots_[phys] = {static_cast<int>(handle), static_cast<int>(logical)};
                }
            }
            emu_->x86.R_AH = 0x00;
            break;
        }
        case 0x51: { // Reallocate Pages
            uint16_t handle = emu_->x86.R_DX;
            uint16_t new_cnt = emu_->x86.R_BX;
            if (handle == 0 || handle >= ems_handles_.size() || !ems_handles_[handle].in_use) {
                emu_->x86.R_AH = 0x83; break;
            }
            int old_cnt = static_cast<int>(ems_handles_[handle].data.size() / EMS_PAGE_SIZE);
            int delta = static_cast<int>(new_cnt) - old_cnt;
            if (ems_pages_used_ + delta > EMS_TOTAL_PAGES) {
                emu_->x86.R_BX = static_cast<uint16_t>(old_cnt);
                emu_->x86.R_AH = 0x87; break;
            }
            ems_handles_[handle].data.resize(static_cast<size_t>(new_cnt) * EMS_PAGE_SIZE, 0);
            ems_pages_used_ += delta;
            emu_->x86.R_BX = new_cnt;
            emu_->x86.R_AH = 0x00;
            break;
        }
        case 0x53: { // Get/Set Handle Name
            uint16_t handle = emu_->x86.R_DX;
            if (handle >= ems_handles_.size() || !ems_handles_[handle].in_use) {
                emu_->x86.R_AH = 0x83; break;
            }
            if (emu_->x86.R_AL == 0x00) {
                uint32_t dst = (static_cast<uint32_t>(emu_->x86.R_ES) << 4) + emu_->x86.R_DI;
                for (int i = 0; i < 8; ++i) {
                    uint8_t c = (i < static_cast<int>(ems_handles_[handle].name.size()))
                                    ? static_cast<uint8_t>(ems_handles_[handle].name[i]) : 0;
                    mem_write8(dst + i, c);
                }
                emu_->x86.R_AH = 0x00;
            } else if (emu_->x86.R_AL == 0x01) {
                uint32_t src = (static_cast<uint32_t>(emu_->x86.R_DS) << 4) + emu_->x86.R_SI;
                std::string nm;
                for (int i = 0; i < 8; ++i) {
                    char c = static_cast<char>(mem_read8(src + i));
                    if (c == '\0') break;
                    nm.push_back(c);
                }
                ems_handles_[handle].name = nm;
                emu_->x86.R_AH = 0x00;
            } else {
                emu_->x86.R_AH = 0x8F;
            }
            break;
        }
        case 0x57: { // EMS 4.0 Move (AL=0) / Exchange (AL=1) Memory Region (used by F.EXE UI popups!)
            uint8_t sub = emu_->x86.R_AL;
            if (sub != 0x00 && sub != 0x01) { emu_->x86.R_AH = 0x8F; break; }
            uint32_t desc = (static_cast<uint32_t>(emu_->x86.R_DS) << 4) + emu_->x86.R_SI;
            uint32_t len = static_cast<uint32_t>(mem_read16(desc + 0)) |
                           (static_cast<uint32_t>(mem_read16(desc + 2)) << 16);
            uint8_t  src_type = mem_read8(desc + 4);
            uint16_t src_hdl  = mem_read16(desc + 5);
            uint16_t src_off  = mem_read16(desc + 7);
            uint16_t src_pg   = mem_read16(desc + 9);
            uint8_t  dst_type = mem_read8(desc + 11);
            uint16_t dst_hdl  = mem_read16(desc + 12);
            uint16_t dst_off  = mem_read16(desc + 14);
            uint16_t dst_pg   = mem_read16(desc + 16);

            if (len == 0) { emu_->x86.R_AH = 0x00; break; }
            if (src_type > 1 || dst_type > 1) { emu_->x86.R_AH = 0x98; break; }
            if (src_type == 1 && (src_hdl >= ems_handles_.size() || !ems_handles_[src_hdl].in_use)) {
                emu_->x86.R_AH = 0x83; break;
            }
            if (dst_type == 1 && (dst_hdl >= ems_handles_.size() || !ems_handles_[dst_hdl].in_use)) {
                emu_->x86.R_AH = 0x83; break;
            }

            size_t src_ems_base = static_cast<size_t>(src_pg) * EMS_PAGE_SIZE + src_off;
            size_t dst_ems_base = static_cast<size_t>(dst_pg) * EMS_PAGE_SIZE + dst_off;
            if (src_type == 1 && src_ems_base + len > ems_handles_[src_hdl].data.size()) {
                emu_->x86.R_AH = 0x93; break;
            }
            if (dst_type == 1 && dst_ems_base + len > ems_handles_[dst_hdl].data.size()) {
                emu_->x86.R_AH = 0x93; break;
            }

            uint32_t src_conv = (static_cast<uint32_t>(src_pg) << 4) + src_off;
            uint32_t dst_conv = (static_cast<uint32_t>(dst_pg) << 4) + dst_off;

            std::vector<uint8_t> sbuf(len);
            if (src_type == 1) {
                std::memcpy(sbuf.data(), ems_handles_[src_hdl].data.data() + src_ems_base, len);
            } else {
                for (uint32_t i = 0; i < len; ++i) sbuf[i] = mem_read8((src_conv + i) & 0xFFFFF);
            }

            if (sub == 0x01) { // Exchange
                std::vector<uint8_t> dbuf(len);
                if (dst_type == 1) {
                    std::memcpy(dbuf.data(), ems_handles_[dst_hdl].data.data() + dst_ems_base, len);
                } else {
                    for (uint32_t i = 0; i < len; ++i) dbuf[i] = mem_read8((dst_conv + i) & 0xFFFFF);
                }
                if (src_type == 1) {
                    std::memcpy(ems_handles_[src_hdl].data.data() + src_ems_base, dbuf.data(), len);
                } else {
                    for (uint32_t i = 0; i < len; ++i) mem_write8((src_conv + i) & 0xFFFFF, dbuf[i]);
                }
            }

            if (dst_type == 1) {
                std::memcpy(ems_handles_[dst_hdl].data.data() + dst_ems_base, sbuf.data(), len);
            } else {
                for (uint32_t i = 0; i < len; ++i) mem_write8((dst_conv + i) & 0xFFFFF, sbuf[i]);
            }
            emu_->x86.R_AH = 0x00;
            break;
        }
        case 0x58: {
            if (emu_->x86.R_AL == 0x00) {
                uint32_t dst = (static_cast<uint32_t>(emu_->x86.R_ES) << 4) + emu_->x86.R_DI;
                for (uint16_t i = 0; i < 4; ++i) {
                    mem_write16(dst + i * 4 + 0, static_cast<uint16_t>(EMS_FRAME_SEG + i * 0x0400));
                    mem_write16(dst + i * 4 + 2, i);
                }
            }
            emu_->x86.R_CX = 4;
            emu_->x86.R_AH = 0x00;
            break;
        }
        default: emu_->x86.R_AH = 0x84; break;
    }
}

void DosMachine::int_7ah() {
    uint16_t fn = emu_->x86.R_BX;
    switch (fn) {
        case 0x0000: emu_->x86.R_AL = 0; if (emu_->x86.R_DX == 0) emu_->x86.R_DX = 0x4567; break;
        case 0x0001: emu_->x86.R_AL = 0; break;
        case 0x0002: emu_->x86.R_AL = 0; emu_->x86.R_CX = 1; break;
        case 0x0003: {
            uint32_t ecb = (static_cast<uint32_t>(emu_->x86.R_ES) << 4) + emu_->x86.R_SI;
            uint16_t frag_cnt = mem_read16(ecb + 34);
            std::vector<uint8_t> payload;
            for (uint16_t i = 0; i < frag_cnt && i < 8; ++i) {
                uint16_t f_off = mem_read16(ecb + 36 + i * 6 + 0);
                uint16_t f_seg = mem_read16(ecb + 36 + i * 6 + 2);
                uint16_t f_len = mem_read16(ecb + 36 + i * 6 + 4);
                uint32_t f_addr = (static_cast<uint32_t>(f_seg) << 4) + f_off;
                for (uint16_t b = 0; b < f_len; ++b) payload.push_back(mem_read8(f_addr + b));
            }
            if (!payload.empty()) net_.send_payload(payload.data(), payload.size());
            mem_write8(ecb + 8, 0x00); mem_write8(ecb + 9, 0x00);
            emu_->x86.R_AL = 0;
            break;
        }
        case 0x0004: {
            uint32_t ecb = (static_cast<uint32_t>(emu_->x86.R_ES) << 4) + emu_->x86.R_SI;
            uint16_t sock = mem_read16(ecb + 10);
            mem_write8(ecb + 8, 0xFE);
            ipx_listen_ecbs_.push_back({emu_->x86.R_ES, emu_->x86.R_SI, sock});
            emu_->x86.R_AL = 0;
            break;
        }
        case 0x0009: {
            uint32_t dst = (static_cast<uint32_t>(emu_->x86.R_ES) << 4) + emu_->x86.R_SI;
            mem_write8(dst + 0, 0); mem_write8(dst + 1, 0); mem_write8(dst + 2, 0); mem_write8(dst + 3, 1);
            const auto& node = net_.local_node_id();
            for (int i = 0; i < 6; ++i) mem_write8(dst + 4 + i, node[i]);
            emu_->x86.R_AL = 0;
            break;
        }
        case 0x000A: poll_ipx_udp(); emu_->x86.R_AL = 0; break;
        default: emu_->x86.R_AL = 0; break;
    }
}

void DosMachine::poll_ipx_udp() {
    net_.poll();
    NetPacket pkt;
    while (!ipx_listen_ecbs_.empty() && net_.pop_packet(pkt)) {
        auto lecb = ipx_listen_ecbs_.front();
        ipx_listen_ecbs_.erase(ipx_listen_ecbs_.begin());
        uint32_t ecb = (static_cast<uint32_t>(lecb.es) << 4) + lecb.si;
        uint16_t frag_cnt = mem_read16(ecb + 34);
        size_t pos = 0;
        for (uint16_t i = 0; i < frag_cnt && i < 8 && pos < pkt.data.size(); ++i) {
            uint16_t f_off = mem_read16(ecb + 36 + i * 6 + 0);
            uint16_t f_seg = mem_read16(ecb + 36 + i * 6 + 2);
            uint16_t f_len = mem_read16(ecb + 36 + i * 6 + 4);
            uint32_t f_addr = (static_cast<uint32_t>(f_seg) << 4) + f_off;
            for (uint16_t b = 0; b < f_len && pos < pkt.data.size(); ++b) mem_write8(f_addr + b, pkt.data[pos++]);
        }
        mem_write8(ecb + 8, 0x00); mem_write8(ecb + 9, 0x00);
    }
}

void DosMachine::load_plane_db() {
    plane_db_.clear();
    std::string pd_path = resolve_dos_path("F14PD01.DAT");
    std::ifstream f(pd_path, std::ios::binary);
    if (!f) return;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    constexpr size_t kRecSz = 52;
    size_t count = data.size() / kRecSz;
    plane_db_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* r = data.data() + i * kRecSz;
        PlaneDbEntry e;
        e.enum_id = static_cast<int16_t>(r[0] | (r[1] << 8));
        char nm[9]{};
        std::memcpy(nm, r + 2, 8);
        e.name = nm;
        char fn[15]{};
        std::memcpy(fn, r + 16, 14);
        e.model_3dz = to_upper(std::string(fn));
        e.bclass = static_cast<int16_t>(r[50] | (r[51] << 8));
        plane_db_.push_back(std::move(e));
    }
    log("Loaded F14PD01.DAT (" + std::to_string(plane_db_.size()) + " aircraft/ship records)");
}

uint32_t DosMachine::find_f14_dgroup() const {
    if (!in_3d_flight_session_) return 0;
    static constexpr uint8_t kReqdSig[24] = {
        16,16,16,16,16,16,17,17,17,17,17,17,17,17,18,18,18,18,18,18,18,19,19,20
    };
    if (f14_dgroup_base_ != 0 && f14_dgroup_base_ + 0x239E + sizeof(kReqdSig) < 0xA0000) {
        if (std::memcmp(&mem_[f14_dgroup_base_ + 0x239E], kReqdSig, sizeof(kReqdSig)) == 0) {
            return f14_dgroup_base_;
        }
    }
    const uint8_t* found = std::search(mem_.data() + 0x10000, mem_.data() + 0x9F000,
                                       kReqdSig, kReqdSig + sizeof(kReqdSig));
    if (found != mem_.data() + 0x9F000) {
        uint32_t reqd_lin = static_cast<uint32_t>(found - mem_.data());
        if (reqd_lin >= 0x239E) {
            f14_dgroup_base_ = reqd_lin - 0x239E;
            return f14_dgroup_base_;
        }
    }
    return 0;
}

FlightTelemetry DosMachine::read_flight_telemetry() const {
    FlightTelemetry t{};
    if (!in_3d_flight_session_ || vga_.mode() != 0x13) return t;
    uint32_t dg = find_f14_dgroup();
    if (dg == 0 || dg + 0xD000 >= 0xA0000) return t;

    auto rd_u16 = [&](uint32_t off) -> uint16_t {
        uint32_t a = dg + off;
        return static_cast<uint16_t>(mem_[a] | (static_cast<uint16_t>(mem_[a + 1]) << 8));
    };
    auto rd_i16 = [&](uint32_t off) -> int16_t { return static_cast<int16_t>(rd_u16(off)); };
    auto rd_i32 = [&](uint32_t off) -> int32_t {
        return static_cast<int32_t>(rd_u16(off) | (static_cast<uint32_t>(rd_u16(off + 2)) << 16));
    };
    auto bam16_u = [](uint16_t v) -> float { return static_cast<float>(v) * (360.0f / 65536.0f); };
    auto bam16_s = [](int16_t v) -> float { return static_cast<float>(v) * (360.0f / 65536.0f); };

    // Verified F14.EXE Gold DGROUP offsets (anchored to ReqdGees @ DGROUP+0x239E):
    int32_t px_lng = rd_i32(0x9086);
    int32_t py_lng = rd_i32(0xAE26);
    int32_t vx_lng = rd_i32(0x7E5E);
    int32_t vy_lng = rd_i32(0x895A);
    if (px_lng == 0 && py_lng == 0 && vx_lng == 0 && vy_lng == 0) return t;

    t.in_flight = true;
    // Unified 20-bit coords: X = East (0..1048576), Y = South (0x100000 - PyLng, matching ps[i].yL & carrier.z/100!)
    t.world_x = static_cast<double>(px_lng);
    t.world_y = static_cast<double>(0x100000 - py_lng);
    uint16_t our_alt = rd_u16(0x2482);
    uint16_t alt4    = rd_u16(0x2480);
    t.altitude_ft = (our_alt > 0) ? static_cast<float>(our_alt) : (static_cast<float>(alt4) * 4.0f);
    t.agl_ft = static_cast<float>(std::max<int32_t>(0, rd_i32(0x2486)));

    t.yaw_deg   = bam16_u(rd_u16(0x247A));
    t.pitch_deg = std::clamp(bam16_s(rd_i16(0x247C)), -89.5f, 89.5f);
    t.roll_deg  = bam16_s(rd_i16(0x247E));

    int16_t knots = rd_i16(0x9568);
    t.airspeed_kts = static_cast<float>(std::clamp<int>(knots, 0, 1800));
    float sigma = std::pow(std::max(0.15f, 1.0f - 0.000006875f * std::min(t.altitude_ft, 55000.0f)), 4.256f);
    float tas_kts = t.airspeed_kts / std::sqrt(std::max(0.08f, sigma));
    float a_kts = std::max(573.0f, 661.5f - 0.0024f * std::min(t.altitude_ft, 36089.0f));
    t.mach = tas_kts / a_kts;
    t.wing_sweep_deg = (t.mach <= 0.65f) ? 20.0f : (t.mach >= 1.25f ? 68.0f : 20.0f + (t.mach - 0.65f) * 80.0f);

    int16_t thrust = rd_i16(0x2498);
    t.throttle_01 = std::clamp(static_cast<float>(thrust) / 144.0f, 0.0f, 1.0f);
    t.g_load = 1.0f;

    // Live weather / cloud deck from HORZ.C (DGROUP+0x2AF8 CLOUDHEIGHT, +0x2B00 ABOVECLOUDS, +0x2B06 OVERCAST)
    uint16_t cloud_h_raw = rd_u16(0x2AF8);
    t.cloud_deck_ft = static_cast<float>(cloud_h_raw) * 2.0f;
    t.above_clouds  = (rd_i16(0x2B00) != 0);
    t.overcast      = (rd_i16(0x2B06) != 0);

    // Active Camera & View Mode
    t.view_mode = rd_u16(0xAE94);
    t.back_seat_rio = (rd_i16(0x1C76) != 0);
    t.padlock_active = (t.view_mode == 0x47);
    t.is_external_cam = (t.view_mode & 0x80u) != 0;

    t.has_camera = true;
    t.cam_x = static_cast<double>(vx_lng);
    t.cam_y = static_cast<double>(0x100000 - vy_lng);
    t.cam_alt_ft = static_cast<float>(rd_i16(0x8972)) * 4.0f;
    t.cam_yaw_deg = bam16_u(rd_u16(0xC65C));
    t.cam_pitch_deg = std::clamp(bam16_s(rd_i16(0xAE9E)), -89.5f, 89.5f);
    t.cam_roll_deg = bam16_s(rd_i16(0x898C));

    if (t.view_mode == 0) {
        t.cam_x = t.world_x;
        t.cam_y = t.world_y;
        t.cam_alt_ft = t.altitude_ft;
        t.cam_yaw_deg = t.yaw_deg;
        t.cam_pitch_deg = t.pitch_deg;
        t.cam_roll_deg = t.roll_deg;
    }

    // Resolve PlaneDataList far pointer @ DGROUP+0x5012 (off) / +0x5014 (seg)
    uint16_t pdl_off = rd_u16(0x5012);
    uint16_t pdl_seg = rd_u16(0x5014);
    uint32_t pdl_lin = (pdl_seg > 0x0100 && pdl_seg < 0xA000) ? ((static_cast<uint32_t>(pdl_seg) << 4) + pdl_off) : 0;

    // Add player F-14 Tomcat as slot 0xFFFF for external chase/LSO/tower/tac views
    {
        WorldEntity pe;
        pe.world_x = t.world_x;
        pe.world_y = t.world_y;
        pe.world_z = t.altitude_ft;
        pe.yaw_deg = t.yaw_deg;
        pe.pitch_deg = t.pitch_deg;
        pe.roll_deg = t.roll_deg;
        pe.slot_id = 0xFFFF;
        pe.is_player = true;
        pe.is_friendly = true;
        pe.model_name = "F14L09C.3DZ";
        pe.label = "F-14";
        t.entities.push_back(std::move(pe));
    }

    // Read all active AI aircraft & ships from ps[0..69] @ DGROUP+0x7E64 (stride 40 B)
    int16_t pad_plane_idx = rd_i16(0x5534);
    int16_t pad_misl_idx  = rd_i16(0x5536);
    bool carrier_in_ps = false;

    for (int i = 0; i < 70; ++i) {
        uint32_t base = 0x7E64u + static_cast<uint32_t>(i) * 0x28u;
        int16_t status = rd_i16(base + 0x18);
        if ((status & 0x0001) == 0) continue; // Not ALIVE
        int32_t xL = rd_i32(base + 0x08);
        int32_t yL = rd_i32(base + 0x0C);
        if (xL == 0 && yL == 0) continue;

        WorldEntity e;
        e.slot_id = static_cast<uint16_t>(i);
        e.world_x = static_cast<double>(xL);
        e.world_y = static_cast<double>(yL);
        e.world_z = static_cast<double>(rd_i16(base + 0x06)) * 4.0;
        e.yaw_deg = bam16_u(rd_u16(base + 0x10));
        e.pitch_deg = std::clamp(bam16_s(rd_i16(base + 0x12)), -89.5f, 89.5f);
        e.roll_deg = bam16_s(rd_i16(base + 0x14));
        e.type_id = rd_u16(base + 0x16);
        e.flags = static_cast<uint16_t>(status);
        e.is_friendly = (status & 0x0400) != 0;

        if (pdl_lin != 0 && e.type_id < 45 && pdl_lin + e.type_id * 40 + 40 < 0xA0000) {
            uint32_t pde = pdl_lin + static_cast<uint32_t>(e.type_id) * 40u;
            int16_t enum_id = static_cast<int16_t>(mem_[pde] | (mem_[pde + 1] << 8));
            int16_t bclass  = static_cast<int16_t>(mem_[pde + 38] | (mem_[pde + 39] << 8));
            char nm[9]{};
            std::memcpy(nm, &mem_[pde + 2], 8);
            e.label = nm;
            e.is_carrier = (bclass == 60);
            e.is_ship = (bclass == 60 || bclass == 70);
            if (e.is_carrier) carrier_in_ps = true;
            if (enum_id >= 0 && static_cast<size_t>(enum_id) < plane_db_.size()) {
                const auto& db = plane_db_[static_cast<size_t>(enum_id)];
                if (!db.model_3dz.empty() && db.model_3dz != "NONE.3DZ") {
                    e.model_name = db.model_3dz;
                } else if (e.is_carrier) {
                    e.model_name = "NIMB08.3DZ";
                } else if (e.label.find("F-14") != std::string::npos) {
                    e.model_name = "F14L09C.3DZ";
                }
            }
        }

        if (i == pad_plane_idx) {
            t.padlock_has_target = true;
            t.padlock_target_is_missile = false;
            t.padlock_target_x = e.world_x;
            t.padlock_target_y = e.world_y;
            t.padlock_target_z = e.world_z;
            double dx = (e.world_x - t.world_x) * 1.2192;
            double dy = (e.world_y - t.world_y) * 1.2192;
            double dz = (e.world_z - t.altitude_ft) * 0.3048;
            t.padlock_range_nm = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz) / 1852.0);
            std::snprintf(t.padlock_target_name, sizeof(t.padlock_target_name), "%s",
                          e.label.empty() ? "BANDIT" : e.label.c_str());
        }
        t.entities.push_back(std::move(e));
    }

    // Add player's carrier from carrier far struct if not already in ps[]
    if (!carrier_in_ps) {
        uint16_t carr_seg = rd_u16(0x5FE4);
        if (carr_seg > 0x0100 && carr_seg < 0xA000) {
            uint32_t ca = (static_cast<uint32_t>(carr_seg) << 4) + 0x0558;
            if (ca + 24 < 0xA0000) {
                auto crd32 = [&](uint32_t o) -> int32_t {
                    return static_cast<int32_t>(mem_[ca + o] | (mem_[ca + o + 1] << 8) |
                                                (mem_[ca + o + 2] << 16) | (mem_[ca + o + 3] << 24));
                };
                int32_t cx = crd32(0), cz = crd32(8);
                uint16_t chdg = static_cast<uint16_t>(mem_[ca + 12] | (mem_[ca + 13] << 8));
                if (cx != 0 || cz != 0) {
                    WorldEntity ce;
                    ce.world_x = static_cast<double>(cx) / 100.0;
                    ce.world_y = static_cast<double>(cz) / 100.0;
                    ce.world_z = 24.0;
                    ce.yaw_deg = bam16_u(chdg);
                    ce.is_ship = true;
                    ce.is_carrier = true;
                    ce.is_friendly = true;
                    ce.model_name = "AMERB14.3DZ";
                    ce.label = "CVN";
                    t.entities.push_back(std::move(ce));
                }
            }
        }
    }

    // Read all active in-flight missiles & SAMs from ss[0..27] @ DGROUP+0x899C (stride 44 B)
    for (int i = 0; i < 28; ++i) {
        uint32_t base = 0x899Cu + static_cast<uint32_t>(i) * 0x2Cu;
        int16_t dist = rd_i16(base + 0x1A);
        if (dist <= 0) continue;
        int32_t mx = rd_i32(base + 0x00);
        int32_t my = rd_i32(base + 0x04);
        if (mx == 0 && my == 0) continue;

        WorldMissile m;
        m.slot_id = static_cast<uint16_t>(i);
        m.world_x = static_cast<double>(mx);
        m.world_y = static_cast<double>(my);
        m.world_z = static_cast<double>(rd_i16(base + 0x10)) * 4.0;
        m.yaw_deg = bam16_u(rd_u16(base + 0x14));
        m.pitch_deg = std::clamp(bam16_s(rd_i16(base + 0x16)), -89.5f, 89.5f);
        m.roll_deg = bam16_s(rd_i16(base + 0x18));
        m.kind_id = rd_u16(base + 0x1E);
        m.is_sam = (i >= 15);

        if (i == pad_misl_idx && !t.padlock_has_target) {
            t.padlock_has_target = true;
            t.padlock_target_is_missile = true;
            t.padlock_target_x = m.world_x;
            t.padlock_target_y = m.world_y;
            t.padlock_target_z = m.world_z;
            std::snprintf(t.padlock_target_name, sizeof(t.padlock_target_name), "%s", m.is_sam ? "SAM" : "MISSILE");
        }
        t.missiles.push_back(m);
    }

    return t;
}

void DosMachine::read_player_fm_extras(float& weight_lbs, float& throttle_01) const {
    weight_lbs = 58000.0f;
    throttle_01 = 0.65f;
    uint32_t dg = find_f14_dgroup();
    if (dg == 0) return;
    int16_t thr = static_cast<int16_t>(mem_[dg + 0x2498] | (mem_[dg + 0x2499] << 8));
    throttle_01 = std::clamp(static_cast<float>(thr) / 144.0f, 0.0f, 1.0f);
}

void DosMachine::read_flight_audio_extras(float& rpm_pct, float& throttle_01, int& ab_stage, float& g_load) const {
    rpm_pct = 80.0f;
    throttle_01 = 0.65f;
    ab_stage = 0;
    g_load = 1.0f;
    uint32_t dg = find_f14_dgroup();
    if (dg == 0) return;
    int16_t thr = static_cast<int16_t>(mem_[dg + 0x2498] | (mem_[dg + 0x2499] << 8));
    throttle_01 = std::clamp(static_cast<float>(thr) / 144.0f, 0.0f, 1.0f);
    rpm_pct = (thr <= 100) ? (55.0f + thr * 0.45f) : 100.0f;
    ab_stage = (thr > 100) ? std::clamp((thr - 96) / 9, 1, 5) : 0;
    int16_t gamnt = static_cast<int16_t>(mem_[dg + 0x249A] | (mem_[dg + 0x249B] << 8));
    g_load = 1.0f + static_cast<float>(gamnt) * 0.07f;
}

void DosMachine::write_player_flight_state(const F14SimState& st, bool sync_forward_cam, bool write_throttle) {
    uint32_t dg = find_f14_dgroup();
    if (dg == 0) return;
    auto wr16 = [&](uint32_t off, uint16_t v) {
        mem_[dg + off + 0] = static_cast<uint8_t>(v & 0xFF);
        mem_[dg + off + 1] = static_cast<uint8_t>(v >> 8);
    };
    auto wr32 = [&](uint32_t off, int32_t v) {
        uint32_t u = static_cast<uint32_t>(v);
        wr16(off + 0, static_cast<uint16_t>(u & 0xFFFF));
        wr16(off + 2, static_cast<uint16_t>(u >> 16));
    };
    auto deg_to_bam16 = [](double deg) -> uint16_t {
        double n = std::fmod(deg, 360.0);
        if (n < 0.0) n += 360.0;
        return static_cast<uint16_t>(std::lround(n * (65536.0 / 360.0)) & 0xFFFF);
    };

    int32_t px_lng = static_cast<int32_t>(std::lround(st.world_x));
    int32_t py_lng = static_cast<int32_t>(0x100000 - std::lround(st.world_y));
    uint16_t our_alt = static_cast<uint16_t>(std::clamp(std::lround(st.alt_ft), 0L, 65000L));
    uint16_t alt4 = static_cast<uint16_t>(our_alt / 4);
    uint16_t hdg = deg_to_bam16(st.yaw_deg);
    int16_t pit = static_cast<int16_t>(std::lround(std::clamp(st.pitch_deg, -89.5, 89.5) * (65536.0 / 360.0)));
    int16_t rol = static_cast<int16_t>(std::lround(st.roll_deg * (65536.0 / 360.0)));

    wr32(0x9086, px_lng);
    wr32(0xAE26, py_lng);
    wr16(0xB5D8, static_cast<uint16_t>((px_lng >> 5) & 0xFFFF));
    wr16(0xB726, static_cast<uint16_t>((py_lng >> 5) & 0xFFFF));
    wr16(0x247A, hdg);
    wr16(0x247C, static_cast<uint16_t>(pit));
    wr16(0x247E, static_cast<uint16_t>(rol));

    // Sync FM[3][3] rotation matrix @ DGROUP+0x2420 (signed 16-bit fixed-point, scale 32767)
    // Verified against live F14.EXE RAM: row0=(R.E,-U.E,F.E), row1=(-R.U,U.U,-F.U), row2=(R.N,-U.N,F.N)
    auto to_i15 = [](double v) -> uint16_t {
        long s = std::clamp(std::lround(v * 32767.0), -32767L, 32767L);
        return static_cast<uint16_t>(static_cast<int16_t>(s));
    };
    wr16(0x2420, to_i15( st.ry)); wr16(0x2422, to_i15(-st.uy)); wr16(0x2424, to_i15( st.fy));
    wr16(0x2426, to_i15(-st.rz)); wr16(0x2428, to_i15( st.uz)); wr16(0x242A, to_i15(-st.fz));
    wr16(0x242C, to_i15( st.rx)); wr16(0x242E, to_i15(-st.ux)); wr16(0x2430, to_i15( st.fx));
    wr16(0x2480, alt4);
    wr16(0x2482, our_alt);
    wr32(0x2486, static_cast<int32_t>(std::max(0.0, st.alt_ft - st.ground_alt_ft)));
    wr16(0x9568, static_cast<uint16_t>(std::clamp(std::lround(st.kias), 0L, 1600L)));
    if (write_throttle) {
        wr16(0x2498, static_cast<uint16_t>(std::clamp(std::lround(st.throttle_cmd * 144.0), 0L, 144L)));
    }
    if (sync_forward_cam) {
        wr32(0x7E5E, px_lng);
        wr32(0x895A, py_lng);
        wr16(0x8972, static_cast<uint16_t>(alt4 + 3));
        wr16(0xC65C, hdg);
        wr16(0xAE9E, static_cast<uint16_t>(pit));
        wr16(0x898C, static_cast<uint16_t>(rol));
    }
}

bool DosMachine::harvest_cockpit_views(Cockpit3D& out) {
    if (!in_3d_flight_session_ || vga_.mode() != 0x13) return false;
    uint32_t dg = find_f14_dgroup();
    if (dg == 0) return false;

    static constexpr uint8_t kWorldColor[256] = {
        0,2,0,2,0,0,0,2,2,2,2,1,1,1,1,0, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0
    };

    auto capture_slot = [&](Cockpit3D::ViewSlot slot) {
        std::array<uint8_t, 320 * 200> idx{};
        std::array<uint8_t, 320 * 200> mask{};
        vga_.render_indices_200(idx.data());
        for (int i = 0; i < 320 * 200; ++i) {
            mask[i] = (kWorldColor[idx[i]] == 1) ? 0 : 255;
        }
        out.set_view_indices(slot, idx.data(), mask.data());
        out.sync_palette(vga_.dac_palette());
    };

    uint16_t cur_view = static_cast<uint16_t>(mem_[dg + 0xAE94] | (mem_[dg + 0xAE95] << 8));
    bool back_seat = (static_cast<int16_t>(mem_[dg + 0x1C76] | (mem_[dg + 0x1C77] << 8)) != 0);

    // Opportunistically capture whichever Pilot/RIO cockpit view is currently active!
    if (cur_view == 0) {
        capture_slot(back_seat ? Cockpit3D::ViewRioForward : Cockpit3D::ViewForward);
    } else if (cur_view == 1) {
        capture_slot(Cockpit3D::ViewLeft);
    } else if (cur_view == 2) {
        capture_slot(Cockpit3D::ViewRight);
    } else if (cur_view == 4) {
        capture_slot(Cockpit3D::ViewRearLeft);
    } else if (cur_view == 5) {
        capture_slot(Cockpit3D::ViewRearRight);
    }

    // One-shot silent 360-deg burst harvest on flight entry (Left, Right, Rear-Left, Rear-Right -> restore F1)
    if (cur_view == 0 && !back_seat && !out.has_all_views()) {
        struct ViewProbe { uint8_t scancode; uint8_t ascii; uint16_t expect_view; Cockpit3D::ViewSlot slot; };
        static constexpr ViewProbe kProbes[] = {
            {0x4B, '4', 1, Cockpit3D::ViewLeft},
            {0x4D, '6', 2, Cockpit3D::ViewRight},
            {0x4F, '1', 4, Cockpit3D::ViewRearLeft},
            {0x51, '3', 5, Cockpit3D::ViewRearRight},
        };
        for (const auto& p : kProbes) {
            if (out.has_view(p.slot)) continue;
            push_key(p.scancode, p.ascii, true);
            for (int f = 0; f < 10; ++f) {
                step(120000);
                on_frame_tick();
                uint16_t v = static_cast<uint16_t>(mem_[dg + 0xAE94] | (mem_[dg + 0xAE95] << 8));
                if (v == p.expect_view) {
                    step(160000);
                    on_frame_tick();
                    capture_slot(p.slot);
                    break;
                }
            }
            push_key(p.scancode, p.ascii, false);
        }
        // Restore Pilot Forward cockpit (F1 = 0x3B) silently before frame swap
        push_key(0x3B, 0, true);
        for (int f = 0; f < 8; ++f) {
            step(120000);
            on_frame_tick();
            uint16_t v = static_cast<uint16_t>(mem_[dg + 0xAE94] | (mem_[dg + 0xAE95] << 8));
            if (v == 0) { step(160000); on_frame_tick(); break; }
        }
        push_key(0x3B, 0, false);
    }
    return out.has_view(Cockpit3D::ViewForward);
}

uint8_t DosMachine::active_dos_joystick_mode() const { return hotas_.recommended_gameport_mode(); }

bool DosMachine::apply_auto_joystick_calibration(bool write_joy_file, int force_mode) {
    if (!write_joy_file || game_dir_.empty()) return true;
    fs::path jp = fs::path(game_dir_) / "JOY.F14";
    std::array<uint8_t, 81> buf{};
    if (fs::exists(jp)) {
        std::ifstream in(jp, std::ios::binary);
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    }
    auto wr16 = [&](size_t off, uint16_t v) {
        buf[off + 0] = static_cast<uint8_t>(v & 0xFF);
        buf[off + 1] = static_cast<uint8_t>(v >> 8);
    };
    uint8_t mode = (force_mode >= 0) ? static_cast<uint8_t>(force_mode) : hotas_.recommended_gameport_mode();
    // Exact Fleet Defender Gold JOY.F14 calibration matching our deterministic INT 15h 84h axes (15..840, center 427):
    wr16(20, (mode >= 1) ? 3 : 0);                           // Joystick (X+Y)
    wr16(22, (mode == 3 || mode == 4) ? 8 : 0);              // ThrustStick
    wr16(24, 0);                                             // CoolyStick (handled by HotasMapper!)
    wr16(26, (mode == 2 || mode == 4) ? 4 : 0);              // RudderStick
    // MinStick[4], MaxStick[4], CenterPosition[4], LowRange[4], HighRange[4]
    wr16(28, 15);  wr16(30, 15);  wr16(32, 15);  wr16(34, 17);
    wr16(36, 840); wr16(38, 840); wr16(40, 840); wr16(42, 836);
    wr16(44, 427); wr16(46, 427); wr16(48, 427); wr16(50, 836);
    wr16(52, 412); wr16(54, 412); wr16(56, 412); wr16(58, 819);
    wr16(60, 413); wr16(62, 413); wr16(64, 413); wr16(66, 0);
    buf[80] = 12; // DeadZone
    std::ofstream out(jp, std::ios::binary | std::ios::trunc);
    if (out) out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    log("Auto-calibrated JOY.F14 for Multi-HOTAS (mode=" + std::to_string(mode) + ")");
    return true;
}

void DosMachine::deliver_hw_irq(uint8_t vec) {
    if (!emu_ || paused_ || exited_ || in_mouse_cb_) return;
    if ((emu_->x86.R_EFLG & 0x200u) == 0) return;
    uint16_t off = mem_read16(vec * 4 + 0);
    uint16_t seg = mem_read16(vec * 4 + 2);
    if (seg == 0 || seg == kHleStubSeg || seg == kEmmStubSeg) return;

    uint16_t sp = emu_->x86.R_SP;
    uint32_t ss_base = static_cast<uint32_t>(emu_->x86.R_SS) << 4;
    sp -= 2; mem_write16(ss_base + sp, static_cast<uint16_t>(emu_->x86.R_EFLG));
    sp -= 2; mem_write16(ss_base + sp, emu_->x86.R_CS);
    sp -= 2; mem_write16(ss_base + sp, emu_->x86.R_IP);
    emu_->x86.R_SP = sp;
    emu_->x86.R_EFLG &= ~0x0300u;
    x86emu_set_seg_register(emu_, emu_->x86.R_CS_SEL, seg);
    emu_->x86.R_IP = off;
}

uint64_t DosMachine::step(uint64_t max_instructions) {
    if (!emu_ || paused_ || exited_) return 0;
    uint32_t reload = (pit_reload0_ == 0) ? 65536u : static_cast<uint32_t>(pit_reload0_);
    uint64_t insns_per_irq = std::max<uint64_t>(2000, (static_cast<uint64_t>(reload) * cpu_speed_ips_) / 1193182ULL);

    uint64_t done = 0;
    while (done < max_instructions && !paused_ && !exited_) {
        if ((emu_->x86.R_EFLG & 0x200u) && !in_mouse_cb_) {
            if (!pending_hw_scancodes_.empty()) {
                last_scancode_ = pending_hw_scancodes_.front();
                pending_hw_scancodes_.pop_front();
                deliver_hw_irq(0x09);
            }
            if (pending_mouse_mask_ != 0 && (emu_->x86.R_EFLG & 0x200u)) {
                uint16_t mask = pending_mouse_mask_;
                pending_mouse_mask_ = 0;
                invoke_mouse_callback(mask);
            }
        }
        uint64_t to_irq = (pit_insn_accum_ >= insns_per_irq) ? 1 : (insns_per_irq - pit_insn_accum_);
        uint64_t chunk = std::min(max_instructions - done, std::min<uint64_t>(to_irq, 8000ULL));
        emu_->max_instr = emu_->x86.R_TSC + chunk;
        uint64_t before = emu_->x86.R_TSC;
        x86emu_run(emu_, X86EMU_RUN_MAX_INSTR | X86EMU_RUN_NO_CODE);
        uint64_t ran = emu_->x86.R_TSC - before;
        if (ran == 0) break;

        done += ran;
        total_insns_ += ran;
        pit_insn_accum_ += ran;

        if (sb_irq_pending_ && total_insns_ >= sb_irq_due_insn_ && (emu_->x86.R_EFLG & 0x200u)) {
            sb_irq_pending_ = false;
            uint16_t seg0f = mem_read16(0x0F * 4 + 2);
            if (seg0f != kHleStubSeg && seg0f != 0) deliver_hw_irq(0x0F);
            else deliver_hw_irq(0x0D);
        }
        if (pit_insn_accum_ >= insns_per_irq) {
            pit_insn_accum_ -= insns_per_irq;
            if (emu_->x86.R_EFLG & 0x200u) {
                uint16_t seg08 = mem_read16(0x08 * 4 + 2);
                if (seg08 != kHleStubSeg && seg08 != 0) deliver_hw_irq(0x08);
                else deliver_hw_irq(0x1C);
            }
        }
    }
    return done;
}

uint64_t DosMachine::effective_cpu_speed_ips() const {
    if (auto_turbo_in_flight_) {
        if (disk_turbo_sec_ > 0.0) return std::max<uint64_t>(flight_cpu_speed_ips_, 25000000ULL);
        if (in_3d_flight_session_) return flight_cpu_speed_ips_;
    }
    return cpu_speed_ips_;
}

uint64_t DosMachine::step_realtime(double dt_sec) {
    if (!emu_ || paused_ || exited_) return 0;

    if (fast_boot_active_) {
        if (total_insns_ > 150000000ULL || (fast_boot_stage_ >= 3 && fast_boot_key_at_ > 0 && total_insns_ >= fast_boot_key_at_)) {
            fast_boot_active_ = false;
            audio_.set_fast_boot_mute(false);
            log("FAST-BOOT COMPLETE -> Fleet Defender ready!");
        } else {
            auto wall_start = std::chrono::steady_clock::now();
            uint64_t done = 0;
            while (done < 2500000ULL && fast_boot_active_ && !paused_ && !exited_) {
                uint64_t ran = step(50000);
                on_frame_tick();
                done += ran;
                if (ran == 0) break;
                if (fast_boot_stage_ >= 3 && fast_boot_key_at_ > 0 && total_insns_ >= fast_boot_key_at_) {
                    fast_boot_active_ = false;
                    audio_.set_fast_boot_mute(false);
                    log("FAST-BOOT COMPLETE -> Fleet Defender ready!");
                    break;
                }
                auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - wall_start).count();
                if (elapsed_us > 14000) break;
            }
            return done;
        }
    }

    dt_sec = std::clamp(dt_sec, 0.0, 0.05);
    update_kbd_stick(dt_sec);
    if (disk_turbo_sec_ > 0.0) disk_turbo_sec_ = std::max(0.0, disk_turbo_sec_ - dt_sec);
    auto wall_start = std::chrono::steady_clock::now();

    poll_ipx_udp();

    bda_time_accum_ += dt_sec;
    constexpr double kBdaPeriod = 1.0 / 18.2065;
    while (bda_time_accum_ >= kBdaPeriod) {
        bda_time_accum_ -= kBdaPeriod;
        bda_ticks_++;
        mem_write16(0x046C, static_cast<uint16_t>(bda_ticks_ & 0xFFFF));
        mem_write16(0x046E, static_cast<uint16_t>(bda_ticks_ >> 16));
    }

    constexpr double kVgaPeriod = 1.0 / 70.08;
    uint64_t active_ips = effective_cpu_speed_ips();
    insn_budget_accum_ += dt_sec * static_cast<double>(active_ips);
    uint64_t max_burst = active_ips / 20;
    if (insn_budget_accum_ > static_cast<double>(max_burst)) insn_budget_accum_ = static_cast<double>(max_burst);
    uint64_t target_insns = static_cast<uint64_t>(insn_budget_accum_);
    insn_budget_accum_ -= static_cast<double>(target_insns);

    uint32_t reload = (pit_reload0_ == 0) ? 65536u : static_cast<uint32_t>(pit_reload0_);
    double pit_period = static_cast<double>(reload) / 1193182.0;
    pit_time_accum_ += dt_sec;

    int irqs_due = 0;
    while (pit_time_accum_ >= pit_period && irqs_due < 512) {
        pit_time_accum_ -= pit_period;
        irqs_due++;
    }
    if (pit_time_accum_ > 0.030) pit_time_accum_ = 0.0;

    int slices = std::max(8, irqs_due);
    double slice_dt = dt_sec / static_cast<double>(slices);
    uint64_t done = 0;
    uint64_t slice_insns = std::max<uint64_t>(80, target_insns / static_cast<uint64_t>(slices));

    for (int s = 0; s < slices && !paused_ && !exited_; ++s) {
        bool over_soft_budget = false;
        if (s > 0 && (s & 3) == 0) {
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - wall_start).count();
            if (elapsed_us > 13000) {
                if (s < irqs_due) pit_time_accum_ = std::min(0.025, pit_time_accum_ + static_cast<double>(irqs_due - s) * pit_period);
                break;
            }
            if (elapsed_us > 11000) {
                if (s >= irqs_due) break;
                over_soft_budget = true;
            }
        }
        vga_time_accum_ += slice_dt;
        while (vga_time_accum_ >= kVgaPeriod) vga_time_accum_ -= kVgaPeriod;
        vga_.set_vblank(vga_time_accum_ >= (kVgaPeriod * 0.85));

        if ((emu_->x86.R_EFLG & 0x200u) && !in_mouse_cb_) {
            if (!pending_hw_scancodes_.empty()) {
                last_scancode_ = pending_hw_scancodes_.front();
                pending_hw_scancodes_.pop_front();
                deliver_hw_irq(0x09);
            }
            if (pending_mouse_mask_ != 0 && (emu_->x86.R_EFLG & 0x200u)) {
                uint16_t mask = pending_mouse_mask_;
                pending_mouse_mask_ = 0;
                invoke_mouse_callback(mask);
            }
        }

        uint64_t chunk = 0;
        if (!over_soft_budget && done < target_insns) chunk = std::min(target_insns - done, slice_insns);
        else if (s < irqs_due) chunk = 120;
        else break;
        if (chunk == 0) break;

        emu_->max_instr = emu_->x86.R_TSC + chunk;
        uint64_t before = emu_->x86.R_TSC;
        x86emu_run(emu_, X86EMU_RUN_MAX_INSTR | X86EMU_RUN_NO_CODE);
        uint64_t ran = emu_->x86.R_TSC - before;
        done += ran;
        total_insns_ += ran;

        if (sb_irq_pending_ && total_insns_ >= sb_irq_due_insn_ && (emu_->x86.R_EFLG & 0x200u)) {
            sb_irq_pending_ = false;
            uint16_t seg0f = mem_read16(0x0F * 4 + 2);
            if (seg0f != kHleStubSeg && seg0f != 0) deliver_hw_irq(0x0F);
            else deliver_hw_irq(0x0D);
        }
        if (s < irqs_due && (emu_->x86.R_EFLG & 0x200u)) {
            uint16_t seg08 = mem_read16(0x08 * 4 + 2);
            if (seg08 != kHleStubSeg && seg08 != 0) deliver_hw_irq(0x08);
            else deliver_hw_irq(0x1C);
        }
    }
    return done;
}

void DosMachine::on_frame_tick() {
    update_kbd_stick(1.0 / 60.0);
    vga_.on_vblank_tick();
    poll_ipx_udp();
    bda_ticks_++;
    mem_write16(0x046C, static_cast<uint16_t>(bda_ticks_ & 0xFFFF));
    mem_write16(0x046E, static_cast<uint16_t>(bda_ticks_ >> 16));
}

void DosMachine::update_kbd_stick(double dt_sec) {
    float dt = static_cast<float>(std::clamp(dt_sec, 0.001, 0.05));
    constexpr float kDeflectRate = 3.8f;
    constexpr float kCenterRate  = 5.5f;
    float tx = (kbd_right_ ? 1.0f : 0.0f) - (kbd_left_ ? 1.0f : 0.0f);
    float ty = (kbd_down_  ? 1.0f : 0.0f) - (kbd_up_   ? 1.0f : 0.0f);
    if (tx != 0.0f) {
        kbd_stick_x_ = std::clamp(kbd_stick_x_ + tx * kDeflectRate * dt, -1.0f, 1.0f);
    } else if (std::abs(kbd_stick_x_) > 0.001f) {
        float step = kCenterRate * dt;
        kbd_stick_x_ = (std::abs(kbd_stick_x_) <= step) ? 0.0f : (kbd_stick_x_ - std::copysign(step, kbd_stick_x_));
    }
    if (ty != 0.0f) {
        kbd_stick_y_ = std::clamp(kbd_stick_y_ + ty * kDeflectRate * dt, -1.0f, 1.0f);
    } else if (std::abs(kbd_stick_y_) > 0.001f) {
        float step = kCenterRate * dt;
        kbd_stick_y_ = (std::abs(kbd_stick_y_) <= step) ? 0.0f : (kbd_stick_y_ - std::copysign(step, kbd_stick_y_));
    }
}

float DosMachine::effective_roll_axis() const {
    float hotas_v = hotas_.is_axis_active(FlightAxis::Roll) ? hotas_.axis_value(FlightAxis::Roll) : 0.0f;
    if (flight_input_mode_ == FlightInputMode::ForceHotas) return hotas_v;
    if (flight_input_mode_ == FlightInputMode::ForceKeyboard) return kbd_stick_x_;
    // AutoSeamless: HOTAS wins when deflected; arrow keys work seamlessly otherwise!
    return (std::abs(hotas_v) > 0.06f) ? hotas_v : kbd_stick_x_;
}

float DosMachine::effective_pitch_axis() const {
    float hotas_v = hotas_.is_axis_active(FlightAxis::Pitch) ? hotas_.axis_value(FlightAxis::Pitch) : 0.0f;
    if (flight_input_mode_ == FlightInputMode::ForceHotas) return hotas_v;
    if (flight_input_mode_ == FlightInputMode::ForceKeyboard) return kbd_stick_y_;
    return (std::abs(hotas_v) > 0.06f) ? hotas_v : kbd_stick_y_;
}

void DosMachine::push_key(uint8_t scancode, uint8_t ascii, bool pressed) {
    // Update DOS BIOS shift/modifier flags (BDA 0x0417) and do NOT enqueue bare modifiers into INT 16h!
    auto set_mod = [&](uint8_t bit) {
        if (pressed) kbd_shift_flags_ |= bit;
        else kbd_shift_flags_ &= static_cast<uint8_t>(~bit);
        mem_write8(0x0417, kbd_shift_flags_);
    };
    bool is_modifier = false;
    if (scancode == 0x2A) { set_mod(0x02); is_modifier = true; }      // Left Shift
    else if (scancode == 0x36) { set_mod(0x01); is_modifier = true; } // Right Shift
    else if (scancode == 0x1D) { set_mod(0x04); is_modifier = true; } // Ctrl
    else if (scancode == 0x38) { set_mod(0x08); is_modifier = true; } // Alt

    // Seamless keyboard flight stick tracking (Arrow keys + KP5 center)
    if (scancode == 0x48) kbd_up_ = pressed;
    else if (scancode == 0x50) kbd_down_ = pressed;
    else if (scancode == 0x4B) kbd_left_ = pressed;
    else if (scancode == 0x4D) kbd_right_ = pressed;
    else if (scancode == 0x4C && pressed) { kbd_stick_x_ = 0.0f; kbd_stick_y_ = 0.0f; }

    uint8_t hw = pressed ? scancode : static_cast<uint8_t>(scancode | 0x80);
    last_scancode_ = hw;
    pending_hw_scancodes_.push_back(hw);
    if (pressed && !is_modifier) key_queue_.push_back({scancode, ascii});
}

void DosMachine::invoke_mouse_callback(uint16_t event_mask) {
    if (!emu_ || in_mouse_cb_ || mouse_cb_seg_ == 0 || (event_mask & mouse_cb_mask_) == 0) return;
    in_mouse_cb_ = true;
    constexpr uint16_t kTrampOff = 0x03F0;
    mem_write8((static_cast<uint32_t>(kHleStubSeg) << 4) + kTrampOff + 0, 0xCD);
    mem_write8((static_cast<uint32_t>(kHleStubSeg) << 4) + kTrampOff + 1, 0xFF);

    auto saved_x86 = emu_->x86;
    uint64_t saved_max_instr = emu_->max_instr;

    uint16_t sp = saved_x86.R_SP;
    uint32_t ss_base = static_cast<uint32_t>(saved_x86.R_SS) << 4;
    sp -= 2; mem_write16(ss_base + sp, kHleStubSeg);
    sp -= 2; mem_write16(ss_base + sp, kTrampOff);
    emu_->x86.R_SP = sp;

    emu_->x86.R_AX = event_mask;
    emu_->x86.R_BX = mouse_buttons_;
    emu_->x86.R_CX = static_cast<uint16_t>(std::clamp(mouse_x_ * 2, mouse_min_x_, mouse_max_x_));
    emu_->x86.R_DX = static_cast<uint16_t>(std::clamp(mouse_y_, mouse_min_y_, mouse_max_y_));
    emu_->x86.R_SI = 0;
    emu_->x86.R_DI = 0;
    emu_->x86.R_EFLG &= ~0x200u;
    x86emu_set_seg_register(emu_, emu_->x86.R_CS_SEL, mouse_cb_seg_);
    emu_->x86.R_IP = mouse_cb_off_;

    emu_->max_instr = emu_->x86.R_TSC + 50000;
    x86emu_run(emu_, X86EMU_RUN_MAX_INSTR | X86EMU_RUN_NO_CODE);

    uint64_t new_tsc = emu_->x86.R_TSC;
    emu_->x86 = saved_x86;
    emu_->x86.R_TSC = new_tsc;
    emu_->max_instr = saved_max_instr;
    in_mouse_cb_ = false;
}

void DosMachine::set_mouse_state(int x_320, int y_200, uint8_t buttons_mask) {
    int nx = std::clamp(x_320, 0, 319);
    int ny = std::clamp(y_200, 0, 199);
    int dx = nx - mouse_x_;
    int dy = ny - mouse_y_;
    uint16_t ev = 0;
    if (dx != 0 || dy != 0) {
        ev |= 0x01;
        mouse_mickey_x_ += dx;
        mouse_mickey_y_ += dy;
    }
    if ((buttons_mask & 1) && !(mouse_buttons_ & 1)) { ev |= 0x02; mouse_press_cnt_[0]++; }
    if (!(buttons_mask & 1) && (mouse_buttons_ & 1)) { ev |= 0x04; mouse_rel_cnt_[0]++; }
    if ((buttons_mask & 2) && !(mouse_buttons_ & 2)) { ev |= 0x08; mouse_press_cnt_[1]++; }
    if (!(buttons_mask & 2) && (mouse_buttons_ & 2)) { ev |= 0x10; mouse_rel_cnt_[1]++; }
    mouse_x_ = nx;
    mouse_y_ = ny;
    mouse_buttons_ = buttons_mask;
    pending_mouse_mask_ |= ev;
}

void DosMachine::render_vga_rgba_400(uint32_t* out_rgba) const {
    vga_.render_rgba_400(mem_.data(), out_rgba);
}

CpuSnapshot DosMachine::snapshot() const {
    CpuSnapshot s;
    if (emu_) {
        s.cs = emu_->x86.R_CS; s.ip = emu_->x86.R_IP;
        s.ss = emu_->x86.R_SS; s.sp = emu_->x86.R_SP;
        s.ds = emu_->x86.R_DS; s.es = emu_->x86.R_ES;
        s.eax = emu_->x86.R_EAX; s.ebx = emu_->x86.R_EBX;
        s.ecx = emu_->x86.R_ECX; s.edx = emu_->x86.R_EDX;
        s.esi = emu_->x86.R_ESI; s.edi = emu_->x86.R_EDI;
        s.ebp = emu_->x86.R_EBP; s.eflags = emu_->x86.R_EFLG;
    }
    s.total_insns = total_insns_;
    s.exited = exited_;
    s.exit_code = exit_code_;
    s.ems_pages_used = ems_pages_used_;
    s.ems_pages_total = EMS_TOTAL_PAGES;
    s.proc_depth = static_cast<int>(proc_stack_.size());
    s.active_proc = active_proc_name_;
    return s;
}

} // namespace f14

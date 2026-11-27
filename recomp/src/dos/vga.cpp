#include "dos/vga.h"

#include <algorithm>
#include <cstring>

namespace f14 {

namespace {
constexpr uint8_t kEga16[16][3] = {
    { 0,  0,  0}, { 0,  0, 42}, { 0, 42,  0}, { 0, 42, 42},
    {42,  0,  0}, {42,  0, 42}, {42, 21,  0}, {42, 42, 42},
    {21, 21, 21}, {21, 21, 63}, {21, 63, 21}, {21, 63, 63},
    {63, 21, 21}, {63, 21, 63}, {63, 63, 21}, {63, 63, 63},
};
} // namespace

VgaDevice::VgaDevice() {
    reset();
}

void VgaDevice::reset() {
    current_mode_ = 0x03;
    vram_.fill(0);
    latches_.fill(0);
    dac_write_idx_ = 0;
    dac_write_comp_ = 0;
    dac_read_idx_ = 0;
    dac_read_comp_ = 0;
    status_retrace_ = 0;
    status_poll_count_ = 0;
    crtc_index_ = 0;
    seq_index_ = 0;
    gc_index_ = 0;
    ac_index_ = 0;
    ac_flipflop_ = false;
    crtc_regs_.fill(0);
    seq_regs_.fill(0);
    gc_regs_.fill(0);
    seq_regs_[2] = 0x0F;
    seq_regs_[4] = 0x0E;
    gc_regs_[8] = 0xFF;
    crtc_regs_[0x09] = 0x41; // Double-scanned 200-line default for Mode 13h
    crtc_regs_[0x12] = 0x8F; // 400 scanlines (0x18F = 399)
    crtc_regs_[0x07] = 0x1F;
    crtc_regs_[0x13] = 40;   // 80 bytes/row per plane
    cursor_x_ = 0;
    cursor_y_ = 0;
    init_default_palette();
}

void VgaDevice::init_default_palette() {
    dac_.fill(0);
    for (int i = 0; i < 16; ++i) {
        dac_[i * 3 + 0] = kEga16[i][0];
        dac_[i * 3 + 1] = kEga16[i][1];
        dac_[i * 3 + 2] = kEga16[i][2];
    }
    for (int i = 0; i < 16; ++i) {
        uint8_t v = static_cast<uint8_t>(i * 63 / 15);
        dac_[(16 + i) * 3 + 0] = v;
        dac_[(16 + i) * 3 + 1] = v;
        dac_[(16 + i) * 3 + 2] = v;
    }
    for (int i = 32; i < 256; ++i) {
        dac_[i * 3 + 0] = static_cast<uint8_t>(((i >> 5) & 7) * 9);
        dac_[i * 3 + 1] = static_cast<uint8_t>(((i >> 2) & 7) * 9);
        dac_[i * 3 + 2] = static_cast<uint8_t>((i & 3) * 21);
    }
}

void VgaDevice::set_mode(uint8_t mode) {
    bool no_clear = (mode & 0x80) != 0;
    current_mode_ = mode & 0x7F;
    cursor_x_ = 0;
    cursor_y_ = 0;
    seq_regs_[2] = 0x0F;
    seq_regs_[4] = (current_mode_ == 0x13) ? 0x0E : 0x02;
    gc_regs_[4] = 0x00;
    gc_regs_[5] = 0x40;
    gc_regs_[8] = 0xFF;
    crtc_regs_[0x0C] = 0;
    crtc_regs_[0x0D] = 0;
    crtc_regs_[0x09] = (current_mode_ == 0x13) ? 0x41 : 0x4F; // Mode 13h: max scanline = 1 (double scan -> 200 lines)
    crtc_regs_[0x12] = 0x8F;
    crtc_regs_[0x07] = 0x1F;
    crtc_regs_[0x13] = 40;
    if (!no_clear) {
        vram_.fill(0);
    }
    init_default_palette();
}

int VgaDevice::visible_height() const {
    if (current_mode_ != 0x13) return 200;
    // CRTC reg 0x09 bits 0..4 = Maximum Scan Line (0 = 1 scanline/row -> 400 rows, 1 = 2 scanlines/row -> 200 rows)
    // Bit 7 = 200-to-400 line conversion (double scan)
    uint8_t reg09 = crtc_regs_[0x09];
    bool double_scan = ((reg09 & 0x80) != 0) || ((reg09 & 0x1F) >= 1);
    return double_scan ? 200 : 400;
}

uint8_t VgaDevice::vram_read(uint16_t offset) {
    if (!is_unchained()) {
        uint8_t plane = offset & 3;
        uint32_t addr = offset >> 2;
        return vram_[plane * PLANE_SIZE + addr];
    }
    for (int p = 0; p < 4; ++p) {
        latches_[p] = vram_[p * PLANE_SIZE + offset];
    }
    uint8_t rplane = gc_regs_[4] & 3;
    return latches_[rplane];
}

void VgaDevice::vram_write(uint16_t offset, uint8_t val) {
    if (!is_unchained()) {
        uint8_t plane = offset & 3;
        uint32_t addr = offset >> 2;
        vram_[plane * PLANE_SIZE + addr] = val;
        return;
    }
    uint8_t map_mask = seq_regs_[2] & 0x0F;
    uint8_t write_mode = gc_regs_[5] & 0x03;
    for (int p = 0; p < 4; ++p) {
        if (map_mask & (1u << p)) {
            if (write_mode == 1) {
                vram_[p * PLANE_SIZE + offset] = latches_[p];
            } else {
                vram_[p * PLANE_SIZE + offset] = val;
            }
        }
    }
}

void VgaDevice::set_dac_color(uint8_t index, uint8_t r6, uint8_t g6, uint8_t b6) {
    dac_[index * 3 + 0] = r6 & 0x3F;
    dac_[index * 3 + 1] = g6 & 0x3F;
    dac_[index * 3 + 2] = b6 & 0x3F;
}

void VgaDevice::get_dac_color(uint8_t index, uint8_t& r6, uint8_t& g6, uint8_t& b6) const {
    r6 = dac_[index * 3 + 0];
    g6 = dac_[index * 3 + 1];
    b6 = dac_[index * 3 + 2];
}

uint8_t VgaDevice::port_read(uint16_t port) {
    switch (port) {
        case 0x3DA: {
            ac_flipflop_ = false;
            status_poll_count_++;
            uint8_t val = status_retrace_;
            // Bit 0 (display enable / hblank) toggles per poll
            status_retrace_ ^= 0x01;
            // In headless non-realtime mode, toggle bit 3 every 256 polls so wait-vblank loops advance.
            // In real-time GUI mode, bit 3 is driven strictly by wall-clock 70.08 Hz CRT phase!
            if (!realtime_vblank_ && (status_poll_count_ & 0xFF) == 0) {
                status_retrace_ ^= 0x08;
            }
            return val;
        }
        case 0x3C4:
            return seq_index_;
        case 0x3C5:
            return seq_regs_[seq_index_ & 0x0F];
        case 0x3C7:
            return 0x03;
        case 0x3C8:
            return dac_write_idx_;
        case 0x3C9: {
            uint8_t val = dac_[dac_read_idx_ * 3 + dac_read_comp_];
            if (++dac_read_comp_ >= 3) {
                dac_read_comp_ = 0;
                dac_read_idx_++;
            }
            return val;
        }
        case 0x3CE:
            return gc_index_;
        case 0x3CF:
            return gc_regs_[gc_index_ & 0x0F];
        case 0x3D4:
            return crtc_index_;
        case 0x3D5:
            return crtc_regs_[crtc_index_];
        default:
            return 0xFF;
    }
}

void VgaDevice::port_write(uint16_t port, uint8_t val) {
    switch (port) {
        case 0x3C4:
            seq_index_ = val;
            break;
        case 0x3C5:
            seq_regs_[seq_index_ & 0x0F] = val;
            break;
        case 0x3C7:
            dac_read_idx_ = val;
            dac_read_comp_ = 0;
            break;
        case 0x3C8:
            dac_write_idx_ = val;
            dac_write_comp_ = 0;
            break;
        case 0x3C9:
            dac_[dac_write_idx_ * 3 + dac_write_comp_] = val & 0x3F;
            if (++dac_write_comp_ >= 3) {
                dac_write_comp_ = 0;
                dac_write_idx_++;
            }
            break;
        case 0x3CE:
            gc_index_ = val;
            break;
        case 0x3CF:
            gc_regs_[gc_index_ & 0x0F] = val;
            break;
        case 0x3D4:
            crtc_index_ = val;
            break;
        case 0x3D5:
            crtc_regs_[crtc_index_] = val;
            break;
        case 0x3C0:
            if (!ac_flipflop_) {
                ac_index_ = val;
            }
            ac_flipflop_ = !ac_flipflop_;
            break;
        default:
            break;
    }
}

void VgaDevice::set_vblank(bool in_retrace) {
    realtime_vblank_ = true;
    if (in_retrace) status_retrace_ |= 0x09;
    else status_retrace_ &= ~0x08;
}

void VgaDevice::on_vblank_tick() {
    realtime_vblank_ = false;
    status_retrace_ ^= 0x08;
}

void VgaDevice::tty_putc(uint8_t* mem_1mb, char c) {
    uint8_t* text_mem = mem_1mb + 0xB8000;
    if (c == '\r') {
        cursor_x_ = 0;
        return;
    }
    if (c == '\n') {
        cursor_x_ = 0;
        cursor_y_++;
    } else if (c == '\b') {
        if (cursor_x_ > 0) cursor_x_--;
    } else {
        int off = (cursor_y_ * 80 + cursor_x_) * 2;
        text_mem[off] = static_cast<uint8_t>(c);
        text_mem[off + 1] = 0x07;
        cursor_x_++;
        if (cursor_x_ >= 80) {
            cursor_x_ = 0;
            cursor_y_++;
        }
    }
    if (cursor_y_ >= 25) {
        std::memmove(text_mem, text_mem + 160, 24 * 160);
        for (int i = 0; i < 80; ++i) {
            text_mem[24 * 160 + i * 2] = ' ';
            text_mem[24 * 160 + i * 2 + 1] = 0x07;
        }
        cursor_y_ = 24;
    }
}

void VgaDevice::render_indices_200(uint8_t* out_idx) const {
    if (current_mode_ != 0x13) {
        std::memset(out_idx, 0, WIDTH * 200);
        return;
    }
    uint16_t start = crtc_start_address();
    uint16_t row_stride = crtc_regs_[0x13] ? static_cast<uint16_t>(crtc_regs_[0x13] * 2) : 80;
    for (int y = 0; y < 200; ++y) {
        uint32_t row_off = static_cast<uint32_t>(start) + static_cast<uint32_t>(y) * row_stride;
        uint8_t* dst = out_idx + y * WIDTH;
        for (int x = 0; x < WIDTH; ++x) {
            uint8_t plane = static_cast<uint8_t>(x & 3);
            uint32_t addr = (row_off + static_cast<uint32_t>(x >> 2)) & 0xFFFFu;
            dst[x] = vram_[plane * PLANE_SIZE + addr];
        }
    }
}

void VgaDevice::render_overlay_layers_200(uint32_t* out_cockpit_rgba, uint32_t* out_hud_rgba,
                                          int* out_world_y_min, int* out_world_y_max) const {
    if (out_world_y_min) *out_world_y_min = 0;
    if (out_world_y_max) *out_world_y_max = 95;
    if (current_mode_ != 0x13) {
        if (out_cockpit_rgba) std::memset(out_cockpit_rgba, 0, WIDTH * 200 * sizeof(uint32_t));
        if (out_hud_rgba) std::memset(out_hud_rgba, 0, WIDTH * 200 * sizeof(uint32_t));
        return;
    }
    uint16_t start = crtc_start_address();
    uint16_t row_stride = crtc_regs_[0x13] ? static_cast<uint16_t>(crtc_regs_[0x13] * 2) : 80;
    // Exact Fleet Defender WorldColor[256] palette classification (from MicroProse FLIR.C):
    //   0 = Cockpit / HUD / Black (0x06 = HUD_COLOR green/amber!)
    //   2 = Cockpit lit panels / gauges / switches
    //   1 = Pure out-the-window 3D world (sky, horizon, ocean, clouds, terrain, aircraft)
    static constexpr uint8_t kWorldColor[256] = {
        0,2,0,2,0,0,0,2,2,2,2,1,1,1,1,0,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0
    };

    int w_ymin = 200;
    int w_ymax = -1;
    for (int y = 0; y < 200; ++y) {
        uint32_t row_off = static_cast<uint32_t>(start) + static_cast<uint32_t>(y) * row_stride;
        uint32_t* dst_cockpit = out_cockpit_rgba ? (out_cockpit_rgba + y * WIDTH) : nullptr;
        uint32_t* dst_hud = out_hud_rgba ? (out_hud_rgba + y * WIDTH) : nullptr;
        int world_px_in_row = 0;
        for (int x = 0; x < WIDTH; ++x) {
            uint8_t plane = static_cast<uint8_t>(x & 3);
            uint32_t addr = (row_off + static_cast<uint32_t>(x >> 2)) & 0xFFFFu;
            uint8_t idx = vram_[plane * PLANE_SIZE + addr];
            uint32_t r = (dac_[idx * 3 + 0] * 255) / 63;
            uint32_t g = (dac_[idx * 3 + 1] * 255) / 63;
            uint32_t b = (dac_[idx * 3 + 2] * 255) / 63;
            uint32_t rgba = 0xFF000000u | (b << 16) | (g << 8) | r;

            bool is_world = (kWorldColor[idx] == 1);
            // Keep lower cockpit instruments (VDI / HSD / RWR below y=112) solid even if they share green/radar colors
            if (y >= 115 && (idx == 0x06 || (g > 140 && r < 80 && b < 80))) {
                is_world = false;
            }
            if (is_world) {
                world_px_in_row++;
            }
            if (dst_cockpit) {
                dst_cockpit[x] = is_world ? 0x00000000u : rgba;
            }
            if (dst_hud) {
                bool in_hud_glass = (y >= 8 && y <= 104 && x >= 60 && x <= 260);
                bool is_hud_pixel = in_hud_glass && ((idx == 0x06) || (!is_world && g >= 180 && r <= 120 && b <= 90));
                dst_hud[x] = is_hud_pixel ? rgba : 0x00000000u;
            }
        }
        if (world_px_in_row >= 60) {
            if (y < w_ymin) w_ymin = y;
            if (y > w_ymax) w_ymax = y;
        }
    }
    if (w_ymax >= w_ymin) {
        if (out_world_y_min) *out_world_y_min = w_ymin;
        if (out_world_y_max) *out_world_y_max = w_ymax;
    }
}

void VgaDevice::render_rgba_400(const uint8_t* mem_1mb, uint32_t* out_rgba) const {
    if (current_mode_ == 0x13) {
        int h = visible_height();
        uint16_t start = crtc_start_address();
        uint16_t row_stride = crtc_regs_[0x13] ? static_cast<uint16_t>(crtc_regs_[0x13] * 2) : 80;

        for (int y = 0; y < h; ++y) {
            uint32_t row_off = static_cast<uint32_t>(start) + static_cast<uint32_t>(y) * row_stride;
            uint32_t* dst0 = (h == 200) ? (out_rgba + (y * 2) * WIDTH) : (out_rgba + y * WIDTH);
            for (int x = 0; x < WIDTH; ++x) {
                uint8_t plane = static_cast<uint8_t>(x & 3);
                uint32_t addr = (row_off + static_cast<uint32_t>(x >> 2)) & 0xFFFFu;
                uint8_t idx = vram_[plane * PLANE_SIZE + addr];
                uint32_t r = (dac_[idx * 3 + 0] * 255) / 63;
                uint32_t g = (dac_[idx * 3 + 1] * 255) / 63;
                uint32_t b = (dac_[idx * 3 + 2] * 255) / 63;
                dst0[x] = 0xFF000000u | (b << 16) | (g << 8) | r;
            }
            if (h == 200) {
                std::memcpy(dst0 + WIDTH, dst0, WIDTH * sizeof(uint32_t));
            }
        }
    } else {
        const uint8_t* text_mem = mem_1mb + 0xB8000;
        std::fill(out_rgba, out_rgba + WIDTH * MAX_HEIGHT, 0xFF141010u);
        for (int row = 0; row < 25; ++row) {
            for (int col = 0; col < 80; ++col) {
                uint8_t ch = text_mem[(row * 80 + col) * 2];
                uint8_t attr = text_mem[(row * 80 + col) * 2 + 1];
                uint8_t fg = attr & 0x0F;
                uint8_t bg = (attr >> 4) & 0x07;
                uint32_t bg_c = 0xFF000000u |
                                (static_cast<uint32_t>((kEga16[bg][2] * 255) / 63) << 16) |
                                (static_cast<uint32_t>((kEga16[bg][1] * 255) / 63) << 8) |
                                static_cast<uint32_t>((kEga16[bg][0] * 255) / 63);
                uint32_t fg_c = 0xFF000000u |
                                (static_cast<uint32_t>((kEga16[fg][2] * 255) / 63) << 16) |
                                (static_cast<uint32_t>((kEga16[fg][1] * 255) / 63) << 8) |
                                static_cast<uint32_t>((kEga16[fg][0] * 255) / 63);
                for (int y = 0; y < 16; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        bool lit = (ch > 0x20) && (x >= 1 && x <= 2 && y >= 2 && y <= 13);
                        out_rgba[(row * 16 + y) * WIDTH + (col * 4 + x)] = lit ? fg_c : bg_c;
                    }
                }
            }
        }
    }
}

} // namespace f14

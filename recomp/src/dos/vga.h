#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace f14 {

class VgaDevice {
public:
    static constexpr int WIDTH = 320;
    static constexpr int MAX_HEIGHT = 400;
    static constexpr uint32_t PLANE_SIZE = 65536;

    VgaDevice();

    void reset();
    void set_mode(uint8_t mode);
    uint8_t mode() const { return current_mode_; }

    // Current visible height (200 for standard Mode 13h/Mode X, 400 when double-scan is disabled!)
    int visible_height() const;

    // VGA VRAM access (0xA0000..0xAFFFF) supporting both Chain-4 linear Mode 13h & Unchained Mode X
    uint8_t vram_read(uint16_t offset);
    void vram_write(uint16_t offset, uint8_t val);

    // I/O port handlers
    uint8_t port_read(uint16_t port);
    void port_write(uint16_t port, uint8_t val);

    // Direct palette helpers (INT 10h AH=10h)
    void set_dac_color(uint8_t index, uint8_t r6, uint8_t g6, uint8_t b6);
    void get_dac_color(uint8_t index, uint8_t& r6, uint8_t& g6, uint8_t& b6) const;

    // Advance vsync state: set_vblank(true/false) for real-time 70.08Hz CRT timing,
    // or on_vblank_tick() for headless deterministic mode.
    void set_vblank(bool in_retrace);
    void on_vblank_tick();

    // Render current VGA memory to RGBA32 (always fills WIDTH * MAX_HEIGHT, upscaling 200->400 when double-scanned)
    void render_rgba_400(const uint8_t* mem_1mb, uint32_t* out_rgba) const;
    // Extract raw 8-bit palette indices (320x200) for cockpit/HUD layer separation
    void render_indices_200(uint8_t* out_idx) const;
    // Extract palette-separated 320x200 RGBA layers for HD 3D compositing:
    //  - out_cockpit_rgba: opaque cockpit frame, canopy bow, HUD rails, chase/padlock bars, & live HUD (3D world = alpha 0)
    //  - out_hud_rgba:     ONLY the live original green HUD symbology (everything else = alpha 0)
    //  - out_world_y_min / out_world_y_max: detected vertical span of 3D world pixels (idx >= 128) for exact view-center projection!
    void render_overlay_layers_200(uint32_t* out_cockpit_rgba, uint32_t* out_hud_rgba,
                                   int* out_world_y_min = nullptr, int* out_world_y_max = nullptr) const;
    const uint8_t* dac_palette() const { return dac_.data(); }

    // Text mode cursor / TTY output helper
    void tty_putc(uint8_t* mem_1mb, char c);

    bool is_unchained() const { return (seq_regs_[4] & 0x08) == 0; }
    uint16_t crtc_start_address() const {
        return static_cast<uint16_t>((crtc_regs_[0x0C] << 8) | crtc_regs_[0x0D]);
    }
    uint8_t crtc_reg(uint8_t idx) const { return crtc_regs_[idx]; }
    uint8_t seq_reg(uint8_t idx) const { return seq_regs_[idx & 0x0F]; }
    uint8_t gc_reg(uint8_t idx) const { return gc_regs_[idx & 0x0F]; }

private:
    uint8_t current_mode_ = 0x03;

    // 256 KB VGA VRAM (4 planes x 64 KB)
    std::array<uint8_t, 4 * PLANE_SIZE> vram_{};
    std::array<uint8_t, 4> latches_{};

    // 256-entry 6-bit VGA DAC palette (0..63 per channel)
    std::array<uint8_t, 256 * 3> dac_{};
    uint8_t dac_write_idx_ = 0;
    uint8_t dac_write_comp_ = 0;
    uint8_t dac_read_idx_ = 0;
    uint8_t dac_read_comp_ = 0;

    // VGA controller registers
    uint8_t status_retrace_ = 0;
    bool realtime_vblank_ = false;
    uint32_t status_poll_count_ = 0;
    uint8_t crtc_index_ = 0;
    uint8_t seq_index_ = 0;
    uint8_t gc_index_ = 0;
    uint8_t ac_index_ = 0;
    bool ac_flipflop_ = false;
    std::array<uint8_t, 256> crtc_regs_{};
    std::array<uint8_t, 16> seq_regs_{};
    std::array<uint8_t, 16> gc_regs_{};

    // Text cursor (80x25)
    int cursor_x_ = 0;
    int cursor_y_ = 0;

    void init_default_palette();
};

} // namespace f14

#pragma once

#include <string>

namespace f14 {

enum class KeyCategory : int {
    All = 0,
    Flight,
    Weapons,
    RadarRio,
    Views,
    CarrierSys,
    NativeHost,
    Count
};

struct QuickKeysState {
    KeyCategory filter = KeyCategory::All;
    int hovered_index = -1;
};

// Draws a dynamically scaled, resolution-independent vector ANSI keyboard + Numpad
// using ImDrawList geometry. Scales seamlessly to whatever width is available.
void draw_quick_keys_panel(QuickKeysState& state, const std::string& external_chart_path = "");

} // namespace f14

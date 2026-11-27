#include "ui/quick_keys.h"
#include "ui/win_helpers.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace f14 {

namespace {

struct KeyDef {
    float x = 0.0f;       // Keyboard units from left
    float y = 0.0f;       // Row units from top
    float w = 1.0f;       // Width in units
    float h = 1.0f;       // Height in units
    const char* cap;      // Keycap glyph ("F1", "A", "Space", etc.)
    const char* short1;   // Primary short label on keycap
    const char* short2;   // Secondary short label (e.g. Shift/Ctrl/Alt)
    KeyCategory cat;      // Category for color-coding & filter
    const char* detail;   // Full hover description
};

ImU32 cat_fill_col(KeyCategory cat, bool dim, bool hovered) {
    ImVec4 c(0.16f, 0.20f, 0.27f, 1.0f);
    switch (cat) {
        case KeyCategory::Flight:     c = ImVec4(0.12f, 0.30f, 0.44f, 1.0f); break;
        case KeyCategory::Weapons:    c = ImVec4(0.46f, 0.16f, 0.16f, 1.0f); break;
        case KeyCategory::RadarRio:   c = ImVec4(0.13f, 0.36f, 0.24f, 1.0f); break;
        case KeyCategory::Views:      c = ImVec4(0.32f, 0.20f, 0.44f, 1.0f); break;
        case KeyCategory::CarrierSys: c = ImVec4(0.42f, 0.32f, 0.12f, 1.0f); break;
        case KeyCategory::NativeHost: c = ImVec4(0.08f, 0.42f, 0.48f, 1.0f); break;
        default:                      c = ImVec4(0.15f, 0.18f, 0.23f, 1.0f); break;
    }
    if (dim) {
        c.x *= 0.42f; c.y *= 0.42f; c.z *= 0.42f;
    } else if (hovered) {
        c.x = std::min(1.0f, c.x * 1.38f + 0.08f);
        c.y = std::min(1.0f, c.y * 1.38f + 0.08f);
        c.z = std::min(1.0f, c.z * 1.38f + 0.08f);
    }
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImU32 cat_border_col(KeyCategory cat, bool dim, bool hovered) {
    if (hovered) return IM_COL32(255, 240, 160, 255);
    ImVec4 c(0.35f, 0.42f, 0.52f, 1.0f);
    switch (cat) {
        case KeyCategory::Flight:     c = ImVec4(0.35f, 0.72f, 0.96f, 1.0f); break;
        case KeyCategory::Weapons:    c = ImVec4(0.95f, 0.42f, 0.38f, 1.0f); break;
        case KeyCategory::RadarRio:   c = ImVec4(0.35f, 0.88f, 0.55f, 1.0f); break;
        case KeyCategory::Views:      c = ImVec4(0.75f, 0.54f, 0.98f, 1.0f); break;
        case KeyCategory::CarrierSys: c = ImVec4(0.96f, 0.78f, 0.32f, 1.0f); break;
        case KeyCategory::NativeHost: c = ImVec4(0.26f, 0.92f, 0.96f, 1.0f); break;
        default:                      c = ImVec4(0.30f, 0.36f, 0.44f, 0.85f); break;
    }
    if (dim) c.w = 0.28f;
    return ImGui::ColorConvertFloat4ToU32(c);
}

const std::vector<KeyDef>& build_f14_keymap() {
    static const std::vector<KeyDef> keys = []() {
        std::vector<KeyDef> k;
        k.reserve(105);

        // --- Row 0: Function row (y = 0.0) ---
        k.push_back({0.0f,  0.0f, 1.0f, 0.92f, "Esc", "Menu",     "",        KeyCategory::CarrierSys, "Esc: In-Flight Main Menu / Resume"});
        k.push_back({1.5f,  0.0f, 1.0f, 0.92f, "F1",  "Cockpit",  "S/A:Wng", KeyCategory::Views,      "F1: Front Cockpit (recenter free-look) | Shift/Alt+F1: Wingman Comms"});
        k.push_back({2.5f,  0.0f, 1.0f, 0.92f, "F2",  "HUD Only", "S/A:Wng", KeyCategory::Views,      "F2: Frontal HUD-Only View | Shift/Alt+F2: Wingman Comms"});
        k.push_back({3.5f,  0.0f, 1.0f, 0.92f, "F3",  "LSO Cam",  "S/A:Wng", KeyCategory::Views,      "F3: Carrier LSO Landing View | Shift/Alt+F3: Wingman Comms"});
        k.push_back({4.5f,  0.0f, 1.0f, 0.92f, "F4",  "Tower",    "S/A:Wng", KeyCategory::Views,      "F4: Carrier Tower View | Shift/Alt+F4: Wingman Comms"});
        k.push_back({5.85f, 0.0f, 1.0f, 0.92f, "F5",  "Chase",    "S/A:Wng", KeyCategory::Views,      "F5: External Chase Camera (Numpad orbits) | Shift/Alt+F5: Wingman"});
        k.push_back({6.85f, 0.0f, 1.0f, 0.92f, "F6",  "Pilot",    "S/A:Wng", KeyCategory::Views,      "F6: Pilot Head Free View | Shift/Alt+F6: Wingman Comms"});
        k.push_back({7.85f, 0.0f, 1.0f, 0.92f, "F7",  "Missile",  "S/A:Wng", KeyCategory::Views,      "F7: Missile / Weapon Camera | Shift/Alt+F7: Wingman Comms"});
        k.push_back({8.85f, 0.0f, 1.0f, 0.92f, "F8",  "Padlock",  "S/A:Wng", KeyCategory::Views,      "F8: 3D Unclamped Target Padlock | Shift/Alt+F8: Wingman Comms"});
        k.push_back({10.2f, 0.0f, 1.0f, 0.92f, "F9",  "Tac 1",    "S/A:Wng", KeyCategory::Views,      "F9: Tactical Camera 1 | Shift/Alt+F9: Wingman Comms"});
        k.push_back({11.2f, 0.0f, 1.0f, 0.92f, "F10", "Tac 2",    "S/A:Wng", KeyCategory::Views,      "F10: Tactical Camera 2 | Shift/Alt+F10: Wingman Comms"});
        k.push_back({12.2f, 0.0f, 1.0f, 0.92f, "F11", "Render",   "Native",  KeyCategory::NativeHost, "F11 [Native]: Cycle Render Mode (Classic VGA / Modern 3D / Hybrid Cockpit / VR Stereo)"});
        k.push_back({13.2f, 0.0f, 1.0f, 0.92f, "F12", "Overlay",  "Native",  KeyCategory::NativeHost, "F12 [Native]: Toggle Native Settings, HOTAS Mapper, Audio, Flight Model & Quick Keys"});

        // --- Row 1: Number row (y = 1.15) ---
        float y = 1.15f;
        k.push_back({0.0f,  y, 1.0f, 1.0f, "`",    "",        "",        KeyCategory::All,       "` (Backtick): Unbound"});
        k.push_back({1.0f,  y, 1.0f, 1.0f, "1",    "M61 Gun", "Wng #1",  KeyCategory::Weapons,   "1: Select 20mm M61 Vulcan Cannon (or Wingman Menu Option 1)"});
        k.push_back({2.0f,  y, 1.0f, 1.0f, "2",    "AIM-9",   "Wng #2",  KeyCategory::Weapons,   "2: Select AIM-9 Sidewinder SRM (or Wingman Menu Option 2)"});
        k.push_back({3.0f,  y, 1.0f, 1.0f, "3",    "AIM-7",   "Wng #3",  KeyCategory::Weapons,   "3: Select AIM-7 Sparrow MRM (or Wingman Menu Option 3)"});
        k.push_back({4.0f,  y, 1.0f, 1.0f, "4",    "AIM-54",  "Wng #4",  KeyCategory::Weapons,   "4: Select AIM-54 Phoenix LRM (or Wingman Menu Option 4)"});
        k.push_back({5.0f,  y, 1.0f, 1.0f, "5",    "NAV",     "Wng #5",  KeyCategory::Weapons,   "5: Select Master NAV Mode (or Wingman Menu Option 5)"});
        k.push_back({6.0f,  y, 1.0f, 1.0f, "6",    "Wng #6",  "",        KeyCategory::CarrierSys,"6: Wingman Command Menu Option 6"});
        k.push_back({7.0f,  y, 1.0f, 1.0f, "7",    "Wng #7",  "",        KeyCategory::CarrierSys,"7: Wingman Command Menu Option 7"});
        k.push_back({8.0f,  y, 1.0f, 1.0f, "8",    "Wng #8",  "",        KeyCategory::CarrierSys,"8: Wingman Command Menu Option 8"});
        k.push_back({9.0f,  y, 1.0f, 1.0f, "9",    "Wng #9",  "",        KeyCategory::CarrierSys,"9: Wingman Command Menu Option 9"});
        k.push_back({10.0f, y, 1.0f, 1.0f, "0",    "Wng #0",  "",        KeyCategory::CarrierSys,"0: Wingman Command Menu Option 10"});
        k.push_back({11.0f, y, 1.0f, 1.0f, "-",    "Thr -",   "S:Idle",  KeyCategory::Flight,    "-: Decrease Throttle | Shift+-: Idle (0%) | Alt+- (Training): Time Earlier"});
        k.push_back({12.0f, y, 1.0f, 1.0f, "=",    "Thr +",   "S:Mil",   KeyCategory::Flight,    "=: Increase Throttle | Shift+=: Military Power (100%) | Alt+= (Training): Time Later"});
        k.push_back({13.0f, y, 2.0f, 1.0f, "Bksp", "Rdr Acq", "AWG-9",   KeyCategory::RadarRio,  "Backspace: AWG-9 Radar Target Acquisition / Cycle Target"});

        // --- Row 2: QWERTY (y = 2.15) ---
        y = 2.15f;
        k.push_back({0.0f,  y, 1.5f, 1.0f, "Tab",  "",        "",        KeyCategory::All,       "Tab: Unbound"});
        k.push_back({1.5f,  y, 1.0f, 1.0f, "Q",    "A:Quit",  "C:Azim",  KeyCategory::CarrierSys,"Alt+Q: End Flight / Return to Debrief | Ctrl+Q: AWG-9 Scan Azimuth"});
        k.push_back({2.5f,  y, 1.0f, 1.0f, "W",    "S:Wx",    "",        KeyCategory::CarrierSys,"Shift+W: Cycle Weather / Cloud Deck"});
        k.push_back({3.5f,  y, 1.0f, 1.0f, "E",    "EasyLnd", "S:EJECT", KeyCategory::CarrierSys,"E: Easy Carrier Landing | Shift+E: EJECT! | Ctrl+E: AWG-9 Scan Bars"});
        k.push_back({4.5f,  y, 1.0f, 1.0f, "R",    "Rdr Pwr", "C:Elv+",  KeyCategory::RadarRio,  "R: AWG-9 Radar Power | Shift+R: Radar Repeat | Ctrl+R: Antenna Elv+ | Alt+R (Trn): Rearm/Refuel"});
        k.push_back({5.5f,  y, 1.0f, 1.0f, "T",    "1x Time", "S:Accel", KeyCategory::CarrierSys,"T: Reset Time Accel (1x) | Shift+T: Increase Time Accel | Alt+T: Toggle Training Mode"});
        k.push_back({6.5f,  y, 1.0f, 1.0f, "Y",    "",        "",        KeyCategory::All,       "Y: Unbound"});
        k.push_back({7.5f,  y, 1.0f, 1.0f, "U",    "",        "",        KeyCategory::All,       "U: Unbound"});
        k.push_back({8.5f,  y, 1.0f, 1.0f, "I",    "IFF",     "Interr",  KeyCategory::RadarRio,  "I: AWG-9 IFF Interrogate (Friend / Foe Check)"});
        k.push_back({9.5f,  y, 1.0f, 1.0f, "O",    "",        "",        KeyCategory::All,       "O: Unbound"});
        k.push_back({10.5f, y, 1.0f, 1.0f, "P",    "AutoPlt", "A:Pause", KeyCategory::Flight,    "P: Toggle Autopilot | Alt+P: Pause Flight | Shift+P: AWACS Picture"});
        k.push_back({11.5f, y, 1.0f, 1.0f, "[",    "Trim -",  "S:Auto",  KeyCategory::Flight,    "[: Pitch Trim Down | Shift+[: Toggle Auto-Trim"});
        k.push_back({12.5f, y, 1.0f, 1.0f, "]",    "Trim +",  "S:Auto",  KeyCategory::Flight,    "]: Pitch Trim Up | Shift+]: Toggle Auto-Trim"});
        k.push_back({13.5f, y, 1.5f, 1.0f, "\\",   "",        "",        KeyCategory::All,       "\\: Unbound"});

        // --- Row 3: ASDF (y = 3.15) ---
        y = 3.15f;
        k.push_back({0.0f,  y, 1.75f, 1.0f, "Caps", "",       "",        KeyCategory::All,       "Caps Lock: Unused"});
        k.push_back({1.75f, y, 1.0f,  1.0f, "A",    "A/B Max","C:Mode",  KeyCategory::Flight,    "A: Full Afterburner (144%) | Ctrl+A: Cycle AWG-9 Search Mode (PDS/TWS/RWS)"});
        k.push_back({2.75f, y, 1.0f,  1.0f, "S",    "Next WP","S:Prev",  KeyCategory::CarrierSys,"S: Next Waypoint | Shift+S: Previous Waypoint"});
        k.push_back({3.75f, y, 1.0f,  1.0f, "D",    "Declut", "C:Spec",  KeyCategory::RadarRio,  "D: HUD Declutter | Ctrl+D: AWG-9 Special ACM Modes (PAL/VSL/PLM)"});
        k.push_back({4.75f, y, 1.0f,  1.0f, "F",    "Flare",  "C:Elv-",  KeyCategory::Weapons,   "F: Dispense IR Flare | Ctrl+F: AWG-9 Antenna Elv- | Alt+F: FPS Info"});
        k.push_back({5.75f, y, 1.0f,  1.0f, "G",    "Gear",   "",        KeyCategory::CarrierSys,"G: Toggle Landing Gear (below 350 KIAS)"});
        k.push_back({6.75f, y, 1.0f,  1.0f, "H",    "Hook",   "S/C:HUD", KeyCategory::CarrierSys,"H: Arresting Tailhook Up/Down | Shift+H / Ctrl+H: Cycle HUD Color"});
        k.push_back({7.75f, y, 1.0f,  1.0f, "J",    "Jammer", "S:Jett",  KeyCategory::Weapons,   "J: Toggle ECM Jammer | Shift+J: Jettison All External Stores"});
        k.push_back({8.75f, y, 1.0f,  1.0f, "K",    "BrkLck", "",        KeyCategory::RadarRio,  "K: Break AWG-9 Radar Lock"});
        k.push_back({9.75f, y, 1.0f,  1.0f, "L",    "Lock",   "C:Cat!",  KeyCategory::RadarRio,  "L: AWG-9 Radar Lock | Ctrl+L: Carrier Catapult Launch | Shift+L: Lights"});
        k.push_back({10.75f,y, 1.0f,  1.0f, ";",    "",       "",        KeyCategory::All,       ";: Unbound"});
        k.push_back({11.75f,y, 1.0f,  1.0f, "'",    "RIO/Plt","",        KeyCategory::Views,     "' (Single Quote): Switch Between Pilot Front Seat & RIO Back Seat"});
        k.push_back({12.75f,y, 2.25f, 1.0f, "Enter","FireGun","A:FullS", KeyCategory::Weapons,   "Enter: Fire M61 Vulcan Cannon (Master Arm ON) | Alt+Enter [Native]: Borderless Fullscreen"});

        // --- Row 4: ZXCV (y = 4.15) ---
        y = 4.15f;
        k.push_back({0.0f,  y, 2.25f, 1.0f, "Shift","Shift",  "",        KeyCategory::All,       "Shift: Modifier (Shift+E Eject, Shift+J Jettison, Shift+F1..F10 Wingman Orders)"});
        k.push_back({2.25f, y, 1.0f,  1.0f, "Z",    "Zoom +", "",        KeyCategory::Views,     "Z: Zoom In (External Camera / RIO Map)"});
        k.push_back({3.25f, y, 1.0f,  1.0f, "X",    "Zoom -", "",        KeyCategory::Views,     "X: Zoom Out (External Camera / RIO Map)"});
        k.push_back({4.25f, y, 1.0f,  1.0f, "C",    "Chaff",  "",        KeyCategory::Weapons,   "C: Dispense Radar Chaff"});
        k.push_back({5.25f, y, 1.0f,  1.0f, "V",    "VDI",    "A:Vol",   KeyCategory::CarrierSys,"V: Toggle VDI Display Mode | Alt+V: Sound Volume"});
        k.push_back({6.25f, y, 1.0f,  1.0f, "B",    "AirBrk", "",        KeyCategory::Flight,    "B: Toggle Speedbrake / Airbrake"});
        k.push_back({7.25f, y, 1.0f,  1.0f, "N",    "",       "",        KeyCategory::All,       "N: Unbound"});
        k.push_back({8.25f, y, 1.0f,  1.0f, "M",    "MstArm", "A:Mem",   KeyCategory::Weapons,   "M: Toggle Master Arm Switch (Required to Fire Weapons) | Alt+M: Memory Info"});
        k.push_back({9.25f, y, 1.0f,  1.0f, ",",    "Rud L",  "",        KeyCategory::Flight,    ", (Comma): Rudder Left"});
        k.push_back({10.25f,y, 1.0f,  1.0f, ".",    "Rud R",  "",        KeyCategory::Flight,    ". (Period): Rudder Right"});
        k.push_back({11.25f,y, 1.0f,  1.0f, "/",    "RIOMap", "",        KeyCategory::RadarRio,  "/: Open / Close RIO Tactical Map"});
        k.push_back({12.25f,y, 2.75f, 1.0f, "Shift","Shift",  "",        KeyCategory::All,       "Right Shift: Modifier Key"});

        // --- Row 5: Bottom row (y = 5.15) ---
        y = 5.15f;
        k.push_back({0.0f,  y, 1.5f, 1.0f, "Ctrl",  "Ctrl",        "", KeyCategory::All,     "Ctrl: Modifier (Ctrl+L Catapult Launch, Ctrl+Q/A/E/D/R/F AWG-9 Controls)"});
        k.push_back({1.5f,  y, 1.25f,1.0f, "Win",   "",            "", KeyCategory::All,     "Windows Key"});
        k.push_back({2.75f, y, 1.5f, 1.0f, "Alt",   "Alt",         "", KeyCategory::All,     "Alt: Modifier (Alt+P Pause, Alt+Q Quit Flight, Alt+Enter Fullscreen, Alt+F1..F10 Wingman)"});
        k.push_back({4.25f, y, 6.5f, 1.0f, "Space", "FIRE MISSILE","", KeyCategory::Weapons, "Spacebar: Launch Selected Missile (AIM-9 / AIM-7 / AIM-54, Master Arm ON)"});
        k.push_back({10.75f,y, 1.5f, 1.0f, "Alt",   "Alt",         "", KeyCategory::All,     "Right Alt: Modifier Key"});
        k.push_back({12.25f,y, 1.25f,1.0f, "Menu",  "",            "", KeyCategory::All,     "Menu Key"});
        k.push_back({13.5f, y, 1.5f, 1.0f, "Ctrl",  "Ctrl",        "", KeyCategory::All,     "Right Ctrl: Modifier Key"});

        // --- Nav Cluster (x = 15.35, 3 cols) ---
        float nx = 15.35f;
        k.push_back({nx + 0.0f, 1.15f, 1.0f, 1.0f, "Ins",  "Azim",  "C+Q", KeyCategory::RadarRio, "Insert (or Ctrl+Q): AWG-9 Scan Azimuth Width"});
        k.push_back({nx + 1.0f, 1.15f, 1.0f, 1.0f, "Home", "Bars",  "C+E", KeyCategory::RadarRio, "Home (or Ctrl+E): AWG-9 Elevation Scan Bars"});
        k.push_back({nx + 2.0f, 1.15f, 1.0f, 1.0f, "PgUp", "Elv +", "C+R", KeyCategory::RadarRio, "Page Up (or Ctrl+R): Tilt AWG-9 Radar Antenna Up"});
        k.push_back({nx + 0.0f, 2.15f, 1.0f, 1.0f, "Del",  "Mode",  "C+A", KeyCategory::RadarRio, "Delete (or Ctrl+A): Cycle AWG-9 Search Mode (PDS / TWS-M / TWS-A / RWS)"});
        k.push_back({nx + 1.0f, 2.15f, 1.0f, 1.0f, "End",  "ACM",   "C+D", KeyCategory::RadarRio, "End (or Ctrl+D): AWG-9 Special ACM Dogfight Modes"});
        k.push_back({nx + 2.0f, 2.15f, 1.0f, 1.0f, "PgDn", "Elv -", "C+F", KeyCategory::RadarRio, "Page Down (or Ctrl+F): Tilt AWG-9 Radar Antenna Down"});

        // --- Arrow Cluster (Virtual Flight Stick) ---
        k.push_back({nx + 1.0f, 4.15f, 1.0f, 1.0f, "Up",   "NoseDn","", KeyCategory::Flight, "Up Arrow: Pitch Nose Down (Native Virtual Keyboard Stick)"});
        k.push_back({nx + 0.0f, 5.15f, 1.0f, 1.0f, "Left", "Roll L","", KeyCategory::Flight, "Left Arrow: Roll Left (Native Virtual Keyboard Stick)"});
        k.push_back({nx + 1.0f, 5.15f, 1.0f, 1.0f, "Down", "NoseUp","", KeyCategory::Flight, "Down Arrow: Pitch Nose Up (Native Virtual Keyboard Stick)"});
        k.push_back({nx + 2.0f, 5.15f, 1.0f, 1.0f, "Right","Roll R","", KeyCategory::Flight, "Right Arrow: Roll Right (Native Virtual Keyboard Stick)"});

        // --- Numpad (x = 18.70, 4 cols) ---
        float px = 18.70f;
        k.push_back({px + 0.0f, 1.15f, 1.0f, 1.0f, "Num",  "",       "", KeyCategory::All,   "Num Lock"});
        k.push_back({px + 1.0f, 1.15f, 1.0f, 1.0f, "/",    "",       "", KeyCategory::All,   "Numpad /"});
        k.push_back({px + 2.0f, 1.15f, 1.0f, 1.0f, "*",    "",       "", KeyCategory::All,   "Numpad *"});
        k.push_back({px + 3.0f, 1.15f, 1.0f, 1.0f, "-",    "Thr -",  "", KeyCategory::Flight,"Numpad -: Decrease Throttle"});

        k.push_back({px + 0.0f, 2.15f, 1.0f, 1.0f, "7",    "",       "", KeyCategory::Views, "Numpad 7"});
        k.push_back({px + 1.0f, 2.15f, 1.0f, 1.0f, "8",    "Lk Fwd", "", KeyCategory::Views, "Numpad 8: Look Forward / Up (or Orbit Camera Up)"});
        k.push_back({px + 2.0f, 2.15f, 1.0f, 1.0f, "9",    "",       "", KeyCategory::Views, "Numpad 9"});
        k.push_back({px + 3.0f, 2.15f, 1.0f, 2.0f, "+",    "Thr +",  "", KeyCategory::Flight,"Numpad +: Increase Throttle"});

        k.push_back({px + 0.0f, 3.15f, 1.0f, 1.0f, "4",    "Lk Lft", "", KeyCategory::Views, "Numpad 4: Look Left Wing (or Orbit Camera Left)"});
        k.push_back({px + 1.0f, 3.15f, 1.0f, 1.0f, "5",    "Center", "", KeyCategory::Flight,"Numpad 5: Center Virtual Stick & Forward Cockpit View"});
        k.push_back({px + 2.0f, 3.15f, 1.0f, 1.0f, "6",    "Lk Rgt", "", KeyCategory::Views, "Numpad 6: Look Right Wing (or Orbit Camera Right)"});

        k.push_back({px + 0.0f, 4.15f, 1.0f, 1.0f, "1",    "Rear L", "", KeyCategory::Views, "Numpad 1: Look Rear Left (Check 6)"});
        k.push_back({px + 1.0f, 4.15f, 1.0f, 1.0f, "2",    "Lk Dwn", "", KeyCategory::Views, "Numpad 2: Look Down at Instruments / RIO TID"});
        k.push_back({px + 2.0f, 4.15f, 1.0f, 1.0f, "3",    "Rear R", "", KeyCategory::Views, "Numpad 3: Look Rear Right (Check 6)"});
        k.push_back({px + 3.0f, 4.15f, 1.0f, 2.0f, "Ent",  "Gun",    "", KeyCategory::Weapons,"Numpad Enter: Fire M61 Vulcan Cannon"});

        k.push_back({px + 0.0f, 5.15f, 2.0f, 1.0f, "0",    "",       "", KeyCategory::All,   "Numpad 0"});
        k.push_back({px + 2.0f, 5.15f, 1.0f, 1.0f, ".",    "",       "", KeyCategory::All,   "Numpad ."});

        return k;
    }();
    return keys;
}

} // namespace

void draw_quick_keys_panel(QuickKeysState& state, const std::string& external_chart_path) {
    // Category filter pills
    static const char* kCatNames[] = {
        "All Keys", "Flight", "Weapons", "AWG-9 / RIO", "Views", "Carrier / Sys", "Native Host"
    };
    static const ImVec4 kCatCols[] = {
        ImVec4(0.32f, 0.42f, 0.56f, 1.0f),
        ImVec4(0.18f, 0.52f, 0.78f, 1.0f),
        ImVec4(0.76f, 0.24f, 0.24f, 1.0f),
        ImVec4(0.18f, 0.62f, 0.38f, 1.0f),
        ImVec4(0.54f, 0.32f, 0.76f, 1.0f),
        ImVec4(0.76f, 0.56f, 0.16f, 1.0f),
        ImVec4(0.12f, 0.68f, 0.76f, 1.0f)
    };

    for (int i = 0; i < static_cast<int>(KeyCategory::Count); ++i) {
        if (i > 0) ImGui::SameLine();
        bool active = (static_cast<int>(state.filter) == i);
        ImVec4 col = kCatCols[i];
        if (!active) {
            col.x *= 0.42f; col.y *= 0.42f; col.z *= 0.42f;
        }
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kCatCols[i]);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kCatCols[i]);
        if (ImGui::SmallButton(kCatNames[i])) {
            state.filter = static_cast<KeyCategory>(i);
        }
        ImGui::PopStyleColor(3);
    }

    if (!external_chart_path.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Open Vector SVG")) {
            open_shell_path(external_chart_path);
        }
    }

    // Dynamic resolution-independent vector canvas sizing
    const float total_units_w = 22.72f;
    const float total_units_h = 6.22f;
    float avail_w = std::max(420.0f, ImGui::GetContentRegionAvail().x);
    float u = std::clamp(avail_w / total_units_w, 20.0f, 88.0f);
    float kb_w = u * total_units_w;
    float kb_h = u * total_units_h;
    float gap = std::max(2.0f, u * 0.085f);
    float rounding = std::max(3.0f, u * 0.12f);

    // Center horizontally if window is very wide
    float x_pad = std::max(0.0f, (ImGui::GetContentRegionAvail().x - kb_w) * 0.5f);
    if (x_pad > 1.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x_pad);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##f14_vector_kb", ImVec2(kb_w, kb_h));
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool canvas_hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();

    // Subtle chassis backdrop
    dl->AddRectFilled(ImVec2(origin.x - 4.0f, origin.y - 4.0f),
                      ImVec2(origin.x + kb_w + 4.0f, origin.y + kb_h + 4.0f),
                      IM_COL32(10, 15, 24, 235), rounding + 3.0f);
    dl->AddRect(ImVec2(origin.x - 4.0f, origin.y - 4.0f),
                ImVec2(origin.x + kb_w + 4.0f, origin.y + kb_h + 4.0f),
                IM_COL32(55, 84, 118, 170), rounding + 3.0f, 0, 1.25f);

    const auto& keys = build_f14_keymap();
    state.hovered_index = -1;

    float cap_fs  = std::clamp(u * 0.27f, 10.0f, 24.0f);
    float lbl_fs  = std::clamp(u * 0.215f, 8.5f, 18.0f);
    float sub_fs  = std::clamp(u * 0.185f, 7.5f, 15.0f);

    for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
        const auto& kd = keys[i];
        ImVec2 p0(origin.x + kd.x * u + gap * 0.5f, origin.y + kd.y * u + gap * 0.5f);
        ImVec2 p1(origin.x + (kd.x + kd.w) * u - gap * 0.5f, origin.y + (kd.y + kd.h) * u - gap * 0.5f);

        bool hov = canvas_hovered && mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y && mouse.y <= p1.y;
        if (hov) state.hovered_index = i;

        bool dim = (state.filter != KeyCategory::All) && (kd.cat != state.filter);

        // Drop shadow
        dl->AddRectFilled(ImVec2(p0.x + 1.5f, p0.y + 2.5f), ImVec2(p1.x + 1.5f, p1.y + 2.5f),
                          IM_COL32(0, 0, 0, 135), rounding);
        // Keycap body
        dl->AddRectFilled(p0, p1, cat_fill_col(kd.cat, dim, hov), rounding);
        // Subtle top bevel highlight
        dl->AddLine(ImVec2(p0.x + rounding * 0.6f, p0.y + 1.0f),
                    ImVec2(p1.x - rounding * 0.6f, p0.y + 1.0f),
                    IM_COL32(255, 255, 255, dim ? 18 : 55), 1.0f);
        // Border
        dl->AddRect(p0, p1, cat_border_col(kd.cat, dim, hov), rounding, 0, hov ? 2.0f : 1.15f);

        // Clip inside keycap so labels never bleed out even on narrow windows
        dl->PushClipRect(ImVec2(p0.x + 2.0f, p0.y + 1.0f), ImVec2(p1.x - 2.0f, p1.y - 1.0f), true);

        ImU32 cap_col = dim ? IM_COL32(160, 170, 185, 110) : IM_COL32(246, 250, 255, 255);
        ImU32 lbl_col = dim ? IM_COL32(140, 155, 175, 95)  : IM_COL32(215, 238, 255, 240);
        ImU32 sub_col = dim ? IM_COL32(130, 140, 150, 80)  : IM_COL32(255, 214, 130, 225);

        float pad_x = std::max(3.0f, u * 0.08f);
        float pad_y = std::max(2.0f, u * 0.06f);
        dl->AddText(font, cap_fs, ImVec2(p0.x + pad_x, p0.y + pad_y), cap_col, kd.cap);

        if (kd.short1 && kd.short1[0]) {
            float y1 = p0.y + pad_y + cap_fs + 1.0f;
            dl->AddText(font, lbl_fs, ImVec2(p0.x + pad_x, y1), lbl_col, kd.short1);
        }
        if (kd.short2 && kd.short2[0] && u >= 30.0f) {
            float y2 = p1.y - sub_fs - pad_y;
            if (y2 > p0.y + pad_y + cap_fs + lbl_fs * 0.85f) {
                dl->AddText(font, sub_fs, ImVec2(p0.x + pad_x, y2), sub_col, kd.short2);
            }
        }
        dl->PopClipRect();
    }

    // Interactive hover detail strip
    ImGui::Spacing();
    if (state.hovered_index >= 0 && state.hovered_index < static_cast<int>(keys.size())) {
        const auto& hk = keys[state.hovered_index];
        ImGui::TextColored(ImVec4(1.0f, 0.88f, 0.45f, 1.0f), "[%s]", hk.cap);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", hk.detail);
    } else {
        ImGui::TextColored(ImVec4(0.68f, 0.80f, 0.92f, 1.0f),
                           "Hover any key for Primary + Shift/Ctrl/Alt details  |  Mouse: Right-Drag = 360 Free-Look, Mid-Click/F1 = Recenter, Left-Click = Cockpit Switches / RIO DDD & TID");
    }
}

} // namespace f14

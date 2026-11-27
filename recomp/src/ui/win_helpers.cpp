#include "ui/win_helpers.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#endif

namespace fs = std::filesystem;

namespace f14 {

namespace {

bool is_f14_dir(const fs::path& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return false;
    return fs::exists(dir / "F14.EXE", ec) || fs::exists(dir / "F14.COM", ec);
}

std::string resolve_f14_candidate(const fs::path& base) {
    std::error_code ec;
    if (is_f14_dir(base)) return fs::absolute(base, ec).string();
    if (is_f14_dir(base / "game_data")) return fs::absolute(base / "game_data", ec).string();
    return "";
}

bool has_mt32_roms(const fs::path& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return false;
    bool ctrl = fs::exists(dir / "CM32L_CONTROL.ROM", ec) || fs::exists(dir / "cm32l_control.rom", ec) ||
                fs::exists(dir / "MT32_CONTROL.ROM", ec)  || fs::exists(dir / "mt32_control.rom", ec);
    bool pcm  = fs::exists(dir / "CM32L_PCM.ROM", ec)     || fs::exists(dir / "cm32l_pcm.rom", ec) ||
                fs::exists(dir / "MT32_PCM.ROM", ec)      || fs::exists(dir / "mt32_pcm.rom", ec);
    return ctrl && pcm;
}

std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

#ifdef _WIN32
std::string trim_slashes(std::string s) {
    while (!s.empty() && (s.back() == '\\' || s.back() == '/' || s.back() == ' ' || s.back() == '"')) {
        s.pop_back();
    }
    while (!s.empty() && (s.front() == ' ' || s.front() == '"')) {
        s.erase(s.begin());
    }
    return s;
}

std::string query_reg_string(HKEY root, const char* subkey, REGSAM sam, const char* val_name) {
    HKEY hk = nullptr;
    if (RegOpenKeyExA(root, subkey, 0, KEY_READ | sam, &hk) != ERROR_SUCCESS) return "";
    char buf[MAX_PATH * 2]{};
    DWORD cb = sizeof(buf) - 1;
    DWORD type = 0;
    std::string out;
    if (RegQueryValueExA(hk, val_name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && buf[0]) {
        out = trim_slashes(buf);
    }
    RegCloseKey(hk);
    return out;
}

std::string scan_gog_registry_enum(HKEY root, const char* games_key, REGSAM sam) {
    HKEY hk = nullptr;
    if (RegOpenKeyExA(root, games_key, 0, KEY_READ | sam, &hk) != ERROR_SUCCESS) return "";
    char sub[256];
    for (DWORD idx = 0; ; ++idx) {
        DWORD sub_len = sizeof(sub);
        if (RegEnumKeyExA(hk, idx, sub, &sub_len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        std::string full = std::string(games_key) + "\\" + sub;
        std::string gname = to_lower_copy(query_reg_string(root, full.c_str(), sam, "gameName"));
        std::string p = query_reg_string(root, full.c_str(), sam, "path");
        std::string pl = to_lower_copy(p);
        if (gname.find("fleet defender") != std::string::npos || pl.find("fleet defender") != std::string::npos) {
            std::string r = resolve_f14_candidate(p);
            if (!r.empty()) {
                RegCloseKey(hk);
                return r;
            }
        }
    }
    RegCloseKey(hk);
    return "";
}

std::string scan_steam_libraries() {
    std::vector<std::string> roots;
    const REGSAM sams[2] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
    for (REGSAM sam : sams) {
        std::string sp = query_reg_string(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam", sam, "SteamPath");
        if (!sp.empty()) roots.push_back(sp);
        std::string ip = query_reg_string(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", sam, "InstallPath");
        if (!ip.empty()) roots.push_back(ip);
    }
    std::vector<fs::path> libs;
    for (const auto& sr : roots) {
        libs.push_back(fs::path(sr));
        std::ifstream vdf(fs::path(sr) / "steamapps" / "libraryfolders.vdf");
        std::string line;
        while (std::getline(vdf, line)) {
            auto pos = line.find("\"path\"");
            if (pos == std::string::npos) continue;
            auto q1 = line.find('"', pos + 6);
            if (q1 == std::string::npos) continue;
            auto q2 = line.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            std::string lp = line.substr(q1 + 1, q2 - q1 - 1);
            std::string unesc;
            for (size_t i = 0; i < lp.size(); ++i) {
                if (lp[i] == '\\' && i + 1 < lp.size() && lp[i + 1] == '\\') {
                    unesc.push_back('\\');
                    ++i;
                } else {
                    unesc.push_back(lp[i]);
                }
            }
            if (!unesc.empty()) libs.push_back(fs::path(unesc));
        }
    }
    for (const auto& lib : libs) {
        fs::path common = lib / "steamapps" / "common";
        std::error_code ec;
        if (!fs::exists(common, ec) || !fs::is_directory(common, ec)) continue;
        for (const auto& ent : fs::directory_iterator(common, ec)) {
            if (!ent.is_directory(ec)) continue;
            std::string name = to_lower_copy(ent.path().filename().string());
            if (name.find("fleet defender") != std::string::npos || name.find("f-14") != std::string::npos) {
                std::string r = resolve_f14_candidate(ent.path());
                if (!r.empty()) return r;
            }
        }
    }
    return "";
}
#endif

} // namespace

bool open_shell_path(const std::string& path) {
    if (path.empty()) return false;
#ifdef _WIN32
    HINSTANCE res = ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(res) > 32;
#else
    (void)path;
    return false;
#endif
}

std::string browse_for_folder(const std::string& title) {
#ifdef _WIN32
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BROWSEINFOA bi{};
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    std::string result;
    if (pidl) {
        char buf[MAX_PATH]{};
        if (SHGetPathFromIDListA(pidl, buf)) {
            result = buf;
        }
        CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    (void)title;
    return "";
#endif
}

std::string autodetect_f14_install_dir() {
    // 1. Same directory as executable, CWD, and local game_data/ subfolders
#ifdef _WIN32
    {
        char exe_buf[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, exe_buf, MAX_PATH) > 0) {
            fs::path exe_dir = fs::path(exe_buf).parent_path();
            for (const fs::path& cand : {exe_dir, exe_dir / "game_data", exe_dir.parent_path() / "game_data",
                                         exe_dir.parent_path().parent_path() / "game_data"}) {
                std::string r = resolve_f14_candidate(cand);
                if (!r.empty()) return r;
            }
        }
    }
#endif
    for (const char* c : {".", "game_data", "../game_data", "../../game_data", ".."}) {
        std::string r = resolve_f14_candidate(c);
        if (!r.empty()) return r;
    }

#ifdef _WIN32
    // 2. Windows Registry: GOG.com Fleet Defender Gold (ID 1708998465), Inno Setup, MicroProse
    struct RegProbe { HKEY root; const char* key; const char* val; };
    static const RegProbe kProbes[] = {
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1708998465", "path"},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1708998465", "workingDir"},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games\\1708998465", "path"},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games\\1708998465", "workingDir"},
        {HKEY_CURRENT_USER,  "SOFTWARE\\GOG.com\\Games\\1708998465", "path"},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1708998465_is1", "InstallLocation"},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1708998465_is1", "Inno Setup: App Path"},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1708998465_is1", "InstallLocation"},
        {HKEY_CURRENT_USER,  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1708998465_is1", "InstallLocation"},
    };
    const REGSAM sams[2] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
    for (const auto& p : kProbes) {
        for (REGSAM sam : sams) {
            std::string val = query_reg_string(p.root, p.key, sam, p.val);
            if (!val.empty()) {
                std::string r = resolve_f14_candidate(val);
                if (!r.empty()) return r;
            }
        }
    }

    // Enumerate all GOG.com\Games\* entries for any Fleet Defender variant
    for (REGSAM sam : sams) {
        for (const char* k : {"SOFTWARE\\GOG.com\\Games", "SOFTWARE\\WOW6432Node\\GOG.com\\Games"}) {
            std::string r = scan_gog_registry_enum(HKEY_LOCAL_MACHINE, k, sam);
            if (!r.empty()) return r;
            r = scan_gog_registry_enum(HKEY_CURRENT_USER, k, sam);
            if (!r.empty()) return r;
        }
    }

    // Steam libraries
    {
        std::string r = scan_steam_libraries();
        if (!r.empty()) return r;
    }

    // 3. Common GOG / Steam / Retail paths across drives C: through H:
    static const char* kRelPaths[] = {
        "GOG Games\\Fleet Defender Gold",
        "GOG Games\\Fleet Defender",
        "Program Files (x86)\\GOG Galaxy\\Games\\Fleet Defender Gold",
        "Program Files\\GOG Galaxy\\Games\\Fleet Defender Gold",
        "Games\\Fleet Defender Gold",
        "Games\\GOG\\Fleet Defender Gold",
        "Program Files (x86)\\Steam\\steamapps\\common\\Fleet Defender",
        "MPS\\F14",
        "MicroProse\\Fleet Defender Gold",
        "MicroProse\\Fleet Defender",
    };
    for (char drv = 'C'; drv <= 'H'; ++drv) {
        for (const char* rel : kRelPaths) {
            std::string cand = std::string(1, drv) + ":\\" + rel;
            std::string r = resolve_f14_candidate(cand);
            if (!r.empty()) return r;
        }
    }
#endif
    return "";
}

std::string autodetect_rom_dir() {
    if (const char* e1 = std::getenv("MT32_ROM_DIR"); e1 && e1[0] && has_mt32_roms(e1)) return e1;
    if (const char* e2 = std::getenv("CM32L_ROM_DIR"); e2 && e2[0] && has_mt32_roms(e2)) return e2;

    std::error_code ec;
#ifdef _WIN32
    char exe_buf[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, exe_buf, MAX_PATH) > 0) {
        fs::path exe_dir = fs::path(exe_buf).parent_path();
        for (const fs::path& c : {exe_dir / "roms", exe_dir / "cm32l", exe_dir.parent_path() / "roms",
                                  exe_dir.parent_path().parent_path() / "roms"}) {
            if (has_mt32_roms(c)) return fs::absolute(c, ec).string();
        }
    }
#endif
    for (const char* c : {"roms", "cm32l", "../roms", "../../roms", R"(C:\munt\roms)", R"(C:\mt32-roms)"}) {
        if (has_mt32_roms(c)) return fs::absolute(c, ec).string();
    }
    if (const char* appdata = std::getenv("APPDATA"); appdata && appdata[0]) {
        fs::path p = fs::path(appdata) / "munt" / "roms";
        if (has_mt32_roms(p)) return p.string();
    }
    return "roms";
}

std::vector<ManualEntry> discover_local_manuals(const std::string& game_dir) {
    std::vector<ManualEntry> out;
    if (game_dir.empty()) return out;
    std::error_code ec;
    fs::path base(game_dir);
    if (!fs::exists(base, ec) || !fs::is_directory(base, ec)) return out;

    struct KnownDoc { const char* file; const char* label; };
    static const KnownDoc kDocs[] = {
        {"Manual.pdf",              "Flight Manual"},
        {"RefCard.pdf",             "Reference Card"},
        {"BasicFlyingGuide.pdf",    "Basic Flying Guide"},
        {"CampaignNotes.pdf",       "Campaign Notes"},
        {"ScenarioDiskManual.pdf",  "Scenario Manual"},
        {"README.TXT",              "GOG ReadMe"},
    };
    for (const auto& d : kDocs) {
        fs::path p = base / d.file;
        if (fs::exists(p, ec)) {
            out.push_back({d.label, fs::absolute(p, ec).string()});
        }
    }
    return out;
}

} // namespace f14

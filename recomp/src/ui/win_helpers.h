#pragma once

#include <string>
#include <vector>

namespace f14 {

// Opens a file/PDF/URL using the OS default handler (Windows ShellExecuteA).
bool open_shell_path(const std::string& path);

// Opens a native folder picker dialog (Windows SHBrowseForFolderA).
// Returns empty string if cancelled.
std::string browse_for_folder(const std::string& title);

// Auto-detects Fleet Defender Gold installation folder using Registry (GOG 1708998465,
// GOG/Inno enum, Steam libraryfolders.vdf) and common drive paths (C: through H:).
std::string autodetect_f14_install_dir();

// Auto-detects Roland CM-32L / MT-32 ROM folder.
std::string autodetect_rom_dir();

struct ManualEntry {
    std::string label;
    std::string path;
};

// Discovers local PDF/TXT manuals inside the user's game directory.
std::vector<ManualEntry> discover_local_manuals(const std::string& game_dir);

} // namespace f14

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <objbase.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define APP_TITLE       "Fleet Defender Gold - Native Setup"
#define F14_STR_IN(x)   #x
#define F14_STR(x)      F14_STR_IN(x)
#ifdef APP_VERSION_RAW
#define APP_VERSION     F14_STR(APP_VERSION_RAW)
#else
#define APP_VERSION     "v0.1.0"
#endif
#define PAYLOAD_MAGIC   "F14NAT01"

#define IDC_PATH_EDIT   101
#define IDC_BROWSE_BTN  102
#define IDC_STATUS_TXT  103
#define IDC_ROM_EDIT    104
#define IDC_ROM_BROWSE  105
#define IDC_ROM_STATUS  106
#define IDC_CHK_BACKUP  107
#define IDC_CHK_SHORT   108
#define IDC_CHK_FAST    109
#define IDC_INSTALL_BTN 110
#define IDC_UNINST_BTN  111
#define IDC_CLOSE_BTN   112

#pragma pack(push, 1)
typedef struct {
    uint32_t offsetFromPayload;
    uint32_t size;
    uint32_t crc32;
    char     relPath[128];
} PayloadEntry;

typedef struct {
    char     magic[8];
    uint32_t payloadStartOffset;
    uint32_t entryTableOffset;
    uint32_t entryCount;
    uint32_t reserved;
} PayloadTrailer;
#pragma pack(pop)

static HWND  g_hwnd = NULL;
static HFONT g_fontBody = NULL;
static HFONT g_fontBold = NULL;
static HFONT g_fontTitle = NULL;
static char  g_selfPath[MAX_PATH] = {0};
static int   g_autoDetected = 0;

static uint32_t CalcCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

static int FileExistsA(const char* path) {
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int DirExistsA(const char* path) {
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void TrimTrailingSlashes(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\\' || s[n - 1] == '/' || s[n - 1] == ' ' || s[n - 1] == '"')) {
        s[--n] = '\0';
    }
}

static void EnsureParentDirs(const char* filePath) {
    char tmp[MAX_PATH];
    lstrcpynA(tmp, filePath, MAX_PATH);
    for (char* p = tmp + 3; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;
            *p = '\0';
            if (!DirExistsA(tmp)) CreateDirectoryA(tmp, NULL);
            *p = saved;
        }
    }
}

static int HasF14InDir(const char* dir) {
    if (!dir || !dir[0]) return 0;
    char p1[MAX_PATH], p2[MAX_PATH];
    snprintf(p1, sizeof(p1), "%s\\F14.COM", dir);
    snprintf(p2, sizeof(p2), "%s\\F14.EXE", dir);
    return FileExistsA(p1) || FileExistsA(p2);
}

static int ResolveF14Dir(const char* dir, char* outGameDir, size_t cap) {
    if (!dir || !dir[0]) return 0;
    if (HasF14InDir(dir)) {
        if (outGameDir) lstrcpynA(outGameDir, dir, (int)cap);
        return 1;
    }
    char sub[MAX_PATH];
    snprintf(sub, sizeof(sub), "%s\\game_data", dir);
    if (HasF14InDir(sub)) {
        if (outGameDir) lstrcpynA(outGameDir, sub, (int)cap);
        return 1;
    }
    return 0;
}

static int HasMt32Roms(const char* dir) {
    if (!dir || !dir[0] || !DirExistsA(dir)) return 0;
    char c1[MAX_PATH], c2[MAX_PATH], p1[MAX_PATH], p2[MAX_PATH];
    snprintf(c1, sizeof(c1), "%s\\CM32L_CONTROL.ROM", dir);
    snprintf(c2, sizeof(c2), "%s\\MT32_CONTROL.ROM", dir);
    snprintf(p1, sizeof(p1), "%s\\CM32L_PCM.ROM", dir);
    snprintf(p2, sizeof(p2), "%s\\MT32_PCM.ROM", dir);
    return (FileExistsA(c1) || FileExistsA(c2)) && (FileExistsA(p1) || FileExistsA(p2));
}

static int QueryRegString(HKEY root, const char* subkey, REGSAM sam, const char* valName, char* outBuf, size_t cap) {
    HKEY hk = NULL;
    if (RegOpenKeyExA(root, subkey, 0, KEY_READ | sam, &hk) != ERROR_SUCCESS) return 0;
    char buf[MAX_PATH] = {0};
    DWORD cb = sizeof(buf) - 1;
    DWORD type = 0;
    int found = 0;
    if (RegQueryValueExA(hk, valName, NULL, &type, (LPBYTE)buf, &cb) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && buf[0]) {
        TrimTrailingSlashes(buf);
        lstrcpynA(outBuf, buf, (int)cap);
        found = 1;
    }
    RegCloseKey(hk);
    return found;
}

static int ScanGogRegistryEnum(HKEY root, const char* gamesKey, REGSAM sam, char* outDir, size_t cap) {
    HKEY hk = NULL;
    if (RegOpenKeyExA(root, gamesKey, 0, KEY_READ | sam, &hk) != ERROR_SUCCESS) return 0;
    char sub[128];
    for (DWORD idx = 0; ; ++idx) {
        DWORD len = sizeof(sub);
        if (RegEnumKeyExA(hk, idx, sub, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        char full[256];
        snprintf(full, sizeof(full), "%s\\%s", gamesKey, sub);
        char gname[256] = {0}, p[MAX_PATH] = {0};
        QueryRegString(root, full, sam, "gameName", gname, sizeof(gname));
        QueryRegString(root, full, sam, "path", p, sizeof(p));
        if (p[0] && (strstr(gname, "Fleet Defender") || strstr(gname, "FLEET DEFENDER") || strstr(p, "Fleet Defender"))) {
            if (ResolveF14Dir(p, outDir, cap)) {
                RegCloseKey(hk);
                return 1;
            }
        }
    }
    RegCloseKey(hk);
    return 0;
}

static int ScanSteamLibraries(char* outDir, size_t cap) {
    char steamPath[MAX_PATH] = {0};
    const REGSAM sams[2] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    for (int s = 0; s < 2 && !steamPath[0]; ++s) {
        QueryRegString(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam", sams[s], "SteamPath", steamPath, sizeof(steamPath));
        if (!steamPath[0]) {
            QueryRegString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", sams[s], "InstallPath", steamPath, sizeof(steamPath));
        }
    }
    if (!steamPath[0]) return 0;
    for (char* q = steamPath; *q; ++q) if (*q == '/') *q = '\\';

    const char* steamRel[] = {
        "steamapps\\common\\Fleet Defender",
        "steamapps\\common\\Fleet Defender Gold",
        "steamapps\\common\\Fleet Defender The F-14 Tomcat Simulation",
    };
    for (size_t i = 0; i < sizeof(steamRel) / sizeof(steamRel[0]); ++i) {
        char cand[MAX_PATH];
        snprintf(cand, sizeof(cand), "%s\\%s", steamPath, steamRel[i]);
        if (ResolveF14Dir(cand, outDir, cap)) return 1;
    }

    char vdfPath[MAX_PATH];
    snprintf(vdfPath, sizeof(vdfPath), "%s\\steamapps\\libraryfolders.vdf", steamPath);
    FILE* f = fopen(vdfPath, "r");
    if (!f) return 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* p = strstr(line, "\"path\"");
        if (!p) continue;
        char* q1 = strchr(p + 6, '"');
        if (!q1) continue;
        char* q2 = strchr(q1 + 1, '"');
        if (!q2) continue;
        char lib[MAX_PATH] = {0};
        size_t o = 0;
        for (char* s = q1 + 1; s < q2 && o + 1 < MAX_PATH; ++s) {
            if (*s == '\\' && s + 1 < q2 && s[1] == '\\') { lib[o++] = '\\'; ++s; }
            else lib[o++] = *s;
        }
        lib[o] = '\0';
        for (size_t i = 0; i < sizeof(steamRel) / sizeof(steamRel[0]); ++i) {
            char cand[MAX_PATH];
            snprintf(cand, sizeof(cand), "%s\\%s", lib, steamRel[i]);
            if (ResolveF14Dir(cand, outDir, cap)) {
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

static void DetectGameDir(char* outDir, size_t outCap) {
    outDir[0] = '\0';
    g_autoDetected = 0;

    // 1. Same folder as installer (or game_data subfolder)
    char selfDir[MAX_PATH];
    lstrcpynA(selfDir, g_selfPath, MAX_PATH);
    char* slash = strrchr(selfDir, '\\');
    if (slash) {
        *slash = '\0';
        if (ResolveF14Dir(selfDir, outDir, outCap)) {
            g_autoDetected = 1;
            return;
        }
    }

    // 2. GOG.com Fleet Defender Gold (ID 1708998465) & Inno Setup registry keys
    struct { HKEY root; const char* key; const char* val; } regChecks[] = {
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1708998465", "path" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1708998465", "workingDir" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games\\1708998465", "path" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games\\1708998465", "workingDir" },
        { HKEY_CURRENT_USER,  "SOFTWARE\\GOG.com\\Games\\1708998465", "path" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1708998465_is1", "InstallLocation" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1708998465_is1", "Inno Setup: App Path" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1708998465_is1", "InstallLocation" },
        { HKEY_CURRENT_USER,  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1708998465_is1", "InstallLocation" },
    };
    const REGSAM sams[2] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    for (size_t i = 0; i < sizeof(regChecks) / sizeof(regChecks[0]); ++i) {
        for (int s = 0; s < 2; ++s) {
            char tmp[MAX_PATH] = {0};
            if (QueryRegString(regChecks[i].root, regChecks[i].key, sams[s], regChecks[i].val, tmp, sizeof(tmp))) {
                if (ResolveF14Dir(tmp, outDir, outCap)) {
                    g_autoDetected = 1;
                    return;
                }
            }
        }
    }

    for (int s = 0; s < 2; ++s) {
        if (ScanGogRegistryEnum(HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games", sams[s], outDir, outCap) ||
            ScanGogRegistryEnum(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\GOG.com\\Games", sams[s], outDir, outCap) ||
            ScanGogRegistryEnum(HKEY_CURRENT_USER,  "SOFTWARE\\GOG.com\\Games", sams[s], outDir, outCap)) {
            g_autoDetected = 1;
            return;
        }
    }

    if (ScanSteamLibraries(outDir, outCap)) {
        g_autoDetected = 1;
        return;
    }

    // 3. Scan common GOG / Steam / Retail paths across C: through H:
    const char* relPaths[] = {
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
        for (size_t i = 0; i < sizeof(relPaths) / sizeof(relPaths[0]); ++i) {
            char cand[MAX_PATH];
            snprintf(cand, sizeof(cand), "%c:\\%s", drv, relPaths[i]);
            if (ResolveF14Dir(cand, outDir, outCap)) {
                g_autoDetected = 1;
                return;
            }
        }
    }
}

static void DetectRomDir(const char* gameDir, char* outDir, size_t cap) {
    outDir[0] = '\0';
    const char* e1 = getenv("MT32_ROM_DIR");
    if (e1 && HasMt32Roms(e1)) { lstrcpynA(outDir, e1, (int)cap); return; }
    const char* e2 = getenv("CM32L_ROM_DIR");
    if (e2 && HasMt32Roms(e2)) { lstrcpynA(outDir, e2, (int)cap); return; }

    if (gameDir && gameDir[0]) {
        char p[MAX_PATH];
        snprintf(p, sizeof(p), "%s\\roms", gameDir);
        if (HasMt32Roms(p)) { lstrcpynA(outDir, p, (int)cap); return; }
    }
    const char* cands[] = { "C:\\munt\\roms", "C:\\mt32-roms", "C:\\Program Files\\munt\\roms" };
    for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]); ++i) {
        if (HasMt32Roms(cands[i])) { lstrcpynA(outDir, cands[i], (int)cap); return; }
    }
    const char* appdata = getenv("APPDATA");
    if (appdata && appdata[0]) {
        char p[MAX_PATH];
        snprintf(p, sizeof(p), "%s\\munt\\roms", appdata);
        if (HasMt32Roms(p)) { lstrcpynA(outDir, p, (int)cap); return; }
    }
}

static int GetDesktopShortcutPath(char* outPath, size_t cap) {
    char desk[MAX_PATH] = {0};
    if (SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desk) != S_OK) return 0;
    snprintf(outPath, cap, "%s\\Fleet Defender Gold [Native].lnk", desk);
    return 1;
}

static int CreateDesktopShortcut(const char* installDir, const char* gameDir) {
    char lnkPath[MAX_PATH];
    if (!GetDesktopShortcutPath(lnkPath, sizeof(lnkPath))) return 0;

    char targetExe[MAX_PATH], ico1[MAX_PATH], ico2[MAX_PATH];
    snprintf(targetExe, sizeof(targetExe), "%s\\f14_native.exe", installDir);
    snprintf(ico1, sizeof(ico1), "%s\\goggame-1708998465.ico", gameDir);
    snprintf(ico2, sizeof(ico2), "%s\\F14.ICO", gameDir);

    HRESULT hr = CoInitialize(NULL);
    IShellLinkA* psl = NULL;
    int ok = 0;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void**)&psl))) {
        IShellLinkA_SetPath(psl, targetExe);
        IShellLinkA_SetWorkingDirectory(psl, installDir);
        IShellLinkA_SetDescription(psl, "Fleet Defender Gold (F-14 Tomcat) Native Host");
        if (FileExistsA(ico1)) IShellLinkA_SetIconLocation(psl, ico1, 0);
        else if (FileExistsA(ico2)) IShellLinkA_SetIconLocation(psl, ico2, 0);

        IPersistFile* ppf = NULL;
        if (SUCCEEDED(IShellLinkA_QueryInterface(psl, &IID_IPersistFile, (void**)&ppf))) {
            WCHAR wpath[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, lnkPath, -1, wpath, MAX_PATH);
            if (SUCCEEDED(IPersistFile_Save(ppf, wpath, TRUE))) ok = 1;
            IPersistFile_Release(ppf);
        }
        IShellLinkA_Release(psl);
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    return ok;
}

static void WriteJsonEscaped(FILE* f, const char* s) {
    fputc('"', f);
    for (; *s; ++s) {
        if (*s == '\\' || *s == '"') fputc('\\', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static void WriteDefaultSettingsIfMissing(const char* installDir, const char* gameDir, const char* romDir, int fastBoot) {
    char cfgPath[MAX_PATH];
    snprintf(cfgPath, sizeof(cfgPath), "%s\\f14_settings.json", installDir);
    if (FileExistsA(cfgPath)) return; // Never overwrite user's customized settings

    FILE* f = fopen(cfgPath, "w");
    if (!f) return;
    fprintf(f, "{\n  \"audio\": {\n    \"cm32l_rom_dir\": ");
    WriteJsonEscaped(f, (romDir && romDir[0]) ? romDir : "roms");
    fprintf(f, ",\n    \"master_volume\": 1.0,\n    \"mt32_volume\": 1.0,\n    \"sb16_sfx_volume\": 1.0,\n    \"engine_bus_volume\": 1.0,\n    \"weapons_bus_volume\": 1.0\n  },\n");
    fprintf(f, "  \"render\": {\n    \"mode\": 2,\n    \"hybrid_fill_widescreen\": true,\n    \"widescreen_3d_cockpit\": true,\n    \"enhanced_3d_padlock\": true,\n    \"show_clouds\": true,\n    \"horizon_fog\": true,\n    \"wireframe_terrain\": false,\n    \"smart_scaling_mode\": 2,\n    \"fov_deg\": 50.0,\n    \"widescreen_fov_deg\": 54.0,\n    \"aircraft_scale\": 1.0,\n    \"lighting_mode\": 0\n  },\n");
    fprintf(f, "  \"flight_model\": {\n    \"enabled\": false,\n    \"engine_f110_ge400\": false,\n    \"auto_wing_sweep\": true,\n    \"manual_sweep_deg\": 20.0,\n    \"aoa_limiter\": true\n  },\n");
    fprintf(f, "  \"host\": {\n    \"game_dir\": ");
    WriteJsonEscaped(f, gameDir);
    fprintf(f, ",\n    \"show_launcher_on_startup\": true,\n    \"skip_intro\": %s,\n    \"flight_input_mode\": 0,\n    \"window_w\": 1280,\n    \"window_h\": 720\n  }\n}\n",
            fastBoot ? "true" : "false");
    fclose(f);
}

static int DoInstall(const char* installDir, const char* romDir, int doBackup, int doShortcut, int doFastBoot, char* errMsg, size_t errCap) {
    char gameDir[MAX_PATH] = {0};
    if (!ResolveF14Dir(installDir, gameDir, sizeof(gameDir))) {
        snprintf(errMsg, errCap, "F14.COM / F14.EXE was not found in:\n%s\n\nPlease select your Fleet Defender Gold installation folder.", installDir);
        return 0;
    }

    HANDLE hSelf = CreateFileA(g_selfPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSelf == INVALID_HANDLE_VALUE) {
        snprintf(errMsg, errCap, "Could not open installer executable to read payload.");
        return 0;
    }
    DWORD fileSize = GetFileSize(hSelf, NULL);
    if (fileSize < sizeof(PayloadTrailer)) {
        CloseHandle(hSelf);
        snprintf(errMsg, errCap, "Installer payload is missing or corrupted.");
        return 0;
    }

    PayloadTrailer tr;
    SetFilePointer(hSelf, (LONG)(fileSize - sizeof(PayloadTrailer)), NULL, FILE_BEGIN);
    DWORD rd = 0;
    if (!ReadFile(hSelf, &tr, sizeof(tr), &rd, NULL) || rd != sizeof(tr) ||
        memcmp(tr.magic, PAYLOAD_MAGIC, 8) != 0 || tr.entryCount == 0 || tr.entryCount > 64) {
        CloseHandle(hSelf);
        snprintf(errMsg, errCap, "Invalid installer payload signature. Please re-download the release binary.");
        return 0;
    }

    PayloadEntry* entries = (PayloadEntry*)malloc(tr.entryCount * sizeof(PayloadEntry));
    if (!entries) {
        CloseHandle(hSelf);
        snprintf(errMsg, errCap, "Out of memory.");
        return 0;
    }
    SetFilePointer(hSelf, (LONG)tr.entryTableOffset, NULL, FILE_BEGIN);
    if (!ReadFile(hSelf, entries, tr.entryCount * sizeof(PayloadEntry), &rd, NULL) ||
        rd != tr.entryCount * sizeof(PayloadEntry)) {
        free(entries);
        CloseHandle(hSelf);
        snprintf(errMsg, errCap, "Failed to read embedded file table.");
        return 0;
    }

    // 1. Backup pre-existing f14_native.exe if present
    if (doBackup) {
        char prevExe[MAX_PATH + 32], bakDir[MAX_PATH + 32], bakExe[MAX_PATH + 64];
        snprintf(prevExe, sizeof(prevExe), "%s\\f14_native.exe", installDir);
        snprintf(bakDir, sizeof(bakDir), "%s\\_f14_native_backup", installDir);
        snprintf(bakExe, sizeof(bakExe), "%s\\f14_native.exe", bakDir);
        if (FileExistsA(prevExe)) {
            if (!DirExistsA(bakDir)) CreateDirectoryA(bakDir, NULL);
            if (!FileExistsA(bakExe)) CopyFileA(prevExe, bakExe, TRUE);
        }
    }

    // 2. Extract all bundled payload files with CRC32 verification
    for (uint32_t i = 0; i < tr.entryCount; ++i) {
        uint8_t* buf = (uint8_t*)malloc(entries[i].size > 0 ? entries[i].size : 1);
        if (!buf) {
            snprintf(errMsg, errCap, "Out of memory extracting %s.", entries[i].relPath);
            free(entries);
            CloseHandle(hSelf);
            return 0;
        }
        SetFilePointer(hSelf, (LONG)(tr.payloadStartOffset + entries[i].offsetFromPayload), NULL, FILE_BEGIN);
        if (entries[i].size > 0 && (!ReadFile(hSelf, buf, entries[i].size, &rd, NULL) || rd != entries[i].size)) {
            snprintf(errMsg, errCap, "Read error extracting %s.", entries[i].relPath);
            free(buf); free(entries); CloseHandle(hSelf);
            return 0;
        }
        if (CalcCrc32(buf, entries[i].size) != entries[i].crc32) {
            snprintf(errMsg, errCap, "CRC checksum mismatch on %s. Download may be corrupted.", entries[i].relPath);
            free(buf); free(entries); CloseHandle(hSelf);
            return 0;
        }

        char dest[MAX_PATH];
        snprintf(dest, sizeof(dest), "%s\\%s", installDir, entries[i].relPath);
        EnsureParentDirs(dest);

        HANDLE hOut = CreateFileA(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut == INVALID_HANDLE_VALUE) {
            DWORD gle = GetLastError();
            free(buf); free(entries); CloseHandle(hSelf);
            snprintf(errMsg, errCap,
                     "Failed to write:\n%s\n\nError code: %lu\n(Close Fleet Defender Native if running, or run as Administrator if inside Program Files.)",
                     dest, (unsigned long)gle);
            return 0;
        }
        DWORD wr = 0;
        BOOL ok = (entries[i].size == 0) || (WriteFile(hOut, buf, entries[i].size, &wr, NULL) && wr == entries[i].size);
        CloseHandle(hOut);
        free(buf);
        if (!ok) {
            free(entries); CloseHandle(hSelf);
            snprintf(errMsg, errCap, "Disk write failed on %s.", dest);
            return 0;
        }
    }

    free(entries);
    CloseHandle(hSelf);

    // 3. Write initial f14_settings.json if absent
    WriteDefaultSettingsIfMissing(installDir, gameDir, romDir, doFastBoot);

    // 4. Optional Desktop shortcut
    if (doShortcut) {
        CreateDesktopShortcut(installDir, gameDir);
    }

    return 1;
}

static int DoUninstall(const char* installDir, char* errMsg, size_t errCap) {
    char gameDir[MAX_PATH] = {0};
    if (!ResolveF14Dir(installDir, gameDir, sizeof(gameDir))) {
        snprintf(errMsg, errCap, "F14.COM / F14.EXE was not found in:\n%s", installDir);
        return 0;
    }

    const char* removeFiles[] = {
        "f14_native.exe",
        "THIRD_PARTY_LICENSES.txt",
        "README_NATIVE.md",
        "docs\\F14_Quickstart_Keys.svg",
        "docs\\F14_Quickstart_Keys.png"
    };
    for (size_t i = 0; i < sizeof(removeFiles) / sizeof(removeFiles[0]); ++i) {
        char p[MAX_PATH];
        snprintf(p, sizeof(p), "%s\\%s", installDir, removeFiles[i]);
        if (FileExistsA(p)) DeleteFileA(p);
    }
    char docsDir[MAX_PATH];
    snprintf(docsDir, sizeof(docsDir), "%s\\docs", installDir);
    RemoveDirectoryA(docsDir); // Only removes if empty

    // Restore backed-up f14_native.exe if present
    char bakDir[MAX_PATH + 32], bakExe[MAX_PATH + 64], dstExe[MAX_PATH + 32];
    snprintf(bakDir, sizeof(bakDir), "%s\\_f14_native_backup", installDir);
    snprintf(bakExe, sizeof(bakExe), "%s\\f14_native.exe", bakDir);
    snprintf(dstExe, sizeof(dstExe), "%s\\f14_native.exe", installDir);
    if (FileExistsA(bakExe)) {
        MoveFileExA(bakExe, dstExe, MOVEFILE_REPLACE_EXISTING);
    }
    RemoveDirectoryA(bakDir);

    char lnkPath[MAX_PATH];
    if (GetDesktopShortcutPath(lnkPath, sizeof(lnkPath)) && FileExistsA(lnkPath)) {
        DeleteFileA(lnkPath);
    }
    return 1;
}

static void RefreshPathStatus(HWND hwnd) {
    char dir[MAX_PATH] = {0}, rom[MAX_PATH] = {0};
    GetDlgItemTextA(hwnd, IDC_PATH_EDIT, dir, MAX_PATH);
    GetDlgItemTextA(hwnd, IDC_ROM_EDIT, rom, MAX_PATH);
    TrimTrailingSlashes(dir);
    TrimTrailingSlashes(rom);

    int ok = ResolveF14Dir(dir, NULL, 0);
    SetDlgItemTextA(hwnd, IDC_STATUS_TXT,
                    ok ? (g_autoDetected ? "Status: Auto-detected Fleet Defender Gold - ready for 1-click install!"
                                         : "Status: Found F14.COM / F14.EXE - ready for 1-click install!")
                       : "Status: F14.COM / F14.EXE not found - click Browse... to select your game folder.");
    EnableWindow(GetDlgItem(hwnd, IDC_INSTALL_BTN), ok);
    EnableWindow(GetDlgItem(hwnd, IDC_UNINST_BTN), ok);

    int romOk = HasMt32Roms(rom);
    SetDlgItemTextA(hwnd, IDC_ROM_STATUS,
                    romOk ? "ROM Status: Valid Roland CM-32L / MT-32 ROM pair detected!"
                          : "ROM Status: Optional (leave blank for OPL FM / SB16; add ROMs anytime in Launcher/F12).");
}

static void BrowseForFolderInto(HWND hwnd, int editId, const char* title) {
    BROWSEINFOA bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = title;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH] = {0};
        if (SHGetPathFromIDListA(pidl, path)) {
            if (editId == IDC_PATH_EDIT) g_autoDetected = 0;
            SetDlgItemTextA(hwnd, editId, path);
            RefreshPathStatus(hwnd);
        }
        CoTaskMemFree(pidl);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_fontTitle = CreateFontA(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            g_fontBold  = CreateFontA(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            g_fontBody  = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            HWND h;
            h = CreateWindowA("STATIC", "Fleet Defender Gold - Native Host " APP_VERSION,
                              WS_CHILD | WS_VISIBLE, 20, 14, 540, 28, hwnd, NULL, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

            h = CreateWindowA("STATIC",
                              "Standalone SDL3 + OpenGL HD Renderer, 360 Cockpit, Roland MT-32/CM-32L + SB16 Audio,\n"
                              "Multi-HOTAS Mapper & 240 Hz F-14 Flight Model — runs outside DOSBox.",
                              WS_CHILD | WS_VISIBLE, 20, 46, 540, 36, hwnd, NULL, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("STATIC", "Fleet Defender Gold Install Folder:",
                              WS_CHILD | WS_VISIBLE, 20, 92, 340, 20, hwnd, NULL, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

            h = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                20, 114, 434, 26, hwnd, (HMENU)IDC_PATH_EDIT, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("BUTTON", "Browse...",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              462, 113, 96, 28, hwnd, (HMENU)IDC_BROWSE_BTN, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("STATIC", "",
                              WS_CHILD | WS_VISIBLE, 20, 145, 540, 20, hwnd, (HMENU)IDC_STATUS_TXT, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

            h = CreateWindowA("STATIC", "Roland CM-32L / MT-32 ROM Folder (Optional):",
                              WS_CHILD | WS_VISIBLE, 20, 175, 360, 20, hwnd, NULL, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

            h = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                20, 197, 434, 26, hwnd, (HMENU)IDC_ROM_EDIT, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("BUTTON", "Browse...",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              462, 196, 96, 28, hwnd, (HMENU)IDC_ROM_BROWSE, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("STATIC", "",
                              WS_CHILD | WS_VISIBLE, 20, 228, 540, 20, hwnd, (HMENU)IDC_ROM_STATUS, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("BUTTON", "Backup any existing f14_native.exe to _f14_native_backup\\",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              20, 258, 540, 22, hwnd, (HMENU)IDC_CHK_BACKUP, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
            SendMessageA(h, BM_SETCHECK, BST_CHECKED, 0);

            h = CreateWindowA("BUTTON", "Create Desktop shortcut (Fleet Defender Gold [Native])",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              20, 285, 540, 22, hwnd, (HMENU)IDC_CHK_SHORT, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
            SendMessageA(h, BM_SETCHECK, BST_CHECKED, 0);

            h = CreateWindowA("BUTTON", "Enable Fast-Boot (skip startup intro) && Hybrid Widescreen HD defaults",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              20, 312, 540, 22, hwnd, (HMENU)IDC_CHK_FAST, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
            SendMessageA(h, BM_SETCHECK, BST_CHECKED, 0);

            h = CreateWindowA("BUTTON", "Install / Update",
                              WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                              20, 354, 175, 36, hwnd, (HMENU)IDC_INSTALL_BTN, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

            h = CreateWindowA("BUTTON", "Uninstall / Restore",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              208, 354, 165, 36, hwnd, (HMENU)IDC_UNINST_BTN, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("BUTTON", "Close",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              462, 354, 96, 36, hwnd, (HMENU)IDC_CLOSE_BTN, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            char detected[MAX_PATH] = {0}, detectedRom[MAX_PATH] = {0};
            DetectGameDir(detected, sizeof(detected));
            DetectRomDir(detected, detectedRom, sizeof(detectedRom));
            SetDlgItemTextA(hwnd, IDC_PATH_EDIT, detected);
            SetDlgItemTextA(hwnd, IDC_ROM_EDIT, detectedRom);
            RefreshPathStatus(hwnd);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if ((id == IDC_PATH_EDIT || id == IDC_ROM_EDIT) && code == EN_CHANGE) {
                RefreshPathStatus(hwnd);
                return 0;
            }
            if (id == IDC_BROWSE_BTN) {
                BrowseForFolderInto(hwnd, IDC_PATH_EDIT, "Select your Fleet Defender Gold folder (containing F14.COM / F14.EXE):");
                return 0;
            }
            if (id == IDC_ROM_BROWSE) {
                BrowseForFolderInto(hwnd, IDC_ROM_EDIT, "Select your Roland CM-32L / MT-32 ROM folder:");
                return 0;
            }
            if (id == IDC_CLOSE_BTN) {
                PostQuitMessage(0);
                return 0;
            }
            if (id == IDC_INSTALL_BTN) {
                char dir[MAX_PATH] = {0}, rom[MAX_PATH] = {0}, err[512] = {0};
                GetDlgItemTextA(hwnd, IDC_PATH_EDIT, dir, MAX_PATH);
                GetDlgItemTextA(hwnd, IDC_ROM_EDIT, rom, MAX_PATH);
                TrimTrailingSlashes(dir);
                TrimTrailingSlashes(rom);

                int doBackup = (SendDlgItemMessageA(hwnd, IDC_CHK_BACKUP, BM_GETCHECK, 0, 0) == BST_CHECKED);
                int doShort  = (SendDlgItemMessageA(hwnd, IDC_CHK_SHORT,  BM_GETCHECK, 0, 0) == BST_CHECKED);
                int doFast   = (SendDlgItemMessageA(hwnd, IDC_CHK_FAST,   BM_GETCHECK, 0, 0) == BST_CHECKED);

                if (!DoInstall(dir, rom, doBackup, doShort, doFast, err, sizeof(err))) {
                    MessageBoxA(hwnd, err, APP_TITLE, MB_OK | MB_ICONERROR);
                    return 0;
                }
                int res = MessageBoxA(hwnd,
                    "Fleet Defender Gold Native Host " APP_VERSION " installed successfully!\n\n"
                    "- Launch anytime via f14_native.exe or your Desktop shortcut.\n"
                    "- Press F11 to cycle Render Modes and F12 for Settings, HOTAS, Audio & Quick Keys.\n\n"
                    "Launch Fleet Defender Gold Native now?",
                    APP_TITLE, MB_YESNO | MB_ICONINFORMATION);
                if (res == IDYES) {
                    char exe[MAX_PATH];
                    snprintf(exe, sizeof(exe), "%s\\f14_native.exe", dir);
                    ShellExecuteA(NULL, "open", exe, NULL, dir, SW_SHOWNORMAL);
                }
                PostQuitMessage(0);
                return 0;
            }
            if (id == IDC_UNINST_BTN) {
                char dir[MAX_PATH] = {0}, err[512] = {0};
                GetDlgItemTextA(hwnd, IDC_PATH_EDIT, dir, MAX_PATH);
                TrimTrailingSlashes(dir);

                if (MessageBoxA(hwnd,
                        "Remove Fleet Defender Gold Native runtime files and restore any backup?\n(Your game files, campaigns, and f14_settings.json will be kept.)",
                        APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    if (!DoUninstall(dir, err, sizeof(err))) {
                        MessageBoxA(hwnd, err, APP_TITLE, MB_OK | MB_ICONERROR);
                    } else {
                        MessageBoxA(hwnd, "Uninstalled Fleet Defender Gold Native successfully.", APP_TITLE, MB_OK | MB_ICONINFORMATION);
                    }
                }
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontBold)  DeleteObject(g_fontBold);
            if (g_fontBody)  DeleteObject(g_fontBody);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int ParseSilentArgs(const char* cmd, int* outSilent, int* outUninstall, char* outDir, size_t cap) {
    *outSilent = 0;
    *outUninstall = 0;
    outDir[0] = '\0';
    if (!cmd || !cmd[0]) return 0;

    if (strstr(cmd, "/S") || strstr(cmd, "/s") || strstr(cmd, "--silent")) *outSilent = 1;
    if (strstr(cmd, "/U") || strstr(cmd, "/u") || strstr(cmd, "--uninstall")) *outUninstall = 1;

    const char* d = strstr(cmd, "/D=");
    if (!d) d = strstr(cmd, "/d=");
    if (d) {
        d += 3;
        int quoted = (*d == '"');
        if (quoted) ++d;
        size_t i = 0;
        while (*d && i + 1 < cap) {
            if (quoted && *d == '"') break;
            if (!quoted && (*d == ' ' || *d == '\t')) break;
            outDir[i++] = *d++;
        }
        outDir[i] = '\0';
        TrimTrailingSlashes(outDir);
    }
    return (*outSilent || *outUninstall);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShowCmd) {
    (void)hPrev; (void)nShowCmd;
    GetModuleFileNameA(NULL, g_selfPath, MAX_PATH);

    // Silent CLI mode: /S /D="C:\Path\To\Fleet Defender Gold" or /U /D="C:\Path\To\Fleet Defender Gold"
    int silent = 0, uninst = 0;
    char cliDir[MAX_PATH] = {0};
    if (ParseSilentArgs(lpCmdLine, &silent, &uninst, cliDir, sizeof(cliDir))) {
        if (!cliDir[0]) DetectGameDir(cliDir, sizeof(cliDir));
        char err[512] = {0};
        if (uninst) {
            return DoUninstall(cliDir, err, sizeof(err)) ? 0 : 1;
        }
        char romDir[MAX_PATH] = {0};
        DetectRomDir(cliDir, romDir, sizeof(romDir));
        return DoInstall(cliDir, romDir, 1, 0, 1, err, sizeof(err)) ? 0 : 1;
    }

    InitCommonControls();

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconA(NULL, IDI_APPLICATION);
    wc.lpszClassName = "F14NativeSetupWnd";
    RegisterClassA(&wc);

    int w = 594, h = 446;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    g_hwnd = CreateWindowA(wc.lpszClassName, APP_TITLE " " APP_VERSION,
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           sx, sy, w, h, NULL, NULL, hInst, NULL);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

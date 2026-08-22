#include "config.h"

#include <shlwapi.h>

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s = buf;
    const size_t cut = s.find_last_of(L'\\');
    return (cut == std::wstring::npos) ? std::wstring{} : s.substr(0, cut + 1);
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        if (CompareStringOrdinal(haystack.c_str() + i, (int)needle.size(), needle.c_str(),
                                 (int)needle.size(), TRUE) == CSTR_EQUAL)
            return true;
    }
    return false;
}

namespace {

std::wstring ReadStr(const std::wstring& file, const wchar_t* sec, const wchar_t* key,
                     const wchar_t* def) {
    wchar_t buf[512]{};
    GetPrivateProfileStringW(sec, key, def, buf, (DWORD)std::size(buf), file.c_str());
    return buf;
}

int ReadInt(const std::wstring& file, const wchar_t* sec, const wchar_t* key, int def) {
    // GetPrivateProfileIntW non gestisce i negativi: passiamo per la stringa.
    const std::wstring s = ReadStr(file, sec, key, std::to_wstring(def).c_str());
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}

void WriteInt(const std::wstring& file, const wchar_t* sec, const wchar_t* key, int v) {
    WritePrivateProfileStringW(sec, key, std::to_wstring(v).c_str(), file.c_str());
}

// La sezione [spotify] si chiama [player] dalla 1.1: i file vecchi devono
// continuare a funzionare, quindi in lettura si prova la nuova e si ripiega
// sulla vecchia. In scrittura si usa sempre la nuova.
constexpr wchar_t kPlayerSection[] = L"player";
constexpr wchar_t kLegacySection[] = L"spotify";
constexpr wchar_t kAbsent[]        = L"\x1";  // sentinella: chiave non presente

std::wstring ReadPlayerStr(const std::wstring& file, const wchar_t* key, const wchar_t* def) {
    std::wstring v = ReadStr(file, kPlayerSection, key, kAbsent);
    if (v != kAbsent) return v;
    v = ReadStr(file, kLegacySection, key, kAbsent);
    return (v != kAbsent) ? v : std::wstring(def);
}

int ReadPlayerInt(const std::wstring& file, const wchar_t* key, int def) {
    const std::wstring s = ReadPlayerStr(file, key, std::to_wstring(def).c_str());
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}

DockEdge ParseEdge(const std::wstring& s) {
    if (s == L"left") return DockEdge::Left;
    if (s == L"top") return DockEdge::Top;
    if (s == L"right") return DockEdge::Right;
    return DockEdge::Bottom;
}

BarMode ParseMode(const std::wstring& s) {
    if (s == L"docked") return BarMode::Docked;
    if (s == L"taskbar") return BarMode::Taskbar;
    return BarMode::Overlay;
}

const wchar_t* ModeName(BarMode m) {
    switch (m) {
        case BarMode::Docked: return L"docked";
        case BarMode::Taskbar: return L"taskbar";
        default: return L"overlay";
    }
}

TaskbarAnchor ParseAnchor(const std::wstring& s) {
    if (s == L"left") return TaskbarAnchor::Left;
    if (s == L"center") return TaskbarAnchor::Center;
    if (s == L"right") return TaskbarAnchor::Right;
    return TaskbarAnchor::Auto;
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

void Config::EnsurePath() {
    if (path_.empty()) path_ = ExeDir() + L"minibar.ini";
}

void Config::Load() {
    EnsurePath();
    WriteDefaultsIfMissing();

    mode         = ParseMode(ReadStr(path_, L"general", L"mode", L"overlay"));
    preferredApp = ReadStr(path_, L"general", L"preferred_app", L"Spotify");

    overlayLayout = (ReadStr(path_, L"overlay", L"layout", L"bar") == L"tile")
                        ? OverlayLayout::Tile
                        : OverlayLayout::Bar;
    barWidth      = Clamp(ReadInt(path_, L"overlay", L"bar_width", 266), 168, 840);
    tileSize      = Clamp(ReadInt(path_, L"overlay", L"tile_size", 92), 68, 224);
    x             = ReadInt(path_, L"overlay", L"x", INT_MIN);
    y             = ReadInt(path_, L"overlay", L"y", INT_MIN);
    idleOpacity   = Clamp(ReadInt(path_, L"overlay", L"idle_opacity", 140), 20, 255);

    edge = ParseEdge(ReadStr(path_, L"docked", L"edge", L"bottom"));

    taskbarAnchor = ParseAnchor(ReadStr(path_, L"taskbar", L"anchor", L"auto"));

    autoMinimize    = ReadPlayerInt(path_, L"auto_minimize", 1) != 0;
    hideInstead     = ReadPlayerInt(path_, L"hide_instead", 0) != 0;
    launchIfMissing = ReadPlayerInt(path_, L"launch_if_missing", 0) != 0;
    launchPath      = ReadPlayerStr(path_, L"launch_path", L"");

    volumeStep = Clamp(ReadInt(path_, L"volume", L"wheel_step", 4), 1, 25);

    hotkeyEnabled = ReadInt(path_, L"hotkey", L"enabled", 0) != 0;
    hotkeyMods    = 0;
    const std::wstring mods = ReadStr(path_, L"hotkey", L"modifiers", L"alt+shift");
    if (ContainsNoCase(mods, L"alt")) hotkeyMods |= MOD_ALT;
    if (ContainsNoCase(mods, L"ctrl")) hotkeyMods |= MOD_CONTROL;
    if (ContainsNoCase(mods, L"shift")) hotkeyMods |= MOD_SHIFT;
    if (ContainsNoCase(mods, L"win")) hotkeyMods |= MOD_WIN;
    if (!hotkeyMods) hotkeyMods = MOD_ALT | MOD_SHIFT;
    const std::wstring k = ReadStr(path_, L"hotkey", L"key", L"M");
    hotkeyVk             = k.empty() ? 'M' : (UINT)towupper(k[0]);
}

void Config::WriteDefaultsIfMissing() {
    EnsurePath();
    if (PathFileExistsW(path_.c_str())) return;

    WritePrivateProfileStringW(L"general", L"mode", L"overlay", path_.c_str());
    WritePrivateProfileStringW(L"general", L"preferred_app", L"Spotify", path_.c_str());
    WritePrivateProfileStringW(L"overlay", L"layout", L"bar", path_.c_str());
    WritePrivateProfileStringW(L"overlay", L"bar_width", L"266", path_.c_str());
    WritePrivateProfileStringW(L"overlay", L"tile_size", L"92", path_.c_str());
    WritePrivateProfileStringW(L"overlay", L"idle_opacity", L"140", path_.c_str());
    WritePrivateProfileStringW(L"docked", L"edge", L"bottom", path_.c_str());
    WritePrivateProfileStringW(L"taskbar", L"anchor", L"auto", path_.c_str());
    WritePrivateProfileStringW(kPlayerSection, L"auto_minimize", L"1", path_.c_str());
    WritePrivateProfileStringW(kPlayerSection, L"hide_instead", L"0", path_.c_str());
    WritePrivateProfileStringW(kPlayerSection, L"launch_if_missing", L"0", path_.c_str());
    WritePrivateProfileStringW(kPlayerSection, L"launch_path", L"", path_.c_str());
    WritePrivateProfileStringW(L"volume", L"wheel_step", L"4", path_.c_str());
    WritePrivateProfileStringW(L"hotkey", L"enabled", L"0", path_.c_str());
    WritePrivateProfileStringW(L"hotkey", L"modifiers", L"alt+shift", path_.c_str());
    WritePrivateProfileStringW(L"hotkey", L"key", L"M", path_.c_str());
}

int Config::OverlaySize() const {
    return overlayLayout == OverlayLayout::Tile ? tileSize : barWidth;
}

void Config::SaveOverlaySize(int dip) {
    EnsurePath();
    if (overlayLayout == OverlayLayout::Tile) {
        tileSize = Clamp(dip, 68, 224);
        WriteInt(path_, L"overlay", L"tile_size", tileSize);
    } else {
        barWidth = Clamp(dip, 168, 840);
        WriteInt(path_, L"overlay", L"bar_width", barWidth);
    }
}

void Config::SavePosition(int nx, int ny) {
    EnsurePath();
    x = nx;
    y = ny;
    WriteInt(path_, L"overlay", L"x", nx);
    WriteInt(path_, L"overlay", L"y", ny);
}

void Config::SaveMode(BarMode m) {
    EnsurePath();
    mode = m;
    WritePrivateProfileStringW(L"general", L"mode", ModeName(m), path_.c_str());
}

void Config::SaveOverlayLayout(OverlayLayout l) {
    EnsurePath();
    overlayLayout = l;
    WritePrivateProfileStringW(L"overlay", L"layout", l == OverlayLayout::Tile ? L"tile" : L"bar",
                               path_.c_str());
}

void Config::SaveAutoMinimize(bool v) {
    EnsurePath();
    autoMinimize = v;
    WriteInt(path_, kPlayerSection, L"auto_minimize", v ? 1 : 0);
}

void Config::SavePreferredApp(const std::wstring& app) {
    EnsurePath();
    preferredApp = app;
    WritePrivateProfileStringW(L"general", L"preferred_app", app.c_str(), path_.c_str());
}

#include "player.h"

#include <appmodel.h>
#include <psapi.h>
#include <shlwapi.h>

namespace {

struct FindCtx {
    const std::wstring* key;
    bool                includeHidden;
    HWND                best;
    LONG                bestArea;
};

std::wstring StripExe(std::wstring s) {
    if (s.size() > 4 && _wcsicmp(s.c_str() + s.size() - 4, L".exe") == 0) s.resize(s.size() - 4);
    return s;
}

// Nome dell'eseguibile senza estensione; vuoto se il processo non è leggibile.
std::wstring ExeStem(HANDLE proc) {
    wchar_t path[MAX_PATH]{};
    DWORD   len = MAX_PATH;
    if (!QueryFullProcessImageNameW(proc, 0, path, &len)) return {};
    return StripExe(PathFindFileNameW(path));
}

// AUMID di un'app impacchettata; vuoto per le app Win32 classiche.
std::wstring PackagedAumid(HANDLE proc) {
    UINT32 len = 0;
    if (GetApplicationUserModelId(proc, &len, nullptr) != ERROR_INSUFFICIENT_BUFFER || len == 0)
        return {};

    std::wstring id(len, L'\0');
    if (GetApplicationUserModelId(proc, &len, id.data()) != ERROR_SUCCESS) return {};
    id.resize(len > 0 ? len - 1 : 0);  // len include il terminatore
    return id;
}

// Il nome del processo e quello dichiarato dalla sessione SMTC quasi mai
// coincidono alla lettera ("Chrome" contro chrome.exe): basta che uno contenga
// l'altro, con una lunghezza minima che eviti collisioni casuali.
bool StemsMatch(const std::wstring& exeStem, const std::wstring& keyStem) {
    if (exeStem.empty() || keyStem.size() < 2) return false;
    if (ContainsNoCase(exeStem, keyStem)) return true;
    return exeStem.size() >= 4 && ContainsNoCase(keyStem, exeStem);
}

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindCtx*>(lp);

    // I player basati su Chromium creano molte top-level invisibili con la
    // stessa classe: la classe non è un criterio utile. Filtriamo su processo +
    // stili + rect + titolo.
    if (!ctx->includeHidden && !IsWindowVisible(hwnd)) return TRUE;

    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    const LONG w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return TRUE;

    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!PlayerManager::ProcessMatches(pid, *ctx->key)) return TRUE;

    const LONG area = w * h;
    if (area > ctx->bestArea) {
        ctx->bestArea = area;
        ctx->best     = hwnd;
    }
    return TRUE;
}

void WriteMarker(const std::wstring& path, const std::wstring& text) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
}

std::wstring ReadMarker(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};

    wchar_t buf[256]{};
    DWORD   read = 0;
    ReadFile(f, buf, sizeof(buf) - sizeof(wchar_t), &read, nullptr);
    CloseHandle(f);
    return std::wstring(buf, read / sizeof(wchar_t));
}

}  // namespace

// ------------------------------------------------------------------ identità --
bool PlayerManager::ProcessMatches(DWORD pid, const std::wstring& appKey) {
    if (!pid || appKey.empty()) return false;

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;

    const bool         packaged = appKey.find(L'!') != std::wstring::npos;
    const std::wstring stem     = ExeStem(h);
    const std::wstring aumid    = packaged ? PackagedAumid(h) : std::wstring{};
    CloseHandle(h);

    if (packaged) {
        if (!aumid.empty()) return _wcsicmp(aumid.c_str(), appKey.c_str()) == 0;
        // AUMID non leggibile: il nome del pacchetto contiene quasi sempre
        // quello dell'eseguibile ("SpotifyAB.SpotifyMusic_…!Spotify").
        return stem.size() >= 4 && ContainsNoCase(appKey, stem);
    }
    return StemsMatch(stem, StripExe(appKey));
}

std::wstring PlayerManager::FriendlyName(const std::wstring& appKey) {
    if (appKey.empty()) return {};

    std::wstring name = appKey;

    // AUMID: "Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic" → ZuneMusic.
    const size_t bang = name.find_last_of(L'!');
    if (bang != std::wstring::npos) name = name.substr(bang + 1);
    const size_t dot = name.find_last_of(L'.');
    if (bang != std::wstring::npos && dot != std::wstring::npos) name = name.substr(dot + 1);

    name = StripExe(name);
    if (name.empty()) return appKey;

    name[0] = static_cast<wchar_t>(towupper(name[0]));
    return name;
}

// ------------------------------------------------------------------ finestra --
HWND PlayerManager::FindMainWindow(const std::wstring& appKey, bool includeHidden) {
    if (appKey.empty()) return nullptr;
    FindCtx ctx{&appKey, includeHidden, nullptr, 0};
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.best;
}

bool PlayerManager::IsRunning(const std::wstring& appKey) {
    return FindMainWindow(appKey, true) != nullptr;
}

std::wstring PlayerManager::MarkerPath() { return ExeDir() + L"minibar-hidden.flag"; }

void PlayerManager::Apply(const std::wstring& appKey, bool hideInstead) {
    HWND w = FindMainWindow(appKey, false);
    if (!w) return;

    if (hideInstead) {
        // Una finestra nascosta non è recuperabile da taskbar/Alt-Tab: lasciamo
        // un marker su disco perché il prossimo avvio sappia chi recuperare.
        WriteMarker(MarkerPath(), appKey);
        ShowWindow(w, SW_HIDE);
        hid_       = true;
        minimized_ = false;
    } else {
        ShowWindow(w, SW_MINIMIZE);
        minimized_ = true;
        hid_       = false;
    }
    applied_ = appKey;
}

void PlayerManager::Restore() {
    if (!hid_ && !minimized_) return;

    HWND w = FindMainWindow(applied_, true);
    if (w) {
        if (hid_) ShowWindow(w, SW_SHOW);
        ShowWindow(w, SW_RESTORE);
    }
    DeleteFileW(MarkerPath().c_str());
    hid_       = false;
    minimized_ = false;
    applied_.clear();
}

void PlayerManager::RecoverOrphan() {
    // Il marker delle versioni "solo Spotify" non conteneva il nome del player.
    const std::wstring legacy = ExeDir() + L"minibar-spotify-hidden.flag";

    struct Candidate {
        std::wstring path;
        std::wstring fallbackKey;
    } const candidates[] = {{MarkerPath(), {}}, {legacy, L"Spotify"}};

    for (const auto& c : candidates) {
        if (!PathFileExistsW(c.path.c_str())) continue;

        std::wstring key = ReadMarker(c.path);
        if (key.empty()) key = c.fallbackKey;

        HWND w = FindMainWindow(key, true);
        if (w && !IsWindowVisible(w)) {
            ShowWindow(w, SW_SHOW);
            ShowWindow(w, SW_RESTORE);
        }
        DeleteFileW(c.path.c_str());
    }
}

bool PlayerManager::Launch(const std::wstring& appKey, const std::wstring& explicitPath) {
    std::vector<std::wstring> candidates;
    if (!explicitPath.empty()) candidates.push_back(explicitPath);

    // Percorsi tipici: <root>\<Nome>\<Nome>.exe copre le installazioni
    // per-utente (Spotify, Tidal) e per-macchina della maggior parte dei player.
    const std::wstring stem = FriendlyName(appKey);
    if (!stem.empty()) {
        for (const wchar_t* var :
             {L"APPDATA", L"LOCALAPPDATA", L"ProgramFiles", L"ProgramFiles(x86)"}) {
            wchar_t root[MAX_PATH]{};
            if (GetEnvironmentVariableW(var, root, MAX_PATH))
                candidates.push_back(std::wstring(root) + L"\\" + stem + L"\\" + stem + L".exe");
        }
    }

    for (const auto& exe : candidates) {
        if (!PathFileExistsW(exe.c_str())) continue;

        std::wstring cmd = L"\"" + exe + L"\"";
        if (ContainsNoCase(exe, L"spotify.exe")) cmd += L" --minimized";  // lo capisce solo lui

        STARTUPINFOW         si{sizeof(si)};
        PROCESS_INFORMATION  pi{};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');
        if (CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                           &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
    }
    return false;
}

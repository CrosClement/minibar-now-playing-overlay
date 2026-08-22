#include "spotify.h"

#include <psapi.h>
#include <shlwapi.h>

namespace {

struct FindCtx {
    bool includeHidden;
    HWND best;
    LONG bestArea;
};

bool IsSpotifyProcess(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return SpotifyManager::IsSpotifyPid(pid);
}

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindCtx*>(lp);

    // CEF crea molte top-level invisibili con la stessa classe: la classe non
    // è un criterio utile. Filtriamo su processo + stili + rect + titolo.
    if (!ctx->includeHidden && !IsWindowVisible(hwnd)) return TRUE;

    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    const LONG w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return TRUE;

    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;
    if (!IsSpotifyProcess(hwnd)) return TRUE;

    const LONG area = w * h;
    if (area > ctx->bestArea) {
        ctx->bestArea = area;
        ctx->best     = hwnd;
    }
    return TRUE;
}

}  // namespace

bool SpotifyManager::IsSpotifyPid(DWORD pid) {
    if (!pid) return false;

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;

    wchar_t    path[MAX_PATH]{};
    DWORD      len = MAX_PATH;
    const BOOL ok  = QueryFullProcessImageNameW(h, 0, path, &len);
    CloseHandle(h);
    if (!ok) return false;

    return _wcsicmp(PathFindFileNameW(path), L"spotify.exe") == 0;
}

HWND SpotifyManager::FindMainWindow(bool includeHidden) {
    FindCtx ctx{includeHidden, nullptr, 0};
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.best;
}

bool SpotifyManager::IsRunning() { return FindMainWindow(true) != nullptr; }

std::wstring SpotifyManager::MarkerPath() { return ExeDir() + L"minibar-spotify-hidden.flag"; }

void SpotifyManager::Apply(bool hideInstead) {
    HWND w = FindMainWindow(false);
    if (!w) return;

    if (hideInstead) {
        // Una finestra nascosta non è recuperabile da taskbar/Alt-Tab: lasciamo
        // un marker su disco perché il prossimo avvio possa recuperarla.
        HANDLE f = CreateFileW(MarkerPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        ShowWindow(w, SW_HIDE);
        hid_       = true;
        minimized_ = false;
    } else {
        ShowWindow(w, SW_MINIMIZE);
        minimized_ = true;
        hid_       = false;
    }
}

void SpotifyManager::Restore() {
    if (!hid_ && !minimized_) return;

    HWND w = FindMainWindow(true);
    if (w) {
        if (hid_) ShowWindow(w, SW_SHOW);
        ShowWindow(w, SW_RESTORE);
    }
    DeleteFileW(MarkerPath().c_str());
    hid_       = false;
    minimized_ = false;
}

void SpotifyManager::RecoverOrphan() {
    if (!PathFileExistsW(MarkerPath().c_str())) return;

    HWND w = FindMainWindow(true);
    if (w && !IsWindowVisible(w)) {
        ShowWindow(w, SW_SHOW);
        ShowWindow(w, SW_RESTORE);
    }
    DeleteFileW(MarkerPath().c_str());
}

bool SpotifyManager::Launch(bool minimized) {
    // Percorsi tipici: installazione per-utente (Store esclusa) e per-macchina.
    wchar_t appdata[MAX_PATH]{};
    std::wstring candidates[2];
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH))
        candidates[0] = std::wstring(appdata) + L"\\Spotify\\Spotify.exe";
    wchar_t pf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH))
        candidates[1] = std::wstring(pf) + L"\\Spotify\\Spotify.exe";

    for (const auto& exe : candidates) {
        if (exe.empty() || !PathFileExistsW(exe.c_str())) continue;

        std::wstring cmd = L"\"" + exe + L"\"";
        if (minimized) cmd += L" --minimized";

        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
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

#include "window.h"

#include "player.h"

#include <shellapi.h>

namespace {

constexpr wchar_t kClassName[] = L"MiniBarWindow";

int ReadDword(const wchar_t* subKey, const wchar_t* name, int fallback) {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
        ERROR_SUCCESS)
        return static_cast<int>(value);
    return fallback;
}

}  // namespace

namespace win {

bool RegisterBarClass(HINSTANCE inst, WNDPROC proc, HICON icon) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // disegna tutto Direct2D
    wc.lpszClassName = kClassName;
    wc.hIcon         = icon;
    wc.hIconSm       = icon;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND CreateBarWindow(HINSTANCE inst, void* userData, int widthPx, int heightPx) {
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED |
                          WS_EX_TRANSPARENT;
    return CreateWindowExW(exStyle, kClassName, L"MiniBar", WS_POPUP, 0, 0, widthPx, heightPx,
                           nullptr, nullptr, inst, userData);
}

void SetClickThrough(HWND hwnd, bool on) {
    const LONG_PTR cur = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR next =
        on ? (cur | WS_EX_TRANSPARENT) : (cur & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));
    if (next != cur) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, next);
}

UINT DpiFor(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return dpi ? dpi : 96;
}

void ShowNoActivate(HWND hwnd) { ShowWindow(hwnd, SW_SHOWNOACTIVATE); }

RECT WorkAreaFor(HWND hwnd) {
    MONITORINFO mi{sizeof(mi)};
    HMONITOR    mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(mon, &mi)) return mi.rcWork;
    RECT r{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    return r;
}

TaskbarInfo QueryTaskbar() {
    TaskbarInfo info;

    HWND tb = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tb || !GetWindowRect(tb, &info.rect)) return info;
    info.found = true;

    HWND tray = FindWindowExW(tb, nullptr, L"TrayNotifyWnd", nullptr);
    if (!tray || !GetWindowRect(tray, &info.tray))
        info.tray = RECT{info.rect.right, info.rect.top, info.rect.right, info.rect.bottom};

    const LONG w = info.rect.right - info.rect.left;
    const LONG h = info.rect.bottom - info.rect.top;
    info.horizontal = w >= h;

    // Con l'auto-hide attivo la taskbar ritirata resta quasi tutta fuori
    // schermo: è così che ci accorgiamo che è sparita.
    MONITORINFO mi{sizeof(mi)};
    HMONITOR    mon = MonitorFromWindow(tb, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(mon, &mi)) {
        RECT inter{};
        info.onScreen = IntersectRect(&inter, &info.rect, &mi.rcMonitor) &&
                        (inter.bottom - inter.top) * 2 >= h && (inter.right - inter.left) * 2 >= w;
    }

    info.alignment = ReadDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                               L"TaskbarAl", 1);
    return info;
}

bool SystemUsesLightTheme() {
    return ReadDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", 0) != 0;
}

bool AddTrayIcon(HWND hwnd, HICON icon) {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd             = hwnd;
    nid.uID              = kTrayIconId;
    nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon            = icon;
    wcscpy_s(nid.szTip, L"MiniBar");
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    return true;
}

void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = hwnd;
    nid.uID  = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

UINT ShowContextMenu(HWND hwnd, POINT screenPt, const MenuState& state) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return 0;

    HMENU modes = CreatePopupMenu();
    AppendMenuW(modes, MF_STRING, IDM_MODE_OVERLAY, L"Overlay");
    AppendMenuW(modes, MF_STRING, IDM_MODE_DOCKED, L"Agganciata al bordo");
    AppendMenuW(modes, MF_STRING, IDM_MODE_TASKBAR, L"Dentro la taskbar");
    const UINT modeId = state.mode == BarMode::Docked    ? IDM_MODE_DOCKED
                        : state.mode == BarMode::Taskbar ? IDM_MODE_TASKBAR
                                                         : IDM_MODE_OVERLAY;
    CheckMenuRadioItem(modes, IDM_MODE_OVERLAY, IDM_MODE_TASKBAR, modeId, MF_BYCOMMAND);

    HMENU looks = CreatePopupMenu();
    AppendMenuW(looks, MF_STRING, IDM_LAYOUT_BAR, L"Barretta");
    AppendMenuW(looks, MF_STRING, IDM_LAYOUT_TILE, L"Riquadro");
    CheckMenuRadioItem(looks, IDM_LAYOUT_BAR, IDM_LAYOUT_TILE,
                       state.layout == OverlayLayout::Tile ? IDM_LAYOUT_TILE : IDM_LAYOUT_BAR,
                       MF_BYCOMMAND);

    // Selettore di player: con due sorgenti attive (Spotify e un video nel
    // browser) è qui che si sceglie quale seguire.
    HMENU players = CreatePopupMenu();
    AppendMenuW(players, MF_STRING, IDM_SESSION_AUTO, L"Automatico");
    if (!state.sessions.empty()) AppendMenuW(players, MF_SEPARATOR, 0, nullptr);

    UINT id      = IDM_SESSION_FIRST;
    UINT checked = IDM_SESSION_AUTO;
    for (const auto& session : state.sessions) {
        if (id > IDM_SESSION_LAST) break;
        AppendMenuW(players, MF_STRING, id, PlayerManager::FriendlyName(session).c_str());
        if (!state.preferred.empty() && ContainsNoCase(session, state.preferred)) checked = id;
        ++id;
    }
    CheckMenuRadioItem(players, IDM_SESSION_AUTO, id - 1, checked, MF_BYCOMMAND);

    const std::wstring who = state.playerName.empty() ? L"il player" : state.playerName;

    AppendMenuW(menu, MF_STRING, IDM_BAR_TOGGLE,
                state.barVisible ? L"Nascondi barretta" : L"Mostra barretta");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(modes), L"Modalità");
    AppendMenuW(menu, MF_POPUP | (state.mode == BarMode::Overlay ? MF_ENABLED : MF_GRAYED),
                reinterpret_cast<UINT_PTR>(looks), L"Aspetto");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(players), L"Player");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (state.autoMinimize ? MF_CHECKED : MF_UNCHECKED),
                IDM_AUTOMIN_TOGGLE, (L"Minimizza " + who).c_str());
    AppendMenuW(menu, MF_STRING, IDM_PLAYER_RESTORE, (L"Ripristina " + who).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_OPEN_INI, L"Apri minibar.ini");
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Esci");

    // La finestra è WS_EX_NOACTIVATE: senza questo il menu non si chiude
    // cliccando fuori.
    SetForegroundWindow(hwnd);
    const UINT cmd = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, screenPt.x, screenPt.y, hwnd,
        nullptr));
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return cmd;
}

}  // namespace win

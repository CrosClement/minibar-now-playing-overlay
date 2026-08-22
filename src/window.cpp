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

bool AppsUseLightTheme() {
    return ReadDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", 0) != 0;
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
    AppendMenuW(modes, MF_STRING, IDM_MODE_DOCKED, L"Docked to an edge");
    AppendMenuW(modes, MF_STRING, IDM_MODE_TASKBAR, L"In the taskbar");
    const UINT modeId = state.mode == BarMode::Docked    ? IDM_MODE_DOCKED
                        : state.mode == BarMode::Taskbar ? IDM_MODE_TASKBAR
                                                         : IDM_MODE_OVERLAY;
    CheckMenuRadioItem(modes, IDM_MODE_OVERLAY, IDM_MODE_TASKBAR, modeId, MF_BYCOMMAND);

    HMENU looks = CreatePopupMenu();
    AppendMenuW(looks, MF_STRING, IDM_LAYOUT_BAR, L"Bar");
    AppendMenuW(looks, MF_STRING, IDM_LAYOUT_TILE, L"Tile");
    CheckMenuRadioItem(looks, IDM_LAYOUT_BAR, IDM_LAYOUT_TILE,
                       state.layout == OverlayLayout::Tile ? IDM_LAYOUT_TILE : IDM_LAYOUT_BAR,
                       MF_BYCOMMAND);

    // Selettore di player: con due sorgenti attive (Spotify e un video nel
    // browser) è qui che si sceglie quale seguire.
    HMENU players = CreatePopupMenu();
    AppendMenuW(players, MF_STRING, IDM_SESSION_AUTO, L"Automatic");
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

    HMENU edges = CreatePopupMenu();
    AppendMenuW(edges, MF_STRING, IDM_EDGE_BOTTOM, L"Bottom");
    AppendMenuW(edges, MF_STRING, IDM_EDGE_TOP, L"Top");
    AppendMenuW(edges, MF_STRING, IDM_EDGE_LEFT, L"Left");
    AppendMenuW(edges, MF_STRING, IDM_EDGE_RIGHT, L"Right");
    CheckMenuRadioItem(edges, IDM_EDGE_BOTTOM, IDM_EDGE_RIGHT,
                       state.edge == DockEdge::Top     ? IDM_EDGE_TOP
                       : state.edge == DockEdge::Left  ? IDM_EDGE_LEFT
                       : state.edge == DockEdge::Right ? IDM_EDGE_RIGHT
                                                       : IDM_EDGE_BOTTOM,
                       MF_BYCOMMAND);

    HMENU themes = CreatePopupMenu();
    AppendMenuW(themes, MF_STRING, IDM_THEME_AUTO, L"Follow Windows");
    AppendMenuW(themes, MF_STRING, IDM_THEME_DARK, L"Dark");
    AppendMenuW(themes, MF_STRING, IDM_THEME_LIGHT, L"Light");
    CheckMenuRadioItem(themes, IDM_THEME_AUTO, IDM_THEME_LIGHT,
                       state.theme == Theme::Dark    ? IDM_THEME_DARK
                       : state.theme == Theme::Light ? IDM_THEME_LIGHT
                                                     : IDM_THEME_AUTO,
                       MF_BYCOMMAND);

    // Opacità a riposo: quattro scatti bastano, e chi vuole un valore preciso
    // ha comunque l'INI.
    HMENU opacity  = CreatePopupMenu();
    UINT  opacityId = IDM_OPACITY_FIRST;
    UINT  opacityChecked = IDM_OPACITY_FIRST;
    int   bestGap  = 1000;
    for (const auto& step : kIdleSteps) {
        wchar_t label[16]{};
        swprintf_s(label, L"%d%%", step.percent);
        AppendMenuW(opacity, MF_STRING, opacityId, label);

        const int gap = (state.idleOpacity > step.value) ? state.idleOpacity - step.value
                                                         : step.value - state.idleOpacity;
        if (gap < bestGap) {
            bestGap        = gap;
            opacityChecked = opacityId;
        }
        ++opacityId;
    }
    CheckMenuRadioItem(opacity, IDM_OPACITY_FIRST, opacityId - 1, opacityChecked, MF_BYCOMMAND);

    const std::wstring who     = state.playerName.empty() ? L"the player" : state.playerName;
    const bool         overlay = state.mode == BarMode::Overlay;

    AppendMenuW(menu, MF_STRING, IDM_BAR_TOGGLE, state.barVisible ? L"Hide bar" : L"Show bar");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(modes), L"Mode");
    AppendMenuW(menu, MF_POPUP | (overlay ? MF_ENABLED : MF_GRAYED),
                reinterpret_cast<UINT_PTR>(looks), L"Look");
    AppendMenuW(menu, MF_POPUP | (state.mode == BarMode::Docked ? MF_ENABLED : MF_GRAYED),
                reinterpret_cast<UINT_PTR>(edges), L"Edge");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(players), L"Player");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(themes), L"Theme");
    AppendMenuW(menu, MF_POPUP | (overlay ? MF_ENABLED : MF_GRAYED),
                reinterpret_cast<UINT_PTR>(opacity), L"Idle opacity");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (state.autoMinimize ? MF_CHECKED : MF_UNCHECKED),
                IDM_AUTOMIN_TOGGLE, (L"Minimize " + who).c_str());
    AppendMenuW(menu, MF_STRING, IDM_PLAYER_RESTORE, (L"Restore " + who).c_str());
    AppendMenuW(menu, MF_STRING | (state.pauseOnSwitch ? MF_CHECKED : MF_UNCHECKED),
                IDM_PAUSE_TOGGLE, L"Pause the player you leave");
    AppendMenuW(menu, MF_STRING | (state.playOnSwitch ? MF_CHECKED : MF_UNCHECKED),
                IDM_PLAY_TOGGLE, L"Play the player you pick");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (state.autostart ? MF_CHECKED : MF_UNCHECKED), IDM_AUTOSTART,
                L"Start with Windows");
    AppendMenuW(menu, MF_STRING, IDM_OPEN_INI, L"Open minibar.ini");
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

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

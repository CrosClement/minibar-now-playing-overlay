#include "appbar.h"

#include <shellapi.h>

namespace {

UINT ToAbeEdge(DockEdge e) {
    switch (e) {
        case DockEdge::Left: return ABE_LEFT;
        case DockEdge::Top: return ABE_TOP;
        case DockEdge::Right: return ABE_RIGHT;
        default: return ABE_BOTTOM;
    }
}

}  // namespace

bool AppBar::Register(HWND hwnd, bool reserveSpace) {
    if (registered_) return true;
    hwnd_     = hwnd;
    reserves_ = reserveSpace;

    APPBARDATA abd{sizeof(abd)};
    abd.hWnd             = hwnd;
    abd.uCallbackMessage = WM_APP_APPBAR;
    registered_          = SHAppBarMessage(ABM_NEW, &abd) != 0;
    return registered_;
}

void AppBar::Unregister() {
    if (!registered_) return;
    APPBARDATA abd{sizeof(abd)};
    abd.hWnd = hwnd_;
    SHAppBarMessage(ABM_REMOVE, &abd);
    registered_ = false;
}

void AppBar::Reposition(DockEdge edge, int thicknessPx) {
    if (!registered_ || !hwnd_ || !reserves_) return;
    edge_      = edge;
    thickness_ = thicknessPx;

    MONITORINFO mi{sizeof(mi)};
    HMONITOR    mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(mon, &mi)) return;
    const RECT screen = mi.rcMonitor;

    APPBARDATA abd{sizeof(abd)};
    abd.hWnd  = hwnd_;
    abd.uEdge = ToAbeEdge(edge);

    switch (abd.uEdge) {
        case ABE_TOP:
            abd.rc = {screen.left, screen.top, screen.right, screen.top + thicknessPx};
            break;
        case ABE_BOTTOM:
            abd.rc = {screen.left, screen.bottom - thicknessPx, screen.right, screen.bottom};
            break;
        case ABE_LEFT:
            abd.rc = {screen.left, screen.top, screen.left + thicknessPx, screen.bottom};
            break;
        default:
            abd.rc = {screen.right - thicknessPx, screen.top, screen.right, screen.bottom};
            break;
    }

    // QUERYPOS aggiusta il rect per non sovrapporsi ad altre appbar (taskbar).
    SHAppBarMessage(ABM_QUERYPOS, &abd);
    switch (abd.uEdge) {
        case ABE_TOP: abd.rc.bottom = abd.rc.top + thicknessPx; break;
        case ABE_BOTTOM: abd.rc.top = abd.rc.bottom - thicknessPx; break;
        case ABE_LEFT: abd.rc.right = abd.rc.left + thicknessPx; break;
        default: abd.rc.left = abd.rc.right - thicknessPx; break;
    }
    SHAppBarMessage(ABM_SETPOS, &abd);

    MoveWindow(hwnd_, abd.rc.left, abd.rc.top, abd.rc.right - abd.rc.left,
               abd.rc.bottom - abd.rc.top, TRUE);
}

void AppBar::OnCallback(WPARAM notification) {
    if (!reserves_) return;  // in modalità taskbar ci pensa App
    switch (notification) {
        case ABN_POSCHANGED:
        case ABN_FULLSCREENAPP:
        case ABN_STATECHANGE:
            Reposition(edge_, thickness_);
            break;
        default:
            break;
    }
}

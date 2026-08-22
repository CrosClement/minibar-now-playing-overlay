#include "foreground.h"

#include <dwmapi.h>

HWND ForegroundWatch::s_target = nullptr;

void CALLBACK ForegroundWatch::Proc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG,
                                    DWORD, DWORD) {
    if (event != EVENT_SYSTEM_FOREGROUND || idObject != OBJID_WINDOW || !hwnd) return;
    if (s_target && IsWindow(s_target)) PostMessageW(s_target, WM_APP_FOREGROUND, 0, 0);
}

void ForegroundWatch::Start(HWND target) {
    if (hook_) return;
    s_target = target;
    hook_    = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, Proc, 0, 0,
                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void ForegroundWatch::Stop() {
    if (hook_) {
        UnhookWinEvent(hook_);
        hook_ = nullptr;
    }
    s_target = nullptr;
}

void ForegroundWatch::AssertTopmost(HWND hwnd) {
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

bool ForegroundWatch::IsCovered(HWND hwnd) {
    RECT self{};
    if (!GetWindowRect(hwnd, &self)) return false;

    // GW_HWNDPREV cammina verso l'alto della z-order.
    for (HWND h = GetWindow(hwnd, GW_HWNDPREV); h != nullptr; h = GetWindow(h, GW_HWNDPREV)) {
        if (h == hwnd) break;
        if (!IsWindowVisible(h)) continue;
        if (IsIconic(h)) continue;

        RECT other{}, inter{};
        if (!GetWindowRect(h, &other)) continue;
        if (!IntersectRect(&inter, &self, &other)) continue;

        // Le UWP sospese restano nella z-order ma sono "cloaked": non coprono.
        BOOL cloaked = FALSE;
        if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
            continue;

        return true;
    }
    return false;
}

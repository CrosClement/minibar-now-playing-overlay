// window.h — creazione finestra, stili estesi, click-through, DPI, tray, menu.
#pragma once
#include "common.h"
#include "config.h"

namespace win {

bool RegisterBarClass(HINSTANCE inst, WNDPROC proc, HICON icon);

// WS_POPUP + topmost/noactivate/toolwindow + layered: niente focus, niente
// Alt-Tab, niente taskbar, alpha per-pixel via UpdateLayeredWindow.
HWND CreateBarWindow(HINSTANCE inst, void* userData, int widthPx, int heightPx);

void SetClickThrough(HWND hwnd, bool on);
UINT DpiFor(HWND hwnd);
void ShowNoActivate(HWND hwnd);

RECT WorkAreaFor(HWND hwnd);

// Stato della taskbar, letto da fuori: nessuna injection, solo GetWindowRect
// sulle sue finestre pubbliche e due valori di registro in sola lettura.
struct TaskbarInfo {
    bool found      = false;
    RECT rect{};        // Shell_TrayWnd
    RECT tray{};        // TrayNotifyWnd (area di notifica, bordo destro utile)
    bool horizontal = true;
    bool onScreen   = true;  // falso quando l'auto-hide l'ha ritirata
    int  alignment  = 1;     // TaskbarAl: 0 = app a sinistra, 1 = app al centro
};
TaskbarInfo QueryTaskbar();

// Tema chiaro di sistema: in modalità taskbar serve la palette scura.
bool SystemUsesLightTheme();

bool AddTrayIcon(HWND hwnd, HICON icon);
void RemoveTrayIcon(HWND hwnd);

struct MenuState {
    BarMode       mode         = BarMode::Overlay;
    OverlayLayout layout       = OverlayLayout::Bar;
    bool          autoMinimize = true;
    bool          barVisible   = true;
};

// Ritorna l'id del comando scelto, 0 se annullato.
UINT ShowContextMenu(HWND hwnd, POINT screenPt, const MenuState& state);

}  // namespace win

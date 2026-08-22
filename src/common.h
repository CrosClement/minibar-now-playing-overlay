// common.h — definizioni condivise da tutti i moduli.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

// Messaggi interni. Gli handler SMTC girano su thread del threadpool: non
// toccano mai lo stato, impacchettano i dati e li rimandano al thread UI.
enum : UINT {
    WM_APP_SESSION    = WM_APP + 1,  // la sessione corrente è cambiata
    WM_APP_MEDIA      = WM_APP + 2,  // lParam = MediaUpdate* (di proprietà del ricevente)
    WM_APP_PLAYBACK   = WM_APP + 3,  // lParam = PlaybackUpdate*
    WM_APP_TIMELINE   = WM_APP + 4,  // lParam = TimelineUpdate*
    WM_APP_TRAY       = WM_APP + 5,  // callback icona di notifica
    WM_APP_APPBAR     = WM_APP + 6,  // callback SHAppBarMessage
    WM_APP_FOREGROUND = WM_APP + 7,  // EVENT_SYSTEM_FOREGROUND
};

// Comandi del menu contestuale / tray.
enum : UINT {
    IDM_MODE_OVERLAY   = 1001,
    IDM_MODE_DOCKED    = 1002,
    IDM_MODE_TASKBAR   = 1003,
    IDM_LAYOUT_BAR     = 1004,
    IDM_LAYOUT_TILE    = 1005,
    IDM_AUTOMIN_TOGGLE = 1006,
    IDM_BAR_TOGGLE     = 1007,
    IDM_PLAYER_RESTORE = 1008,
    IDM_OPEN_INI       = 1009,
    IDM_EXIT           = 1010,
    IDM_PAUSE_TOGGLE   = 1011,
    IDM_AUTOSTART      = 1012,
    IDM_PLAY_TOGGLE    = 1013,

    IDM_THEME_AUTO  = 1020,
    IDM_THEME_DARK  = 1021,
    IDM_THEME_LIGHT = 1022,

    // Opacità a riposo: quattro scatti, il valore vero sta in kIdleSteps.
    IDM_OPACITY_FIRST = 1030,
    IDM_OPACITY_LAST  = 1033,

    // Selettore di sessione: la voce automatica più una per player attivo.
    IDM_SESSION_AUTO  = 1100,
    IDM_SESSION_FIRST = 1101,
    IDM_SESSION_LAST  = 1132,
};

// Timer.
enum : UINT_PTR {
    IDT_CURSOR  = 1,  // polling hover, 10 Hz
    IDT_TICK    = 2,  // avanzamento progress bar, 1 Hz
    IDT_TOPMOST = 3,  // fallback z-order, 7 s
    IDT_ANIM    = 4,  // transizioni brevi: opacità, copertina, pillola taskbar
    IDT_UNHOVER = 5,  // isteresi uscita hover (one-shot)
    IDT_PLAYER  = 6,  // applicazione ritardata della policy sul player (one-shot)
    IDT_VOLUME  = 7,  // fine del lampeggio del volume (one-shot)
    IDT_MARQUEE = 8,  // scorrimento del titolo lungo, solo durante l'hover
    IDT_REVEAL  = 9,  // fine del "eccomi" a opacità piena dopo avvio/riapertura (one-shot)
};

constexpr UINT kTrayIconId = 1;
constexpr int  kHotkeyId   = 1;

// Scatti di opacità a riposo offerti dal menu, in percentuale e in 0-255.
struct IdleStep {
    int percent;
    int value;
};
constexpr IdleStep kIdleSteps[] = {{25, 64}, {55, 140}, {80, 204}, {100, 255}};

// Directory dell'eseguibile, con backslash finale. L'app è portable: tutto
// (INI compreso) vive accanto al binario.
std::wstring ExeDir();

// Confronto case-insensitive su sottostringa (per SourceAppUserModelId).
bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle);

// Uguaglianza case-insensitive, per confrontare due SourceAppUserModelId.
bool EqualsNoCase(const std::wstring& a, const std::wstring& b);

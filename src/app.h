// app.h — stato centrale e orchestrazione. Nessuna logica di piattaforma qui
// dentro se non il minimo per instradare i messaggi ai moduli.
#pragma once
#include "common.h"

#include "appbar.h"
#include "config.h"
#include "foreground.h"
#include "render.h"
#include "smtc.h"
#include "spotify.h"
#include "volume.h"

class App {
public:
    bool Init(HINSTANCE inst);
    void Shutdown();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    LRESULT Handle(UINT msg, WPARAM wParam, LPARAM lParam);

    bool CreateBar();
    void DestroyBar();
    void ApplyMode(BarMode mode);
    void ApplyOverlayLayout(OverlayLayout layout);
    void ResizeToLayout();
    void PositionOverlay();
    void PositionTaskbar();
    void TrackTaskbar();
    void Redraw();
    void SyncTimers();

    void OnCursorTick();
    void OnFadeTick();
    void SetHovered(bool hovered);
    void OnCommand(UINT id);
    void ToggleBarVisible();
    void ApplySpotifyPolicy();

    // Mostra il livello sulla striscia della progress bar per un attimo.
    void FlashVolume(float level);
    bool NudgeVolume(int wheelDelta);

    double      ShownPosition() const;
    LayoutSpec  BuildLayout() const;
    RenderState BuildState() const;

    HINSTANCE inst_ = nullptr;
    HWND      hwnd_ = nullptr;
    HICON     icon_ = nullptr;
    UINT      dpi_  = 96;

    Config          cfg_;
    Renderer        renderer_;
    SmtcService     smtc_;
    SpotifyManager  spotify_;
    SpotifyVolume   spotifyVolume_;
    ForegroundWatch foreground_;
    AppBar          appbar_;

    // Traccia e riproduzione.
    std::wstring         title_;
    std::wstring         artist_;
    std::vector<uint8_t> thumb_;
    std::wstring         thumbTitle_;  // traccia a cui appartiene la copertina in cache
    bool                 hasTrack_  = false;
    bool                 playing_   = false;
    bool                 canPrev_   = false;
    bool                 canNext_   = false;
    bool                 canToggle_ = false;
    double               posSec_    = 0.0;
    double               durSec_    = 0.0;
    ULONGLONG            baseTick_  = 0;  // GetTickCount64 corrispondente a posSec_

    // Interazione.
    bool  hovered_       = false;
    bool  unhoverArmed_  = false;
    float opacity_       = 1.0f;
    float targetOpacity_ = 1.0f;
    int   pressedArea_   = 0;
    bool  dragging_      = false;
    POINT dragCursor_{};
    POINT dragOrigin_{};

    // Volume di Spotify: livello mostrato e istante in cui smette di mostrarsi.
    float     volumeLevel_      = -1.f;
    ULONGLONG volumeShownUntil_ = 0;

    // Modalità taskbar.
    RECT  lastTaskbarRect_{};
    bool  taskbarOnScreen_  = true;
    bool  fullscreenApp_    = false;
    float taskbarHeightDip_ = 42.f;
    float dockedWidthDip_   = 1920.f;
    bool  lightUi_          = false;

    bool barVisible_       = true;
    bool quitting_         = false;
    bool spotifyApplied_   = false;
    bool hotkeyRegistered_ = false;
};

// app.h — stato centrale e orchestrazione. Nessuna logica di piattaforma qui
// dentro se non il minimo per instradare i messaggi ai moduli.
#pragma once
#include "common.h"

#include "appbar.h"
#include "config.h"
#include "foreground.h"
#include "player.h"
#include "render.h"
#include "smtc.h"
#include "startup.h"
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
    void SetHovered(bool hovered);

    // Transizioni brevi (opacità, copertina, pillola della taskbar): un solo
    // timer, acceso solo mentre c'è qualcosa da muovere.
    void StartAnim();
    void OnAnimTick();

    // Accende o spegne lo scorrimento del titolo e lo fa ripartire da capo.
    void SyncMarquee();

    // Cambia la copertina facendola comparire in dissolvenza su quella di prima.
    void SwapThumbnail(const std::vector<uint8_t>& bytes);

    // Rilegge chiaro/scuro dal sistema (o dall'INI, se lo forza).
    void RefreshTheme();
    void ShowMenu(POINT screenPt);
    void OnCommand(UINT id);
    void ToggleBarVisible();
    void ApplyPlayerPolicy();

    // Player su cui agiscono volume e minimizzazione: la sessione SMTC in corso
    // se c'è, altrimenti la stringa preferred_app dell'INI.
    std::wstring PlayerKey() const;
    void         SelectPlayer(const std::wstring& appId);

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
    PlayerManager   player_;
    PlayerVolume    volume_;
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
    bool      hovered_      = false;
    bool      unhoverArmed_ = false;
    bool      marqueeOn_    = false;
    ULONGLONG marqueeStart_ = 0;
    float opacity_       = 1.0f;
    float targetOpacity_ = 1.0f;

    // Apertura della pillola nella taskbar, 0-1, e dissolvenza della copertina.
    float     expand_      = 0.f;
    float     expandTarget_ = 0.f;
    bool      artFading_   = false;
    ULONGLONG artFadeStart_ = 0;
    int   pressedArea_   = 0;
    bool  dragging_      = false;
    POINT dragCursor_{};
    POINT dragOrigin_{};

    // Volume del player: livello mostrato e istante in cui smette di mostrarsi.
    float     volumeLevel_      = -1.f;
    ULONGLONG volumeShownUntil_ = 0;

    // Modalità taskbar.
    RECT  lastTaskbarRect_{};
    bool  taskbarOnScreen_  = true;
    bool  fullscreenApp_    = false;
    float taskbarHeightDip_ = 42.f;
    float dockedWidthDip_   = 1920.f;
    bool  lightTaskbar_     = false;  // colore della taskbar sotto la pillola
    bool  lightTheme_       = false;  // tema con cui la barretta disegna sé stessa

    bool barVisible_       = true;
    bool quitting_         = false;
    bool playerApplied_    = false;
    bool hotkeyRegistered_ = false;
};

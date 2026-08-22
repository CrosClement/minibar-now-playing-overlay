#include "app.h"

#include "resource.h"
#include "window.h"

#include <shellapi.h>
#include <windowsx.h>

#include <memory>

namespace {

// Quanto resta visibile il livello del volume sulla striscia della progress bar.
constexpr UINT kVolumeFlashMs = 1400;

int ClampToVirtualScreen(int value, int size, int origin, int extent) {
    const int minV = origin - size + 40;  // sempre almeno una fetta visibile
    const int maxV = origin + extent - 40;
    if (value < minV) return minV;
    if (value > maxV) return maxV;
    return value;
}

}  // namespace

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs    = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self        = static_cast<App*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->Handle(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool App::Init(HINSTANCE inst) {
    inst_ = inst;
    cfg_.Load();

    icon_ = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_MINIBAR), IMAGE_ICON, 0, 0,
                                          LR_DEFAULTSIZE | LR_SHARED));
    if (!icon_) icon_ = LoadIconW(nullptr, IDI_APPLICATION);

    if (!win::RegisterBarClass(inst, WndProc, icon_)) return false;

    // Se un'esecuzione precedente è morta con un player nascosto, si recupera qui.
    player_.RecoverOrphan();

    if (!CreateBar()) return false;

    smtc_.Start(hwnd_, cfg_.preferredApp);

    // Un attimo di ritardo: serve a dare tempo alla sessione SMTC di arrivare
    // (è lei a dire quale sia il player) e alla finestra di comparire se il
    // player è appena partito.
    SetTimer(hwnd_, IDT_PLAYER, 1200, nullptr);
    return true;
}

void App::Shutdown() {
    smtc_.Stop();
    player_.Restore();  // garantito: mai lasciare una finestra nascosta orfana
    quitting_ = true;
    DestroyBar();
}

// ------------------------------------------------------------ ciclo di vita --
bool App::CreateBar() {
    lightUi_ = win::SystemUsesLightTheme();

    const UINT dpiGuess = GetDpiForSystem() ? GetDpiForSystem() : 96;
    const SIZE guess    = Renderer::Measure(BuildLayout(), dpiGuess);

    hwnd_ = win::CreateBarWindow(inst_, this, guess.cx, guess.cy);
    if (!hwnd_) return false;
    dpi_ = win::DpiFor(hwnd_);

    switch (cfg_.mode) {
        case BarMode::Docked:
            appbar_.Register(hwnd_, true);
            appbar_.Reposition(cfg_.edge, Renderer::Measure(BuildLayout(), dpi_).cy);
            win::SetClickThrough(hwnd_, false);
            opacity_ = targetOpacity_ = 1.0f;
            break;

        case BarMode::Taskbar:
            // Registrazione senza riservare spazio: serve solo per sapere quando
            // la taskbar si sposta, si nasconde o parte un'app a schermo intero.
            appbar_.Register(hwnd_, false);
            win::SetClickThrough(hwnd_, false);
            opacity_ = targetOpacity_ = 1.0f;
            PositionTaskbar();
            foreground_.Start(hwnd_);
            break;

        default:
            ResizeToLayout();
            PositionOverlay();
            win::SetClickThrough(hwnd_, true);
            opacity_ = targetOpacity_ = cfg_.idleOpacity / 255.0f;
            foreground_.Start(hwnd_);
            break;
    }

    if (!renderer_.Init(hwnd_, dpi_)) return false;

    win::AddTrayIcon(hwnd_, icon_);
    if (cfg_.hotkeyEnabled)
        hotkeyRegistered_ =
            RegisterHotKey(hwnd_, kHotkeyId, cfg_.hotkeyMods | MOD_NOREPEAT, cfg_.hotkeyVk) != 0;

    if (barVisible_) win::ShowNoActivate(hwnd_);

    SyncTimers();
    Redraw();
    return true;
}

void App::DestroyBar() {
    if (!hwnd_) return;

    foreground_.Stop();
    appbar_.Unregister();
    if (hotkeyRegistered_) {
        UnregisterHotKey(hwnd_, kHotkeyId);
        hotkeyRegistered_ = false;
    }
    for (UINT_PTR id : {IDT_CURSOR, IDT_TICK, IDT_TOPMOST, IDT_FADE, IDT_UNHOVER, IDT_PLAYER,
                        IDT_VOLUME, IDT_MARQUEE})
        KillTimer(hwnd_, id);
    unhoverArmed_ = false;
    hovered_      = false;
    marqueeOn_    = false;

    win::RemoveTrayIcon(hwnd_);
    renderer_.Shutdown();

    HWND doomed = hwnd_;
    DestroyWindow(doomed);
    hwnd_ = nullptr;
}

void App::ApplyMode(BarMode mode) {
    if (mode == cfg_.mode) return;
    cfg_.SaveMode(mode);

    DestroyBar();
    if (!CreateBar()) {
        PostQuitMessage(1);
        return;
    }
    smtc_.Rebind(hwnd_);
}

void App::ApplyOverlayLayout(OverlayLayout layout) {
    if (cfg_.mode != BarMode::Overlay || cfg_.overlayLayout == layout) return;
    cfg_.SaveOverlayLayout(layout);
    ResizeToLayout();
    Redraw();
}

// ---------------------------------------------------------- posizionamento --
void App::ResizeToLayout() {
    if (!hwnd_ || cfg_.mode != BarMode::Overlay) return;
    const SIZE size = Renderer::Measure(BuildLayout(), dpi_);
    SetWindowPos(hwnd_, nullptr, 0, 0, size.cx, size.cy,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void App::PositionOverlay() {
    const SIZE size = Renderer::Measure(BuildLayout(), dpi_);

    int x, y;
    if (cfg_.x != INT_MIN && cfg_.y != INT_MIN) {
        x = cfg_.x;
        y = cfg_.y;
    } else {
        const RECT wa = win::WorkAreaFor(hwnd_);
        x             = wa.right - size.cx - 24;
        y             = wa.bottom - size.cy - 24;
    }

    x = ClampToVirtualScreen(x, size.cx, GetSystemMetrics(SM_XVIRTUALSCREEN),
                             GetSystemMetrics(SM_CXVIRTUALSCREEN));
    y = ClampToVirtualScreen(y, size.cy, GetSystemMetrics(SM_YVIRTUALSCREEN),
                             GetSystemMetrics(SM_CYVIRTUALSCREEN));

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, size.cx, size.cy, SWP_NOACTIVATE);
}

void App::PositionTaskbar() {
    const auto tb = win::QueryTaskbar();
    if (!tb.found) {
        PositionOverlay();  // niente taskbar da agganciare: meglio di sparire
        return;
    }
    lastTaskbarRect_ = tb.rect;

    const LONG tbHeight = tb.rect.bottom - tb.rect.top;
    taskbarHeightDip_   = (tbHeight - 6) * 96.f / static_cast<float>(dpi_);

    const SIZE size = Renderer::Measure(BuildLayout(), dpi_);

    // Regione utile: dal bordo interno della taskbar all'inizio dell'area di
    // notifica. Le icone delle app non sono enumerabili sulla taskbar XAML di
    // Windows 11, quindi ci si regola sull'allineamento dichiarato dal sistema.
    LONG left  = tb.rect.left + 8;
    LONG right = ((tb.tray.left > tb.rect.left + 40) ? tb.tray.left : tb.rect.right) - 8;
    if (right - left < size.cx) right = left + size.cx;

    TaskbarAnchor anchor = cfg_.taskbarAnchor;
    if (anchor == TaskbarAnchor::Auto)
        anchor = (tb.alignment == 1) ? TaskbarAnchor::Left   // app al centro -> noi a sinistra
                                     : TaskbarAnchor::Center;  // app a sinistra -> noi al centro

    LONG x;
    switch (anchor) {
        case TaskbarAnchor::Left: x = left; break;
        case TaskbarAnchor::Right: x = right - size.cx; break;
        default: x = (left + right) / 2 - size.cx / 2; break;
    }
    if (x < left) x = left;
    if (x + size.cx > right) x = right - size.cx;

    const LONG y = tb.rect.top + (tbHeight - size.cy) / 2;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, size.cx, size.cy, SWP_NOACTIVATE);
}

void App::TrackTaskbar() {
    if (cfg_.mode != BarMode::Taskbar || !hwnd_) return;

    const auto tb = win::QueryTaskbar();
    if (!tb.found) return;

    const bool visible = tb.onScreen && !fullscreenApp_;
    if (visible != taskbarOnScreen_) {
        taskbarOnScreen_ = visible;
        if (barVisible_) ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    if (visible && !EqualRect(&tb.rect, &lastTaskbarRect_)) PositionTaskbar();
}

// ------------------------------------------------------------------- stato --
LayoutSpec App::BuildLayout() const {
    LayoutSpec spec;
    switch (cfg_.mode) {
        case BarMode::Docked:
            spec.kind     = LayoutKind::Docked;
            spec.widthDip = dockedWidthDip_;
            break;
        case BarMode::Taskbar:
            spec.kind      = LayoutKind::Taskbar;
            spec.heightDip = taskbarHeightDip_;
            spec.expanded  = hovered_;
            break;
        default:
            spec.kind = (cfg_.overlayLayout == OverlayLayout::Tile) ? LayoutKind::Tile
                                                                   : LayoutKind::Bar;
            spec.widthDip = static_cast<float>(cfg_.OverlaySize());
            break;
    }
    return spec;
}

double App::ShownPosition() const {
    double p = posSec_;
    if (playing_) {
        const ULONGLONG now = GetTickCount64();
        if (now > baseTick_) p += static_cast<double>(now - baseTick_) / 1000.0;
    }
    if (p < 0.0) p = 0.0;
    if (durSec_ > 0.0 && p > durSec_) p = durSec_;
    return p;
}

RenderState App::BuildState() const {
    RenderState st;
    st.layout    = BuildLayout();
    st.title     = title_;
    st.artist    = artist_;
    st.playing   = playing_;
    st.hovered   = hovered_;
    st.canPrev   = canPrev_;
    st.canNext   = canNext_;
    st.canToggle = canToggle_;
    st.hasTrack  = hasTrack_;
    st.lightUi   = lightUi_;
    st.posSec    = ShownPosition();
    st.durSec    = durSec_;
    st.opacity   = opacity_;
    st.pressed   = pressedArea_;
    st.volume    = (GetTickCount64() < volumeShownUntil_) ? volumeLevel_ : -1.f;
    st.marqueeMs = marqueeOn_ ? static_cast<double>(GetTickCount64() - marqueeStart_) : -1.0;
    return st;
}

void App::SyncMarquee() {
    if (!hwnd_) return;

    // Il titolo scorre solo se sborda davvero e solo mentre il mouse è sopra:
    // fuori dall'hover il timer non esiste, quindi a riposo non si ridisegna
    // niente. Ogni chiamata fa ripartire la corsa da capo — è quello che serve
    // sia entrando in hover sia al cambio traccia.
    const bool want = hovered_ && barVisible_ && renderer_.Valid() &&
                      renderer_.TitleOverflow(BuildState()) > 0.f;

    if (want) {
        marqueeOn_    = true;
        marqueeStart_ = GetTickCount64();
        SetTimer(hwnd_, IDT_MARQUEE, 33, nullptr);
    } else if (marqueeOn_) {
        marqueeOn_ = false;
        KillTimer(hwnd_, IDT_MARQUEE);
    }
}

void App::Redraw() {
    if (!hwnd_ || !barVisible_ || !renderer_.Valid()) return;
    renderer_.Draw(BuildState());
}

void App::SyncTimers() {
    if (!hwnd_) return;

    // Il tick a 1 Hz esiste solo per far avanzare la progress bar: se non si
    // riproduce nulla o la barretta è nascosta, non serve a niente.
    if (playing_ && barVisible_)
        SetTimer(hwnd_, IDT_TICK, 1000, nullptr);
    else
        KillTimer(hwnd_, IDT_TICK);

    if (cfg_.mode != BarMode::Docked && barVisible_)
        SetTimer(hwnd_, IDT_TOPMOST, 7000, nullptr);
    else
        KillTimer(hwnd_, IDT_TOPMOST);

    if (barVisible_)
        SetTimer(hwnd_, IDT_CURSOR, 100, nullptr);
    else
        KillTimer(hwnd_, IDT_CURSOR);
}

// ------------------------------------------------------------ interazione --
void App::SetHovered(bool hovered) {
    if (hovered_ == hovered) return;
    hovered_ = hovered;

    switch (cfg_.mode) {
        case BarMode::Overlay:
            // Il paradosso del click-through: una finestra WS_EX_TRANSPARENT non
            // riceve eventi mouse, quindi l'hover si scopre col polling e lo
            // stile si toglie solo quando serve davvero.
            win::SetClickThrough(hwnd_, !hovered);
            targetOpacity_ = hovered ? 1.0f : cfg_.idleOpacity / 255.0f;
            SetTimer(hwnd_, IDT_FADE, 16, nullptr);
            break;

        case BarMode::Taskbar:
            PositionTaskbar();  // in hover la pillola si allarga per il testo
            break;

        default:
            break;
    }
    if (!hovered) pressedArea_ = 0;
    SyncMarquee();
    Redraw();
}

void App::OnCursorTick() {
    TrackTaskbar();

    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;

    RECT r{};
    if (!GetWindowRect(hwnd_, &r)) return;
    const bool inside = PtInRect(&r, cursor) != 0;

    if (inside) {
        if (unhoverArmed_) {
            KillTimer(hwnd_, IDT_UNHOVER);
            unhoverArmed_ = false;
        }
        if (!hovered_) SetHovered(true);
    } else if (hovered_ && !unhoverArmed_ && !dragging_ && !pressedArea_) {
        SetTimer(hwnd_, IDT_UNHOVER, 250, nullptr);  // isteresi: niente sfarfallio
        unhoverArmed_ = true;
    }
}

void App::OnFadeTick() {
    constexpr float kStep = 0.12f;
    const float     diff  = targetOpacity_ - opacity_;
    if (diff > kStep)
        opacity_ += kStep;
    else if (diff < -kStep)
        opacity_ -= kStep;
    else {
        opacity_ = targetOpacity_;
        KillTimer(hwnd_, IDT_FADE);
    }
    Redraw();
}

void App::ToggleBarVisible() {
    barVisible_ = !barVisible_;
    if (barVisible_) {
        if (cfg_.mode != BarMode::Taskbar || taskbarOnScreen_) win::ShowNoActivate(hwnd_);
        SyncTimers();
        Redraw();
    } else {
        ShowWindow(hwnd_, SW_HIDE);
        hovered_ = false;
        SyncTimers();
        SyncMarquee();
    }
}

std::wstring App::PlayerKey() const {
    const std::wstring current = smtc_.CurrentAppId();
    return current.empty() ? cfg_.preferredApp : current;
}

void App::ApplyPlayerPolicy() {
    if (playerApplied_ || !cfg_.autoMinimize) return;

    const std::wstring key = PlayerKey();
    if (key.empty()) return;

    if (cfg_.launchIfMissing && !PlayerManager::IsRunning(key))
        PlayerManager::Launch(key, cfg_.launchPath);
    player_.Apply(key, cfg_.hideInstead);
    playerApplied_ = true;
}

void App::SelectPlayer(const std::wstring& appId) {
    if (appId == cfg_.preferredApp) return;

    // Il player di prima resta minimizzato — è quello che si voleva — ma se era
    // nascosto va ripristinato subito: senza finestra e senza marker non
    // sarebbe più raggiungibile.
    if (player_.DidHide()) player_.Restore();

    cfg_.SavePreferredApp(appId);
    smtc_.SetPreferred(appId);

    playerApplied_ = false;
    ApplyPlayerPolicy();
    Redraw();
}

void App::FlashVolume(float level) {
    volumeLevel_      = level;
    volumeShownUntil_ = GetTickCount64() + kVolumeFlashMs;
    SetTimer(hwnd_, IDT_VOLUME, kVolumeFlashMs + 40, nullptr);
    Redraw();
}

// Vero se il volume del player è stato mosso davvero: se il player non ha una
// sessione audio (chiuso, o che non ha mai suonato) la rotellina resta libera
// per il ridimensionamento.
bool App::NudgeVolume(int wheelDelta) {
    const float step  = static_cast<float>(cfg_.volumeStep) / 100.f;
    const float level = volume_.Nudge(PlayerKey(), wheelDelta > 0 ? step : -step);
    if (level < 0.f) return false;
    FlashVolume(level);
    return true;
}

void App::ShowMenu(POINT screenPt) {
    win::MenuState ms;
    ms.mode         = cfg_.mode;
    ms.layout       = cfg_.overlayLayout;
    ms.autoMinimize = cfg_.autoMinimize;
    ms.barVisible   = barVisible_;
    ms.sessions     = smtc_.ListSessions();
    ms.preferred    = cfg_.preferredApp;
    ms.playerName   = PlayerManager::FriendlyName(PlayerKey());

    const UINT cmd = win::ShowContextMenu(hwnd_, screenPt, ms);

    if (cmd == IDM_SESSION_AUTO) {
        SelectPlayer({});  // nessun vincolo: si segue la sessione di sistema
    } else if (cmd >= IDM_SESSION_FIRST && cmd <= IDM_SESSION_LAST) {
        const size_t index = cmd - IDM_SESSION_FIRST;
        if (index < ms.sessions.size()) SelectPlayer(ms.sessions[index]);
    } else {
        OnCommand(cmd);
    }
}

void App::OnCommand(UINT id) {
    switch (id) {
        case IDM_BAR_TOGGLE: ToggleBarVisible(); break;

        case IDM_MODE_OVERLAY: ApplyMode(BarMode::Overlay); break;
        case IDM_MODE_DOCKED: ApplyMode(BarMode::Docked); break;
        case IDM_MODE_TASKBAR: ApplyMode(BarMode::Taskbar); break;

        case IDM_LAYOUT_BAR: ApplyOverlayLayout(OverlayLayout::Bar); break;
        case IDM_LAYOUT_TILE: ApplyOverlayLayout(OverlayLayout::Tile); break;

        case IDM_AUTOMIN_TOGGLE:
            cfg_.SaveAutoMinimize(!cfg_.autoMinimize);
            playerApplied_ = false;
            if (cfg_.autoMinimize)
                ApplyPlayerPolicy();
            else
                player_.Restore();
            break;

        case IDM_PLAYER_RESTORE:
            player_.Restore();
            playerApplied_ = false;
            break;

        case IDM_OPEN_INI:
            ShellExecuteW(nullptr, L"open", L"notepad.exe", cfg_.Path().c_str(), nullptr,
                          SW_SHOWNORMAL);
            break;

        case IDM_EXIT:
            quitting_ = true;
            DestroyBar();
            PostQuitMessage(0);
            break;

        default: break;
    }
}

// ---------------------------------------------------------------- messaggi --
LRESULT App::Handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_APP_SESSION:
            smtc_.SelectSession();
            return 0;

        case WM_APP_MEDIA: {
            std::unique_ptr<MediaUpdate> up(reinterpret_cast<MediaUpdate*>(lParam));
            if (!up) {  // nessuna sessione: la barretta si svuota
                hasTrack_ = false;
                title_.clear();
                artist_.clear();
                thumb_.clear();
                thumbTitle_.clear();
                renderer_.SetThumbnail({});
                SyncMarquee();
                Redraw();
                return 0;
            }
            if (!smtc_.AcceptGeneration(static_cast<uint32_t>(wParam))) return 0;

            // SMTC rimanda gli stessi metadati più volte per la stessa traccia
            // (la copertina arriva dopo): la corsa del titolo riparte solo se
            // il titolo è davvero un altro, altrimenti in hover salterebbe.
            const bool titleChanged = (title_ != up->title);

            title_    = up->title;
            artist_   = up->artist;
            hasTrack_ = !title_.empty();

            if (up->hasThumb) {
                if (up->thumb != thumb_) {
                    thumb_      = std::move(up->thumb);
                    thumbTitle_ = title_;
                    renderer_.SetThumbnail(thumb_);
                }
            } else if (!thumb_.empty() && thumbTitle_ != title_) {
                // Thumbnail ancora assente per la nuova traccia: placeholder,
                // riproveremo al prossimo MediaPropertiesChanged.
                thumb_.clear();
                thumbTitle_.clear();
                renderer_.SetThumbnail({});
            }
            if (titleChanged) SyncMarquee();  // titolo nuovo: la corsa riparte da capo
            Redraw();
            return 0;
        }

        case WM_APP_PLAYBACK: {
            std::unique_ptr<PlaybackUpdate> up(reinterpret_cast<PlaybackUpdate*>(lParam));
            if (!up || !smtc_.AcceptGeneration(static_cast<uint32_t>(wParam))) return 0;

            if (up->playing != playing_) {
                // Congela o riparti dalla posizione mostrata, senza salti.
                posSec_   = ShownPosition();
                baseTick_ = GetTickCount64();
                playing_  = up->playing;
                SyncTimers();
            }
            canPrev_   = up->canPrev;
            canNext_   = up->canNext;
            canToggle_ = up->canToggle;
            Redraw();
            return 0;
        }

        case WM_APP_TIMELINE: {
            std::unique_ptr<TimelineUpdate> up(reinterpret_cast<TimelineUpdate*>(lParam));
            if (!up || !smtc_.AcceptGeneration(static_cast<uint32_t>(wParam))) return 0;

            posSec_ = static_cast<double>(up->posMs) / 1000.0;
            durSec_ = static_cast<double>(up->endMs) / 1000.0;

            const ULONGLONG now = GetTickCount64();
            const ULONGLONG age = static_cast<ULONGLONG>(up->ageMs);
            baseTick_           = (now > age) ? now - age : now;
            Redraw();
            return 0;
        }

        case WM_TIMER:
            switch (wParam) {
                case IDT_CURSOR: OnCursorTick(); break;
                case IDT_TICK: Redraw(); break;
                case IDT_FADE: OnFadeTick(); break;
                case IDT_UNHOVER:
                    KillTimer(hwnd_, IDT_UNHOVER);
                    unhoverArmed_ = false;
                    SetHovered(false);
                    break;
                case IDT_TOPMOST:
                    // Mai spingere alla cieca: si ri-asserisce solo se serve.
                    if (ForegroundWatch::IsCovered(hwnd_)) ForegroundWatch::AssertTopmost(hwnd_);
                    break;
                case IDT_PLAYER:
                    KillTimer(hwnd_, IDT_PLAYER);
                    ApplyPlayerPolicy();
                    break;
                case IDT_VOLUME:
                    KillTimer(hwnd_, IDT_VOLUME);
                    Redraw();  // scaduto il lampeggio, torna la progress bar
                    break;
                case IDT_MARQUEE: Redraw(); break;
                default: break;
            }
            return 0;

        case WM_APP_FOREGROUND:
            if (cfg_.mode != BarMode::Docked && barVisible_) ForegroundWatch::AssertTopmost(hwnd_);
            return 0;

        case WM_APP_APPBAR:
            if (wParam == ABN_FULLSCREENAPP && cfg_.mode == BarMode::Taskbar) {
                fullscreenApp_ = lParam != 0;
                TrackTaskbar();
            } else if (cfg_.mode == BarMode::Taskbar) {
                PositionTaskbar();
            } else {
                appbar_.OnCallback(wParam);
            }
            return 0;

        case WM_APP_TRAY: {
            const UINT event = LOWORD(lParam);
            if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
                ShowMenu(POINT{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
            } else if (event == WM_LBUTTONDBLCLK) {
                ToggleBarVisible();
            }
            return 0;
        }

        case WM_HOTKEY:
            if (wParam == kHotkeyId) ToggleBarVisible();
            return 0;

        case WM_LBUTTONDOWN: {
            const POINT       pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const RenderState st   = BuildState();
            const HitArea     area = renderer_.HitTest(pt, st);

            if (area == HitArea::Prev || area == HitArea::PlayPause || area == HitArea::Next) {
                pressedArea_ = static_cast<int>(area);
                SetCapture(hwnd_);
                Redraw();
            } else if (area == HitArea::Drag && cfg_.mode == BarMode::Overlay) {
                dragging_ = true;
                GetCursorPos(&dragCursor_);
                RECT r{};
                GetWindowRect(hwnd_, &r);
                dragOrigin_ = POINT{r.left, r.top};
                SetCapture(hwnd_);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            if (dragging_) {
                POINT cursor{};
                GetCursorPos(&cursor);
                SetWindowPos(hwnd_, nullptr, dragOrigin_.x + (cursor.x - dragCursor_.x),
                             dragOrigin_.y + (cursor.y - dragCursor_.y), 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;

        case WM_LBUTTONUP: {
            const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            if (dragging_) {
                dragging_ = false;
                ReleaseCapture();
                RECT r{};
                if (GetWindowRect(hwnd_, &r)) cfg_.SavePosition(r.left, r.top);
                return 0;
            }

            const RenderState st = BuildState();
            if (pressedArea_) {
                ReleaseCapture();
                const HitArea area = renderer_.HitTest(pt, st);
                if (static_cast<int>(area) == pressedArea_) {
                    switch (area) {
                        case HitArea::Prev: smtc_.Previous(); break;
                        case HitArea::PlayPause: smtc_.TogglePlayPause(); break;
                        case HitArea::Next: smtc_.Next(); break;
                        default: break;
                    }
                }
                pressedArea_ = 0;
                Redraw();
                return 0;
            }

            float         frac = 0.f;
            const HitArea area = renderer_.HitTest(pt, st, &frac);
            if (area == HitArea::Progress && hasTrack_) smtc_.Seek(frac);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            // Rotellina = volume del solo player; con Ctrl (o se il player non
            // sta suonando) ridimensiona la barretta come prima. Richiede che
            // sia attivo "scorri le finestre inattive" (predefinito in Win 11).
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta == 0) return 0;

            if (!(GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) && NudgeVolume(delta)) return 0;

            if (cfg_.mode != BarMode::Overlay) return 0;
            const int step = (cfg_.overlayLayout == OverlayLayout::Tile) ? 6 : 14;
            cfg_.SaveOverlaySize(cfg_.OverlaySize() + (delta > 0 ? step : -step));
            ResizeToLayout();
            Redraw();
            return 0;
        }

        case WM_MBUTTONDOWN: {
            // Mute del player: un gesto, zero pixel di interfaccia.
            const float level = volume_.ToggleMute(PlayerKey());
            if (level >= 0.f) FlashVolume(level);
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt{};
            GetCursorPos(&pt);
            ShowMenu(pt);
            return 0;
        }

        case WM_SIZE:
            if (renderer_.Valid()) {
                renderer_.Resize(LOWORD(lParam), HIWORD(lParam), dpi_);
                SyncMarquee();  // meno spazio per il testo può voler dire scorrerlo
                Redraw();
            }
            return 0;

        case WM_DPICHANGED: {
            dpi_ = HIWORD(wParam);
            if (cfg_.mode == BarMode::Docked) {
                appbar_.Reposition(cfg_.edge, Renderer::Measure(BuildLayout(), dpi_).cy);
            } else if (cfg_.mode == BarMode::Taskbar) {
                PositionTaskbar();
            } else {
                // Il rect suggerito da Windows vale per la nuova densità.
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                const SIZE  size      = Renderer::Measure(BuildLayout(), dpi_);
                SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, size.cx, size.cy,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            renderer_.Resize(rc.right - rc.left, rc.bottom - rc.top, dpi_);
            SyncMarquee();
            Redraw();
            return 0;
        }

        case WM_SETTINGCHANGE:
            // Interessa a docked e taskbar (taskbar spostata, auto-hide, tema).
            lightUi_ = win::SystemUsesLightTheme();
            if (cfg_.mode == BarMode::Docked) {
                appbar_.Reposition(cfg_.edge, Renderer::Measure(BuildLayout(), dpi_).cy);
                Redraw();
            } else if (cfg_.mode == BarMode::Taskbar) {
                PositionTaskbar();
                Redraw();
            }
            return 0;

        case WM_DISPLAYCHANGE:
            if (cfg_.mode == BarMode::Docked)
                appbar_.Reposition(cfg_.edge, Renderer::Measure(BuildLayout(), dpi_).cy);
            else if (cfg_.mode == BarMode::Taskbar)
                PositionTaskbar();
            else if (!dragging_)
                PositionOverlay();
            Redraw();
            return 0;

        case WM_QUERYENDSESSION:
            player_.Restore();
            return TRUE;

        case WM_ENDSESSION:
            player_.Restore();
            return 0;

        case WM_CLOSE:
            quitting_ = true;
            DestroyBar();
            PostQuitMessage(0);
            return 0;

        case WM_DESTROY:
            if (quitting_) PostQuitMessage(0);
            return 0;

        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

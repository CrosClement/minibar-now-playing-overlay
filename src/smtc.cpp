#include "smtc.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <atomic>
#include <chrono>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;

using Session       = GlobalSystemMediaTransportControlsSession;
using SessionMgr    = GlobalSystemMediaTransportControlsSessionManager;
using PlaybackState = GlobalSystemMediaTransportControlsSessionPlaybackStatus;

struct SmtcService::Impl {
    HWND         hwnd = nullptr;
    std::wstring preferred;

    std::atomic<bool>     alive{false};
    std::atomic<uint32_t> gen{0};

    SessionMgr mgr{nullptr};
    Session    session{nullptr};

    event_token tokCurrentSession{};
    event_token tokSessions{};
    event_token tokMedia{};
    event_token tokPlayback{};
    event_token tokTimeline{};

    void Post(UINT msg, void* payload) {
        if (alive && hwnd && IsWindow(hwnd)) {
            if (PostMessageW(hwnd, msg, gen.load(), reinterpret_cast<LPARAM>(payload))) return;
        }
        // Finestra sparita: il payload sarebbe orfano.
        switch (msg) {
            case WM_APP_MEDIA: delete static_cast<MediaUpdate*>(payload); break;
            case WM_APP_PLAYBACK: delete static_cast<PlaybackUpdate*>(payload); break;
            case WM_APP_TIMELINE: delete static_cast<TimelineUpdate*>(payload); break;
            default: break;
        }
    }
};

namespace {

fire_and_forget PullMedia(std::shared_ptr<SmtcService::Impl> d, Session session, uint32_t gen) {
    co_await resume_background();
    try {
        auto props = co_await session.TryGetMediaPropertiesAsync();
        auto up    = std::make_unique<MediaUpdate>();
        up->title  = props.Title();
        up->artist = props.Artist();
        if (up->artist.empty()) up->artist = props.AlbumArtist();

        if (auto ref = props.Thumbnail()) {
            auto           stream = co_await ref.OpenReadAsync();
            const uint32_t size   = static_cast<uint32_t>(stream.Size());
            if (size > 0 && size < 16u * 1024 * 1024) {
                Buffer buffer(size);
                co_await stream.ReadAsync(buffer, size, InputStreamOptions::None);
                auto reader = DataReader::FromBuffer(buffer);
                up->thumb.resize(buffer.Length());
                if (!up->thumb.empty()) {
                    reader.ReadBytes(array_view<uint8_t>(up->thumb));
                    up->hasThumb = true;
                }
            }
        }
        if (d->alive && d->gen.load() == gen) d->Post(WM_APP_MEDIA, up.release());
    } catch (...) {
        // Il player può sparire nel mezzo: nessun dato è meglio di un crash.
    }
}

void PullPlayback(const std::shared_ptr<SmtcService::Impl>& d, const Session& session,
                  uint32_t gen) {
    try {
        auto info      = session.GetPlaybackInfo();
        auto up        = std::make_unique<PlaybackUpdate>();
        up->playing    = info.PlaybackStatus() == PlaybackState::Playing;
        auto controls  = info.Controls();
        up->canPrev    = controls.IsPreviousEnabled();
        up->canNext    = controls.IsNextEnabled();
        up->canToggle  = controls.IsPlayPauseToggleEnabled();
        up->canSeek    = controls.IsPlaybackPositionEnabled();
        if (d->alive && d->gen.load() == gen) d->Post(WM_APP_PLAYBACK, up.release());
    } catch (...) {
    }
}

void PullTimeline(const std::shared_ptr<SmtcService::Impl>& d, const Session& session,
                  uint32_t gen) {
    try {
        auto tl = session.GetTimelineProperties();
        auto up = std::make_unique<TimelineUpdate>();

        using ms          = std::chrono::milliseconds;
        const int64_t st  = std::chrono::duration_cast<ms>(tl.StartTime()).count();
        const int64_t pos = std::chrono::duration_cast<ms>(tl.Position()).count();
        const int64_t end = std::chrono::duration_cast<ms>(tl.EndTime()).count();

        up->posMs = (pos > st) ? pos - st : 0;
        up->endMs = (end > st) ? end - st : 0;

        // LastUpdatedTime dice quanto è vecchia questa posizione: serve perché
        // Spotify minimizzato aggiorna la timeline in modo pigro.
        const auto lastUpdated = tl.LastUpdatedTime();
        if (lastUpdated.time_since_epoch().count() != 0) {
            const int64_t age = std::chrono::duration_cast<ms>(clock::now() - lastUpdated).count();
            up->ageMs         = (age > 0) ? age : 0;
            if (up->ageMs > 24 * 60 * 60 * 1000) up->ageMs = 0;  // orologio incoerente
        }
        if (d->alive && d->gen.load() == gen) d->Post(WM_APP_TIMELINE, up.release());
    } catch (...) {
    }
}

}  // namespace

SmtcService::SmtcService() : d_(std::make_shared<Impl>()) {}
SmtcService::~SmtcService() { Stop(); }

void SmtcService::Start(HWND hwnd, std::wstring preferredApp) {
    d_->hwnd      = hwnd;
    d_->preferred = std::move(preferredApp);
    d_->alive     = true;

    // Asincrono: l'avvio della barretta non aspetta il session manager.
    auto d = d_;
    SessionMgr::RequestAsync().Completed([d](auto&& op, AsyncStatus status) {
        if (status != AsyncStatus::Completed || !d->alive) return;
        try {
            d->mgr = op.GetResults();
            d->tokCurrentSession = d->mgr.CurrentSessionChanged(
                [d](auto&&, auto&&) { d->Post(WM_APP_SESSION, nullptr); });
            d->tokSessions =
                d->mgr.SessionsChanged([d](auto&&, auto&&) { d->Post(WM_APP_SESSION, nullptr); });
            d->Post(WM_APP_SESSION, nullptr);
        } catch (...) {
        }
    });
}

void SmtcService::Rebind(HWND hwnd) {
    d_->hwnd = hwnd;
    if (d_->alive && d_->mgr) SelectSession();
}

void SmtcService::Stop() {
    if (!d_ || !d_->alive.exchange(false)) return;
    try {
        if (d_->session) {
            d_->session.MediaPropertiesChanged(d_->tokMedia);
            d_->session.PlaybackInfoChanged(d_->tokPlayback);
            d_->session.TimelinePropertiesChanged(d_->tokTimeline);
            d_->session = nullptr;
        }
        if (d_->mgr) {
            d_->mgr.CurrentSessionChanged(d_->tokCurrentSession);
            d_->mgr.SessionsChanged(d_->tokSessions);
            d_->mgr = nullptr;
        }
    } catch (...) {
    }
}

bool SmtcService::AcceptGeneration(uint32_t gen) const { return d_->gen.load() == gen; }

bool SmtcService::HasSession() const { return d_->session != nullptr; }

void SmtcService::SelectSession() {
    if (!d_->alive || !d_->mgr) return;

    try {
        // Sgancia la sessione precedente prima di cambiarla.
        if (d_->session) {
            d_->session.MediaPropertiesChanged(d_->tokMedia);
            d_->session.PlaybackInfoChanged(d_->tokPlayback);
            d_->session.TimelinePropertiesChanged(d_->tokTimeline);
            d_->session = nullptr;
        }

        // Anche un video nel browser registra una sessione: se c'è l'app
        // preferita vince lei, altrimenti si usa quella corrente di sistema.
        Session picked{nullptr};
        if (!d_->preferred.empty()) {
            for (auto&& s : d_->mgr.GetSessions()) {
                if (ContainsNoCase(std::wstring(s.SourceAppUserModelId()), d_->preferred)) {
                    picked = s;
                    break;
                }
            }
        }
        if (!picked) picked = d_->mgr.GetCurrentSession();

        const uint32_t gen = d_->gen.fetch_add(1) + 1;
        d_->session        = picked;
        if (!picked) {
            PostMessageW(d_->hwnd, WM_APP_MEDIA, gen, 0);  // niente sessione: pulisci la UI
            return;
        }

        auto d          = d_;
        d_->tokMedia    = picked.MediaPropertiesChanged(
            [d](Session const& s, MediaPropertiesChangedEventArgs const&) {
                PullMedia(d, s, d->gen.load());
            });
        d_->tokPlayback = picked.PlaybackInfoChanged(
            [d](Session const& s, PlaybackInfoChangedEventArgs const&) {
                PullPlayback(d, s, d->gen.load());
            });
        d_->tokTimeline = picked.TimelinePropertiesChanged(
            [d](Session const& s, TimelinePropertiesChangedEventArgs const&) {
                PullTimeline(d, s, d->gen.load());
            });

        PullAll();
    } catch (...) {
    }
}

void SmtcService::PullAll() {
    if (!d_->alive || !d_->session) return;
    const uint32_t gen = d_->gen.load();
    PullMedia(d_, d_->session, gen);
    PullPlayback(d_, d_->session, gen);
    PullTimeline(d_, d_->session, gen);
}

void SmtcService::TogglePlayPause() {
    if (!d_->session) return;
    try {
        d_->session.TryTogglePlayPauseAsync();
    } catch (...) {
    }
}

void SmtcService::Next() {
    if (!d_->session) return;
    try {
        d_->session.TrySkipNextAsync();
    } catch (...) {
    }
}

void SmtcService::Previous() {
    if (!d_->session) return;
    try {
        d_->session.TrySkipPreviousAsync();
    } catch (...) {
    }
}

void SmtcService::Seek(double fraction) {
    if (!d_->session) return;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    try {
        auto tl = d_->session.GetTimelineProperties();
        if (!d_->session.GetPlaybackInfo().Controls().IsPlaybackPositionEnabled()) return;
        const int64_t start = tl.StartTime().count();
        const int64_t end   = tl.EndTime().count();
        if (end <= start) return;
        const int64_t target = start + static_cast<int64_t>((end - start) * fraction);
        d_->session.TryChangePlaybackPositionAsync(target);
    } catch (...) {
    }
}

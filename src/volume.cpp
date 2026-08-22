#include "volume.h"

#include "player.h"

#include <mmdeviceapi.h>

using winrt::com_ptr;

namespace {

float ClampUnit(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

}  // namespace

bool PlayerVolume::Bind(const std::wstring& appKey) {
    control_ = nullptr;
    volume_  = nullptr;
    bound_.clear();
    if (appKey.empty()) return false;

    com_ptr<IMMDeviceEnumerator> devices;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(devices.put()))))
        return false;

    com_ptr<IMMDevice> endpoint;
    if (FAILED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, endpoint.put()))) return false;

    com_ptr<IAudioSessionManager2> manager;
    if (FAILED(endpoint->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr,
                                  manager.put_void())))
        return false;

    com_ptr<IAudioSessionEnumerator> sessions;
    if (FAILED(manager->GetSessionEnumerator(sessions.put()))) return false;

    int count = 0;
    if (FAILED(sessions->GetCount(&count))) return false;

    for (int i = 0; i < count; ++i) {
        com_ptr<IAudioSessionControl> session;
        if (FAILED(sessions->GetSession(i, session.put()))) continue;

        auto session2 = session.try_as<IAudioSessionControl2>();
        if (!session2) continue;

        DWORD pid = 0;
        if (FAILED(session2->GetProcessId(&pid)) || pid == 0) continue;
        if (!PlayerManager::ProcessMatches(pid, appKey)) continue;

        AudioSessionState state{};
        if (SUCCEEDED(session->GetState(&state)) && state == AudioSessionStateExpired) continue;

        auto simple = session.try_as<ISimpleAudioVolume>();
        if (!simple) continue;

        // Un player basato su Chromium ne ha di norma una sola (il processo
        // utility audio), ma se ne comparissero altre quella che sta suonando è
        // l'unica che conta: le inattive restano come ripiego.
        control_ = session;
        volume_  = simple;
        bound_   = appKey;
        if (state == AudioSessionStateActive) return true;
    }
    return volume_ != nullptr;
}

bool PlayerVolume::Alive() const {
    if (!control_ || !volume_) return false;

    AudioSessionState state{};
    if (FAILED(control_->GetState(&state)) || state == AudioSessionStateExpired) return false;

    float level = 0.f;
    return SUCCEEDED(volume_->GetMasterVolume(&level));
}

bool PlayerVolume::Ensure(const std::wstring& appKey) {
    if (!appKey.empty() && bound_ == appKey && Alive()) return true;
    return Bind(appKey);
}

float PlayerVolume::Get(const std::wstring& appKey) {
    if (!Ensure(appKey)) return -1.f;

    float level = 0.f;
    if (FAILED(volume_->GetMasterVolume(&level))) return -1.f;

    BOOL muted = FALSE;
    if (SUCCEEDED(volume_->GetMute(&muted)) && muted) return 0.f;
    return ClampUnit(level);
}

float PlayerVolume::Nudge(const std::wstring& appKey, float delta) {
    if (!Ensure(appKey)) return -1.f;

    float level = 0.f;
    if (FAILED(volume_->GetMasterVolume(&level))) return -1.f;

    // Da mutata il livello percepito è zero: alzare deve ripartire da lì,
    // altrimenti il primo scatto riporterebbe il volume di prima tutto insieme.
    BOOL muted = FALSE;
    if (SUCCEEDED(volume_->GetMute(&muted)) && muted) level = 0.f;

    level = ClampUnit(level + delta);
    if (FAILED(volume_->SetMasterVolume(level, nullptr))) return -1.f;
    if (muted && level > 0.f) volume_->SetMute(FALSE, nullptr);
    return level;
}

float PlayerVolume::ToggleMute(const std::wstring& appKey) {
    if (!Ensure(appKey)) return -1.f;

    BOOL muted = FALSE;
    if (FAILED(volume_->GetMute(&muted))) return -1.f;
    if (FAILED(volume_->SetMute(muted ? FALSE : TRUE, nullptr))) return -1.f;
    if (muted) {
        float level = 0.f;
        if (FAILED(volume_->GetMasterVolume(&level))) return -1.f;
        return ClampUnit(level);
    }
    return 0.f;
}

// volume.h — volume della sola Spotify, non del sistema.
//
// La sessione audio di un processo si raggiunge con IAudioSessionManager2 sul
// dispositivo di uscita predefinito: ISimpleAudioVolume è esattamente la
// levetta che Windows mostra nel mixer, quindi si muove solo Spotify e non il
// volume generale. Tutto out-of-process, coerente con la §2 del piano.
//
// La sessione viene tenuta in cache: si rilega solo quando quella in mano è
// scaduta (Spotify chiusa e riaperta) o quando non ce n'è ancora una.
#pragma once
#include "common.h"

#include <audiopolicy.h>
#include <winrt/base.h>

class SpotifyVolume {
public:
    // Volume corrente 0-1, oppure -1 se Spotify non ha una sessione audio
    // (non è in esecuzione, o non ha mai emesso un suono).
    float Get();

    // Somma delta al volume con clamp 0-1 e toglie il mute se si sale.
    // Torna il nuovo volume, o -1 se non c'è niente da muovere.
    float Nudge(float delta);

    // Torna il livello risultante (0 se ha appena mutato), -1 se non applicabile.
    float ToggleMute();

private:
    bool Bind();   // cerca la sessione di spotify.exe e la mette in cache
    bool Alive();  // vero se la sessione in cache è ancora utilizzabile

    winrt::com_ptr<IAudioSessionControl> control_;
    winrt::com_ptr<ISimpleAudioVolume>   volume_;
};

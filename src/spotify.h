// spotify.h — alleggerimento di Spotify: minimizza/nascondi la sua finestra
// così che Chromium smetta di comporre frame. Solo API out-of-process.
#pragma once
#include "common.h"

class SpotifyManager {
public:
    // Cerca la finestra principale di spotify.exe. Con includeHidden=true
    // considera anche finestre non visibili (recupero di orfane).
    static HWND FindMainWindow(bool includeHidden = false);
    static bool IsRunning();

    // Vero se il PID appartiene a spotify.exe. Lo usa anche il modulo volume
    // per riconoscere la sessione audio giusta nel mixer.
    static bool IsSpotifyPid(DWORD pid);

    // Applica lo stato scelto (minimize o hide). Ricorda cosa ha fatto per
    // poter garantire il ripristino.
    void Apply(bool hideInstead);
    void Restore();

    // Se al precedente avvio la finestra era stata nascosta e MiniBar è morta
    // male, il marker su disco lo dice: qui si recupera l'orfana.
    void RecoverOrphan();

    static bool Launch(bool minimized);

    bool DidHide() const { return hid_; }

private:
    static std::wstring MarkerPath();
    bool                hid_       = false;  // abbiamo usato SW_HIDE
    bool                minimized_ = false;  // abbiamo usato SW_MINIMIZE
};

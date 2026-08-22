// player.h — il player preferito visto da fuori: qual è il suo processo, dov'è
// la sua finestra, come alleggerirlo minimizzandola. Nessun player in
// particolare: l'identità arriva dalla sessione SMTC attiva. Solo API
// out-of-process, come da §2 del piano.
#pragma once
#include "common.h"

class PlayerManager {
public:
    // `appKey` è il SourceAppUserModelId della sessione SMTC ("Spotify.exe",
    // "Chrome", "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"…) oppure la
    // stringa scritta in preferred_app. Il confronto è tollerante: nome
    // dell'eseguibile, AUMID dell'app impacchettata, o sottostringa dell'uno
    // nell'altro — quel che basta per legare una sessione SMTC al suo processo.
    static bool ProcessMatches(DWORD pid, const std::wstring& appKey);

    // Etichetta leggibile per il menu: "Spotify.exe" → "Spotify".
    static std::wstring FriendlyName(const std::wstring& appKey);

    // Finestra principale del player. Con includeHidden=true considera anche le
    // finestre non visibili (recupero di orfane).
    static HWND FindMainWindow(const std::wstring& appKey, bool includeHidden = false);
    static bool IsRunning(const std::wstring& appKey);

    // Applica lo stato scelto (minimize o hide) al player indicato. Ricorda su
    // chi ha agito per poter garantire il ripristino.
    void Apply(const std::wstring& appKey, bool hideInstead);
    void Restore();

    // Se al precedente avvio una finestra era stata nascosta e MiniBar è morta
    // male, il marker su disco dice quale: qui si recupera l'orfana.
    void RecoverOrphan();

    // Prova `explicitPath` se indicato, altrimenti i percorsi tipici derivati
    // dal nome del player.
    static bool Launch(const std::wstring& appKey, const std::wstring& explicitPath);

    bool DidHide() const { return hid_; }

private:
    static std::wstring MarkerPath();

    std::wstring applied_;             // player su cui abbiamo agito
    bool         hid_       = false;   // abbiamo usato SW_HIDE
    bool         minimized_ = false;   // abbiamo usato SW_MINIMIZE
};

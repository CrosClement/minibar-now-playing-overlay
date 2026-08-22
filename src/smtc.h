// smtc.h — wrapper delle System Media Transport Controls.
// Gli eventi WinRT arrivano su thread del threadpool: qui non si tocca mai lo
// stato dell'app, si impacchettano i dati e si posta un messaggio al thread UI.
#pragma once
#include "common.h"

#include <memory>

// Payload dei messaggi WM_APP_*. Il ricevente ne diventa proprietario.
struct MediaUpdate {
    std::wstring         title;
    std::wstring         artist;
    std::vector<uint8_t> thumb;  // byte grezzi dell'immagine (PNG/JPEG)
    bool                 hasThumb = false;
};

struct PlaybackUpdate {
    bool playing   = false;
    bool canPrev   = false;
    bool canNext   = false;
    bool canToggle = false;
    bool canSeek   = false;
};

struct TimelineUpdate {
    int64_t posMs = 0;  // posizione relativa all'inizio del brano
    int64_t endMs = 0;  // durata; 0 = sconosciuta
    int64_t ageMs = 0;  // quanto era vecchia la posizione al momento della lettura
};

class SmtcService {
public:
    struct Impl;  // pubblico: lo usano gli helper interni di smtc.cpp

    SmtcService();
    ~SmtcService();

    void Start(HWND hwnd, std::wstring preferredApp);
    void Stop();

    // La finestra viene ricreata allo switch di modalità: qui si riaggancia
    // la destinazione dei messaggi senza rifare il session manager.
    void Rebind(HWND hwnd);

    // Da chiamare sul thread UI alla ricezione di WM_APP_SESSION.
    void SelectSession();

    // Vero se il messaggio appartiene alla sessione corrente (scarta i
    // messaggi in volo di una sessione ormai sostituita).
    bool AcceptGeneration(uint32_t gen) const;

    bool HasSession() const;

    void TogglePlayPause();
    void Next();
    void Previous();
    void Seek(double fraction);

    // Rilegge e riposta metadati, stato e timeline della sessione corrente.
    void PullAll();

private:
    std::shared_ptr<Impl> d_;
};

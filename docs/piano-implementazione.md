# MiniBar — Piano di implementazione

> Mini player musicale nativo per Windows 11: una barretta overlay/docked che legge le API media di sistema (SMTC), controlla la riproduzione e alleggerisce Spotify. Priorità assolute: **minimalismo e prestazioni**.

| | |
|---|---|
| **Versione** | 1.1 — 20 agosto 2026 |
| **Target** | Windows 11 x64 |
| **Stack** | C++20 · Win32 · Direct2D |
| **Dipendenze** | Solo Windows SDK |
| **Stima totale** | 7–10 giorni |

---

## 1 · Contesto e obiettivo

MiniBar è una barretta discreta che mostra la traccia in riproduzione (copertina, titolo, artista, progress bar) e offre controlli play/pausa/prev/next all'hover. Due modalità nella stessa app:

- **Overlay** — finestra always-on-top visibile sopra i giochi in borderless windowed. Non ruba mai il focus, è click-through quando il mouse non ci passa sopra, e riduce l'opacità a riposo.
- **Docked** — agganciata a un bordo dello schermo vicino alla taskbar, con spazio riservato: le finestre massimizzate la rispettano come fosse parte del sistema.

Obiettivo secondario ma importante: quando MiniBar è attiva, la finestra di Spotify viene minimizzata (o nascosta, opzionale) così che il suo motore Chromium smetta di comporre frame — Spotify si riduce a un servizio audio in background. MiniBar non può ridurre la RAM di base di Spotify (nessuna app esterna può farlo), ma azzera il suo lavoro di rendering su GPU e CPU.

Uso personale; eventuale repo pubblica in un secondo momento.

## 2 · Vincoli non negoziabili

- **Nessuna DLL injection, nessun hook su DirectX/Vulkan, nessun `SetWindowsHookEx` globale.** Sono i pattern che EAC, BattlEye e Vanguard classificano come cheat. Tutto ciò che tocca altri processi passa da API "da fuori": finestre topmost standard, `GetCursorPos`, `SetWinEventHook` in modalità `WINEVENT_OUTOFCONTEXT` (out-of-process, nessuna iniezione).
- **Accettato:** sopra il fullscreen esclusivo l'overlay non si vede. Nessun workaround: in quel caso resta la Game Bar di Windows.
- **Nessuna dipendenza da account/API Spotify:** niente OAuth, niente API key, niente Premium. La fonte dati sono le API media native di Windows (SMTC), quindi MiniBar funziona con qualunque player che le implementa, non solo Spotify.
- **Tutto su disco E:** l'app è portable. Eseguibile, file di configurazione e output di build vivono sotto `E:\CLAUDE\MINIBAR`. Nessun installer, nessuna scrittura in AppData o registro (eccetto l'eventuale voce di autostart, opzionale e disattivata di default).

## 3 · Budget prestazioni

Questi numeri sono **requisiti, non speranze**. Ogni milestone si chiude solo se il budget è rispettato.

| Metrica | Target | Come si misura |
|---|---|---|
| RAM (working set a regime) | < 15 MB | Task Manager dopo 10 min di riproduzione |
| CPU a riposo | ≈ 0,0 % | Task Manager, media su 60 s senza hover |
| GPU tra un ridisegno e l'altro | 0 % | Task Manager scheda GPU / PresentMon |
| Dimensione eseguibile | ≤ 2 MB | Build Release, CRT statica |
| Avvio (processo → barretta visibile) | < 200 ms | Timestamp log |
| Impatto FPS su gioco in borderless | non misurabile | Confronto frametime con/senza overlay |

Principio architetturale che li rende raggiungibili: **zero lavoro se non cambia niente**. Nessun render loop; l'app ridisegna solo su evento (cambio traccia, hover, tick della progress bar) e tra un evento e l'altro dorme nel message loop.

## 4 · Stack e decisioni già prese

Decisioni discusse e chiuse in fase di analisi: il piano le assume, non vanno rimesse in discussione senza motivo nuovo.

| Decisione | Scelta | Motivo |
|---|---|---|
| Linguaggio / UI | C++20, Win32 puro + Direct2D/DirectComposition | WPF/.NET costa 80–120 MB di runtime; Electron/Tauri peggio. Il nativo sta in ~10 MB e parla direttamente col DWM. Scartati WPF e WinUI 3 (pessimo supporto a finestre trasparenti click-through). |
| Fonte dati | SMTC via C++/WinRT (header nel Windows SDK) | Nessuna autenticazione, player-agnostico, event-driven. |
| Hover detection | Polling `GetCursorPos` a ~10 Hz | Un hook globale del mouse è sospetto per gli anti-cheat (specialmente Vanguard). Il polling costa due syscall: 0,00 % nel Task Manager. |
| Guerra del topmost | `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` + fallback raro | Reagire al cambio di foreground è pulito; un fallback ogni 5–10 s copre i giochi che si ri-promuovono topmost senza cambiare focus. |
| Alleggerimento Spotify | Minimizza di default, `SW_HIDE` come opzione | Stesso guadagno (Chromium smette di comporre in entrambi i casi), ma una finestra nascosta non è recuperabile da taskbar/Alt-Tab se MiniBar muore male. Ripristino garantito allo shutdown. |
| Configurazione | File INI accanto all'eseguibile | Portabile (vincolo disco E), zero dipendenze (`GetPrivateProfileString`), editabile a mano. |
| Build | CMake + MSVC (VS 2022), `/MT` (CRT statica) | Eseguibile singolo senza VC Redist, coerente col vincolo portable. |

## 5 · Architettura

Otto moduli piccoli con responsabilità nette. Il flusso: `SmtcService` e gli input utente alimentano `App` (stato centrale), che decide quando `Renderer` deve ridisegnare e quando `WindowManager` deve cambiare stili.

| Modulo | Responsabilità |
|---|---|
| `main.cpp` | Entry point, single-instance (mutex nominato), message loop, init C++/WinRT (apartment STA). |
| `App` | Stato centrale (traccia corrente, playback, modalità, hover). Orchestrazione eventi → ridisegni. Nessuna logica di piattaforma. |
| `SmtcService` | Wrapper di `GlobalSystemMediaTransportControlsSessionManager`: sessioni, metadati, eventi, comandi, filtro per app preferita. |
| `Renderer` | Direct2D + DirectComposition: layout della barretta, testo, copertina, progress bar. Disegna solo su richiesta di App. |
| `WindowManager` | Creazione finestra, stili estesi, toggle click-through, opacità, posizionamento, DPI per-monitor. |
| `AppBar` | Modalità docked: registrazione `SHAppBarMessage`, riserva dello spazio schermo, gestione callback. |
| `ForegroundWatch` | `SetWinEventHook` out-of-context + fallback: mantiene l'overlay sopra il gioco. |
| `SpotifyManager` | Trova la finestra di Spotify, minimizza/nasconde/ripristina, avvio opzionale con `--minimized`. |

**Modello di threading.** Un solo thread UI con il message loop. Gli eventi WinRT delle SMTC arrivano su thread del threadpool: gli handler **non toccano mai lo stato direttamente** — impacchettano i dati e li rimandano al thread UI con `PostMessage(WM_APP + n)`. Il polling del cursore e il fallback topmost usano `SetTimer` (niente thread dedicati). Risultato: zero lock nello stato dell'app, zero race.

## 6 · Dettagli implementativi chiave

### 6.1 · SMTC: sessioni, metadati, comandi

```cpp
auto mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
auto session = mgr.GetCurrentSession();   // può essere null
// Eventi da agganciare:
//   mgr.CurrentSessionChanged
//   session.MediaPropertiesChanged      → titolo, artista, thumbnail
//   session.PlaybackInfoChanged         → playing/paused, comandi abilitati
//   session.TimelinePropertiesChanged   → posizione, durata
```

- Metadati: `TryGetMediaPropertiesAsync()` → `Title`, `Artist`, `Thumbnail` (un `IRandomAccessStreamReference`, non base64).
- Comandi: `TryTogglePlayPauseAsync()`, `TrySkipNextAsync()`, `TrySkipPreviousAsync()`. Il seek (`TryChangePlaybackPositionAsync`) è opzionale: verificare a runtime `PlaybackInfo.Controls.IsPlaybackPositionEnabled`.
- Sessioni multiple: anche un video YouTube nel browser registra una sessione. Config `preferred_app` (default `Spotify`): se tra `mgr.GetSessions()` ce n'è una il cui `SourceAppUserModelId` contiene la stringa, vince; altrimenti si usa `GetCurrentSession()`.

### 6.2 · Progress bar: interpolazione obbligatoria

Le SMTC non emettono un tick continuo: `TimelineProperties` fornisce `Position` + `LastUpdatedTime`. In più Spotify aggiorna la timeline pigramente quando è minimizzato — e MiniBar lo minimizza di proposito, quindi il problema è garantito. La barra visualizzata è sempre calcolata localmente:

```
shown = Position + (now − LastUpdatedTime)   // solo se Playing
clamp(shown, 0, EndTime)
```

Un `SetTimer` a 1 Hz fa avanzare la barra (ridisegnando solo il rettangolo della progress); ogni `TimelinePropertiesChanged` ri-sincronizza. Il timer è attivo solo in stato Playing e con barretta visibile.

### 6.3 · Finestra overlay

- Stili: `WS_POPUP` + `WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` (niente focus, niente Alt-Tab, niente taskbar).
- Rendering raccomandato: `WS_EX_NOREDIRECTIONBITMAP` + swapchain DXGI in modalità composition + DirectComposition per alpha per-pixel accelerata. Variante di ripiego più semplice (accettabile, il costo per una barretta è irrisorio): `WS_EX_LAYERED` + `UpdateLayeredWindow` con bitmap ARGB premoltiplicata disegnata da D2D su render target WIC.
- Swapchain: `CreateSwapChainForComposition` con `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` (le composition swapchain accettano solo il flip model). `Present()` viene chiamata esclusivamente quando si ridisegna: in pausa il message loop dorme e non si presenta mai — il budget GPU a riposo è strutturale, non un'ottimizzazione.
- Mostrare sempre con `ShowWindow(SW_SHOWNOACTIVATE)`.
- Opacità: piena all'hover, ridotta (configurabile, default ~55 %) a riposo, con transizione breve.
- DPI: manifest **Per-Monitor V2**, gestione `WM_DPICHANGED`. Testare con due monitor a scale diverse.

### 6.4 · Click-through e hover

Paradosso da risolvere: una finestra con `WS_EX_TRANSPARENT` non riceve eventi mouse, quindi non può accorgersi dell'hover. Soluzione: timer a ~10 Hz che confronta `GetCursorPos` col rect della finestra.

- Cursore dentro → rimuovi `WS_EX_TRANSPARENT`, mostra i controlli, opacità piena.
- Cursore fuori (con isteresi di qualche centinaio di ms per evitare flicker) → ripristina `WS_EX_TRANSPARENT`, nascondi i controlli, opacità a riposo.

### 6.5 · Restare sopra il gioco

- Via principale: `SetWinEventHook(EVENT_SYSTEM_FOREGROUND, …, WINEVENT_OUTOFCONTEXT)` — notifica via messaggio, nessuna iniezione nel processo del gioco. Al cambio di foreground: un `SetWindowPos(HWND_TOPMOST, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE)`.
- Fallback: ogni 5–10 s verifica se la barretta è finita sotto (camminando la z-order con `GetWindow`) e solo in quel caso ri-asserisce. Mai spingere alla cieca a frequenza alta.
- Nota UIPI: sopra finestre di processi elevati il re-assert può fallire silenziosamente; accettato (caso raro per i giochi).

### 6.6 · Modalità docked (AppBar)

- `SHAppBarMessage`: `ABM_NEW` → `ABM_QUERYPOS` → `ABM_SETPOS` per riservare una striscia su un bordo (default: bottom, sopra la taskbar); `ABM_REMOVE` allo switch di modalità o all'uscita.
- Gestire il messaggio di callback (`ABN_POSCHANGED` ecc.) per taskbar spostata/nascosta e cambi di risoluzione.
- **Bizze note su Windows 11:** la taskbar è riscritta in XAML e `SHAppBarMessage` funziona per retrocompatibilità, ma può restituire RECT sfalsati sui monitor secondari con DPI diverso dal primario. Non fidarsi di rect memorizzati: ricalcolare tutto a ogni `WM_DPICHANGED` e a ogni notifica ABN.
- In docked il click-through è disattivato e l'opacità è sempre piena: è UI "di sistema", non overlay.
- **Chiarezza:** le deskband (estensioni vere della taskbar) sono morte su Windows 11. "Docked" = finestra adiacente alla taskbar con spazio riservato. Visivamente equivalente, tecnicamente separata.

### 6.7 · SpotifyManager

- Trovare la finestra: enumerare le top-level (`EnumWindows`), match sul processo proprietario `spotify.exe` (via `GetWindowThreadProcessId` + `QueryFullProcessImageName`). Non affidarsi alla classe: CEF crea **molte** top-level invisibili con lo stesso nome (`Chrome_WidgetWin_1`). Filtro completo per la finestra principale: processo `spotify.exe` **e** `WS_VISIBLE` **e** niente `WS_EX_TOOLWINDOW` **e** rect non vuoto **e** titolo non vuoto.
- All'attivazione (configurabile): `SW_MINIMIZE` (default) oppure `SW_HIDE` (opzione "aggressiva"). Alla chiusura di MiniBar: **sempre** ripristino (`SW_RESTORE`/`SW_SHOW`), anche su crash gestito (handler di uscita + il ripristino gira pure all'avvio successivo se trova una finestra Spotify nascosta orfana).
- Se Spotify non è in esecuzione (opzione off di default): avvio con flag `--minimized`.

### 6.8 · Pipeline copertine

Stream SMTC → WIC (`IWICImagingFactory` + `IWICBitmapScaler`, interpolazione Fant) → scala **alla dimensione di visualizzazione** (es. 56×56 px effettivi, moltiplicati per il DPI) → `ID2D1Bitmap`. Mai caricare in VRAM l'immagine a piena risoluzione. Cache dell'ultima bitmap: si rigenera solo al cambio traccia o di DPI.

### 6.9 · Configurazione e interazione

```ini
; minibar.ini — accanto all'eseguibile
[general]  mode=overlay|docked   preferred_app=Spotify
[overlay]  x=…  y=…  idle_opacity=140   ; 0–255
[docked]   edge=bottom|left|right
[spotify]  auto_minimize=1  hide_instead=0  launch_if_missing=0
```

- Trascinamento della barretta in overlay (drag su zona non-controlli), posizione persistita.
- Menu contestuale col tasto destro: switch modalità, toggle auto-minimize, esci. Niente finestra impostazioni nella v1: si edita l'INI.
- Hotkey globale opzionale (`RegisterHotKey`) per mostrare/nascondere la barretta.
- **Icona nella system tray** (`Shell_NotifyIconW`) con menu: mostra/nascondi barretta, ripristina Spotify, esci. È la scialuppa di salvataggio: con Spotify nascosto (`hide_instead=1`) e barretta nascosta via hotkey, senza tray l'utente non avrebbe più alcuna UI raggiungibile. Costo in risorse: praticamente zero.

## 7 · Struttura del repository

```
E:\CLAUDE\MINIBAR\
├── CMakeLists.txt
├── docs\piano-implementazione.md        (questo documento)
├── assets\minibar.ico
└── src\
    ├── main.cpp          entry, mutex single-instance, message loop
    ├── app.h/.cpp        stato centrale e orchestrazione
    ├── smtc.h/.cpp       SmtcService (C++/WinRT)
    ├── window.h/.cpp     WindowManager: stili, click-through, DPI
    ├── render.h/.cpp     Renderer: D2D/DComp, layout barretta
    ├── appbar.h/.cpp     modalità docked
    ├── foreground.h/.cpp ForegroundWatch
    ├── spotify.h/.cpp    SpotifyManager
    ├── config.h/.cpp     lettura/scrittura INI
    ├── minibar.rc        risorse, icona
    └── minibar.manifest  DPI Per-Monitor V2
```

Toolchain: Visual Studio 2022 (workload Desktop C++) + Windows 11 SDK. Build: `cmake --preset release` con output in `E:\CLAUDE\MINIBAR\build`. Nessun pacchetto esterno: C++/WinRT, D2D, DComp e WIC sono tutti nel SDK.

## 8 · Milestone

Sette milestone in sequenza, ognuna con criteri di accettazione verificabili. Le stime assumono uno sviluppatore che conosce Win32 ma non necessariamente C++/WinRT.

### M0 · Scheletro — 0,5–1 g

- Progetto CMake, manifest DPI, CRT statica, single-instance.
- Finestra `WS_POPUP` topmost/noactivate/toolwindow con rettangolo D2D di prova e alpha per-pixel.
- Menu contestuale con "Esci".

> **Accettazione:** la barretta vuota resta sopra un gioco in borderless senza rubargli il focus; exe ≤ 2 MB; RAM < 10 MB.

### M1 · SmtcService — 1 g

- Session manager, filtro app preferita, tutti gli eventi agganciati con marshalling via `PostMessage`.
- Interpolazione timeline con timer 1 Hz condizionato allo stato Playing.
- Comandi play/pausa/prev/next funzionanti (test da debug output, senza UI).

> **Accettazione:** cambio traccia su Spotify (anche minimizzato) riflesso negli eventi entro 1 s; posizione interpolata con deriva < 1 s su 3 minuti; nessun crash chiudendo/riaprendo Spotify.

### M2 · Rendering — 1–2 g

- Layout: copertina, titolo (ellissi se lungo), artista, progress bar.
- Pipeline copertine WIC con scaling alla dimensione di visualizzazione.
- Ridisegno solo su evento; tick 1 Hz ridisegna solo la progress.

> **Accettazione:** GPU a 0 % tra i ridisegni; testo nitido a 100 % e 150 % di scala; copertina corretta al cambio traccia.

### M3 · Interazione — 1 g

- Polling cursore 10 Hz, toggle `WS_EX_TRANSPARENT` con isteresi.
- Controlli all'hover collegati a SmtcService; opacità piena/ridotta con transizione.
- Drag della barretta e persistenza posizione nell'INI.

> **Accettazione:** con cursore altrove i click passano al gioco sottostante; i pulsanti rispondono al primo click; CPU a riposo 0,0 % nel Task Manager.

### M4 · Robustezza overlay — 1 g

- `SetWinEventHook` foreground + fallback z-order 5–10 s.
- Gestione `WM_DPICHANGED`, multi-monitor, cambio risoluzione.

> **Accettazione:** dopo Alt-Tab ripetuti su 2–3 giochi borderless la barretta è sempre visibile; frametime del gioco invariato (PresentMon); nessun re-assert quando non serve.

### M5 · Modalità docked — 1–2 g

- AppBar su bordo configurabile con spazio riservato; switch runtime overlay ⇄ docked dal menu.
- Reazione a taskbar spostata/nascosta e cambi di risoluzione.

> **Accettazione:** una finestra massimizzata non copre la barretta docked; dopo lo switch di modalità e ritorno, nessuno spazio riservato orfano; uscita pulita (`ABM_REMOVE` sempre eseguito).

### M6 · SpotifyManager e rifiniture — 1 g

- Auto-minimize (default) / hide (opzione) con ripristino garantito, anche di finestre orfane al riavvio.
- Avvio opzionale di Spotify con `--minimized`; hotkey globale opzionale.
- Icona tray con menu (mostra/nascondi barretta, ripristina Spotify, esci).
- Verifica finale del budget prestazioni, soak test di 2+ ore.

> **Accettazione:** con MiniBar attiva e Spotify minimizzato, GPU di Spotify a 0 %; chiusura di MiniBar (anche brusca + riavvio) ripristina sempre la finestra; con barretta nascosta via hotkey e Spotify nascosto, dalla tray si ripristina tutto; tutti i target della Sezione 3 verificati e annotati.

## 9 · Rischi e mitigazioni

| Rischio | Probabilità | Mitigazione |
|---|---|---|
| Spotify aggiorna la timeline in modo pigro/incoerente da minimizzato | Alta (attesa) | Interpolazione locale già obbligatoria (6.2); re-sync su ogni evento. |
| Thumbnail SMTC null o in ritardo al cambio traccia | Media | Placeholder neutro; retry al successivo `MediaPropertiesChanged`. |
| Giochi che si ri-promuovono topmost senza cambio focus | Media | Fallback z-order (6.5). Se un titolo specifico resiste, documentare e accettare. |
| Interazioni AppBar con la taskbar Win11 (auto-hide, monitor multipli) | Media | Gestire le notifiche ABN; test con taskbar auto-hide sin da M5. |
| Falso positivo anti-cheat | Molto bassa | Nessuna iniezione/hook per design; solo API pubbliche out-of-process. Stesso profilo di rischio di EarTrumpet o del flyout volume. |
| Spotify cambia il flag `--minimized` o il comportamento finestra | Bassa | Funzione opzionale e isolata in SpotifyManager; degrada senza rompere il resto. |

## 10 · Fuori scope (v1)

- Visibilità sopra il fullscreen esclusivo (per design, vedi vincoli).
- Client Spotify alternativo (librespot: richiede Premium, zona grigia TOS).
- Testi/lyrics, code di riproduzione, like/salvataggio brani (SMTC non li espone).
- Finestra impostazioni grafica (v1 = INI + menu contestuale).
- Installer, auto-update, piattaforme diverse da Windows 11 x64.

> **Nota per chi implementa:** ogni volta che una feature sembra richiedere un hook, un'iniezione o un thread in più, la risposta giusta è quasi certamente "no": rientra nei vincoli della Sezione 2 o nel principio zero-lavoro-a-riposo della Sezione 3.

## 11 · Riferimenti API

| Area | API |
|---|---|
| Media | `Windows.Media.Control` — `GlobalSystemMediaTransportControlsSessionManager` e tipi collegati (C++/WinRT) |
| Finestre | `CreateWindowExW`, `SetWindowLongPtrW (GWL_EXSTYLE)`, `SetWindowPos`, `SetLayeredWindowAttributes` / `UpdateLayeredWindow` |
| Rendering | Direct2D 1.1 (`ID2D1DeviceContext`), DirectComposition (`IDCompositionDevice`), DXGI, DirectWrite, WIC |
| Overlay | `SetWinEventHook` (`EVENT_SYSTEM_FOREGROUND`, `WINEVENT_OUTOFCONTEXT`), `GetCursorPos`, `SetTimer` |
| Docked | `SHAppBarMessage` (`ABM_NEW`, `ABM_QUERYPOS`, `ABM_SETPOS`, `ABM_REMOVE`) |
| Spotify | `EnumWindows`, `GetWindowThreadProcessId`, `QueryFullProcessImageNameW`, `ShowWindow`, `CreateProcessW` |
| Varie | `CreateMutexW` (single instance), `RegisterHotKey`, `Shell_NotifyIconW` (tray), `GetPrivateProfileStringW`, `WM_DPICHANGED` |

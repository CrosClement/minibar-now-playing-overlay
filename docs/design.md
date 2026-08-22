# MiniBar — Design

> L'idea, i vincoli e le scelte di progetto dietro MiniBar: una barretta musicale nativa
> per Windows 11 che legge le API media di sistema, controlla la riproduzione e toglie di
> mezzo la finestra del player. Questo documento spiega **perché** le cose sono fatte così; il
> [README](../README.it.md) spiega come si compila e si usa.

| | |
|---|---|
| **Target** | Windows 11 x64 |
| **Stack** | C++20 · Win32 · Direct2D (software) · C++/WinRT |
| **Dipendenze** | Solo Windows SDK |
| **Licenza** | MIT |

---

## 1 · L'idea

Un mini player che non sembri un'altra applicazione aperta: una barretta discreta con
copertina, titolo, artista e progress bar, i comandi play/pausa/precedente/successivo
all'hover, e niente altro. Non è un client musicale — non suona nulla — ma il telecomando
di quello che già suona, qualunque esso sia.

Due principi guidano tutto il resto:

- **Minimalismo e prestazioni prima di ogni feature.** La barretta deve costare meno di
  una scheda del browser vuota: pochi MB di RAM, CPU a zero a riposo, nessun lavoro sulla
  GPU fra un ridisegno e l'altro. Se una feature non può rispettare il budget della §3,
  non entra.
- **Mai toccare gli altri processi.** Niente injection, niente hook globali: tutto ciò che
  MiniBar sa degli altri programmi lo impara da API pubbliche out-of-process, le stesse
  del flyout del volume di Windows. È quello che la rende sicura accanto a un anti-cheat.

Tre modalità nella stessa applicazione, scelte dal menu contestuale:

- **Overlay** — finestra always-on-top che resta sopra i giochi in borderless windowed;
  non ruba mai il focus, è click-through quando il mouse non ci passa sopra e riduce
  l'opacità a riposo. Due aspetti: la barretta orizzontale o il riquadro con la sola
  copertina.
- **Docked** — striscia agganciata a un bordo dello schermo, con spazio riservato: le
  finestre massimizzate la rispettano come fosse parte del sistema.
- **Taskbar** — pillola compatta nello spazio vuoto della taskbar, che ne segue altezza,
  bordo, DPI e tema e sparisce insieme a lei.

Obiettivo collaterale: mentre MiniBar è attiva la finestra del player viene minimizzata (o
nascosta, opzionale), così non sta fra i piedi — lo schermo resta libero e i controlli
restano sulla barretta. Che questo faccia anche risparmiare risorse dipende dal player e
non va dato per scontato: misurato su Spotify (Windows 11 25H2), con la finestra
minimizzata la RAM non cambia e la CPU non scende. Il valore di MiniBar è il suo costo, non
il risparmio altrui.

## 2 · Vincoli non negoziabili

- **Nessuna DLL injection, nessun hook su DirectX/Vulkan, nessun `SetWindowsHookEx`
  globale.** Sono i pattern che EAC, BattlEye e Vanguard classificano come cheat. Tutto ciò
  che riguarda altri processi passa da API "da fuori": finestre topmost standard,
  `GetCursorPos`, `SetWinEventHook` in modalità `WINEVENT_OUTOFCONTEXT`, Core Audio,
  `EnumWindows`.
- **Accettato:** sopra il fullscreen *esclusivo* l'overlay non si vede. Nessun workaround:
  lì resta la Game Bar di Windows. Il borderless windowed, che è quello che usano quasi
  tutti i giochi oggi, funziona.
- **Nessuna dipendenza da account o API di un servizio:** niente OAuth, niente API key,
  niente abbonamento. La fonte dati sono le System Media Transport Controls (SMTC) di
  Windows, quindi MiniBar funziona con qualunque player le implementi: Spotify, Tidal,
  YouTube Music nel browser, foobar2000, il lettore di Windows.
- **Portable.** Un solo eseguibile con CRT statica, nessun installer, nessuna scrittura in
  AppData o nel registro: l'unico file che MiniBar scrive è `minibar.ini`, accanto a sé.
  La stessa filosofia vale per la toolchain, che vive in `tools\` e si ricostruisce da
  pacchetti ufficiali senza installare niente.

## 3 · Budget prestazioni

Questi numeri sono **requisiti, non speranze**: ogni modifica si accetta solo se li
rispetta, e la CI fallisce se l'eseguibile sfora.

| Metrica | Target | Misurato (1.1) | Come si misura |
|---|---|---|---|
| RAM (private working set a regime) | < 15 MB | ~7 MB (barretta), ~5,5 (riquadro) | Task Manager, "Working set (privato)", dopo minuti di riproduzione |
| CPU a riposo | ≈ 0,0 % | 0,0 % (centesimi di core) | Task Manager, media su 60 s senza hover |
| GPU fra un ridisegno e l'altro | 0 % | 0 % — non esiste un device GPU | Task Manager scheda GPU / PresentMon |
| Dimensione eseguibile | ≤ 2 MB | 300 KB | build Release, controllato dalla CI |
| Avvio (processo → barretta visibile) | < 200 ms | ~57 ms (mediana di 5 a caldo) | timestamp |
| Impatto sul frametime di un gioco | non misurabile | — | PresentMon con e senza overlay |

Il principio che li rende raggiungibili: **zero lavoro se non cambia niente**. Non c'è un
render loop; l'app ridisegna solo su evento (cambio traccia, hover, tick della progress bar,
transizione in corso) e fra un evento e l'altro dorme nel message loop. Le misure di CPU
variano con quanti cambi traccia capitano nella finestra di misura — ognuno costa il decode
della copertina, il calcolo della tinta e 220 ms di dissolvenza — e vanno confrontate a
parità di condizioni.

## 4 · Decisioni e motivi

| Decisione | Scelta | Motivo |
|---|---|---|
| Linguaggio / UI | C++20, Win32 puro + Direct2D | WPF/.NET costa 80–120 MB di runtime; Electron/Tauri peggio; WinUI 3 gestisce male le finestre trasparenti click-through. Il nativo sta in pochi MB e parla direttamente col sistema. |
| Rendering | Direct2D **software** su bitmap WIC + `UpdateLayeredWindow` | Provata prima la strada "giusta" (swapchain DXGI di composizione + DirectComposition) e misurata: 37,7 MB privati e 26 thread contro 8,7 MB e 11 thread. Un device D3D11 non si ammortizza su una superficie di 266×45 px ridisegnata al massimo una volta al secondo; la rasterizzazione software costa frazioni di millisecondo e rende lo "0 % GPU" strutturale. |
| Fonte dati | SMTC via C++/WinRT (header nel Windows SDK) | Nessuna autenticazione, player-agnostico, event-driven. |
| Identità del player | `SourceAppUserModelId` della sessione SMTC | Da lì si risale al processo (nome eseguibile o AUMID per le app impacchettate), quindi alla sua sessione nel mixer e alla sua finestra. Nessun nome di applicazione cablato. |
| Hover | Polling `GetCursorPos` a ~10 Hz | Un hook globale del mouse è sospetto per gli anti-cheat; il polling costa due syscall e non si vede nel Task Manager. |
| Restare topmost | `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` out-of-context + controllo raro | Reagire al cambio di foreground è pulito; un controllo ogni pochi secondi copre i giochi che si ri-promuovono senza cambiare focus. Mai spingere alla cieca. |
| Player fuori dai piedi | Minimizza di default, `SW_HIDE` come opzione | Libera lo schermo; sul consumo del player non promette nulla (su Spotify misurato: nessun risparmio). Una finestra nascosta non è recuperabile da taskbar/Alt-Tab se MiniBar muore male: ripristino garantito allo shutdown e recupero all'avvio successivo tramite un marker su disco. |
| Volume | `ISimpleAudioVolume` sulla sessione audio del processo | È la levetta del mixer di Windows: si muove solo il player, non il sistema. Interfacce create al primo uso, mai all'avvio. |
| Configurazione | INI accanto all'eseguibile | Portable, zero dipendenze, editabile a mano. Le sole preferenze "di uso" (modalità, posizione, player) si salvano anche dal menu. |
| Build | CMake + Ninja + MSVC con CRT statica | Eseguibile singolo senza VC Redist. Toolchain portable in `tools\` per lo sviluppo locale; preset separato con Visual Studio per la CI. |

## 5 · Architettura

Moduli piccoli con responsabilità nette. Il flusso: `SmtcService` e gli input utente
alimentano `App` (stato centrale), che decide quando `Renderer` deve ridisegnare e quando
cambiare stili e posizione della finestra.

| Modulo | Responsabilità |
|---|---|
| `main.cpp` | Entry point, single-instance, message loop, init C++/WinRT (STA). |
| `App` | Stato centrale (traccia, playback, modalità, hover, animazioni). Orchestra eventi → ridisegni. Nessuna logica di piattaforma. |
| `SmtcService` | Wrapper di `GlobalSystemMediaTransportControlsSessionManager`: sessioni, metadati, eventi, comandi, selezione del player preferito, elenco sessioni, pausa di un'app. |
| `Renderer` | Direct2D software: layout delle quattro geometrie (barra, riquadro, docked, pillola), testo, copertina, progress bar, tinta dalla copertina, marquee, dissolvenze. Disegna solo su richiesta. |
| `window.cpp` | Creazione finestra, stili estesi, click-through, DPI per-monitor, query sulla taskbar, tema di sistema, tray, menu contestuale. |
| `AppBar` | Registrazione `SHAppBarMessage`: riserva dello spazio in docked, sole notifiche in taskbar. |
| `ForegroundWatch` | `SetWinEventHook` out-of-context + controllo di copertura: mantiene l'overlay sopra il gioco. |
| `PlayerManager` | Lega una sessione SMTC al suo processo e alla sua finestra; minimizza/nasconde/ripristina; recupera orfane; avvio opzionale. |
| `PlayerVolume` | Sessione Core Audio del player in cache, volume e mute. |
| `Config` | INI: lettura con ripieghi per i file vecchi, scrittura dei default e delle preferenze. |

**Threading.** Un solo thread UI con il message loop. Gli eventi WinRT delle SMTC arrivano
su thread del threadpool: gli handler **non toccano mai lo stato** — impacchettano i dati e
li rimandano al thread UI con `PostMessage(WM_APP + n)`, con un numero di generazione che
scarta i messaggi in volo di una sessione ormai sostituita. Polling del cursore, tick della
progress bar, animazioni e controllo topmost usano `SetTimer`. Risultato: zero lock, zero
race.

## 6 · Dettagli chiave

### 6.1 · SMTC: sessioni, player, comandi

- `GetCurrentSession()` è la sessione "di sistema"; `GetSessions()` le elenca tutte — anche
  un video in una scheda del browser ne registra una. Se `preferred_app` è impostata (dal
  file o dal menu) vince la sessione il cui `SourceAppUserModelId` la contiene, altrimenti
  si segue quella di sistema.
- Il sottomenu **Player** elenca le sessioni attive senza duplicati. Cambiando player,
  quello seguito prima viene messo in pausa se è diverso da quello seguito dopo: così
  "Automatic" che risolve sullo stesso player non ferma nulla, e non suonano mai in due.
- Comandi: `TryTogglePlayPauseAsync`, `TrySkipNext/PreviousAsync`,
  `TryChangePlaybackPositionAsync` solo se `IsPlaybackPositionEnabled`.

### 6.2 · Progress bar: interpolazione obbligatoria

Le SMTC non emettono un tick continuo: `TimelineProperties` dà `Position` +
`LastUpdatedTime`, e un player minimizzato aggiorna la timeline pigramente. La posizione
mostrata è sempre calcolata localmente — `Position + (now − LastUpdatedTime)` se in
riproduzione, con clamp sulla durata — e un timer a 1 Hz, attivo solo in Playing e con
barretta visibile, la fa avanzare; ogni evento di timeline risincronizza.

### 6.3 · Finestra overlay e rendering

- Stili: `WS_POPUP` + `WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED`.
  Mostrata sempre con `SW_SHOWNOACTIVATE`.
- Disegno su una bitmap WIC ARGB premoltiplicata con un render target Direct2D software;
  presentazione con `UpdateLayeredWindow`. Nessun device D3D, nessuna swapchain.
- DPI: manifest Per-Monitor V2, `WM_DPICHANGED` ridimensiona e ridecodifica.
- Opacità: piena all'hover, ridotta a riposo (`idle_opacity`), con una transizione breve.

### 6.4 · Click-through e hover

Una finestra con `WS_EX_TRANSPARENT` non riceve eventi mouse, quindi non può accorgersi
dell'hover: un timer a ~10 Hz confronta `GetCursorPos` col rettangolo della finestra.
Cursore dentro → via `WS_EX_TRANSPARENT`, comandi visibili, opacità piena; cursore fuori,
con un'isteresi di qualche centinaio di millisecondi, → ripristino. La rotellina e il tasto
centrale arrivano perché Windows, di default, scorre le finestre inattive sotto il cursore.

### 6.5 · Restare sopra il gioco

`SetWinEventHook(EVENT_SYSTEM_FOREGROUND, WINEVENT_OUTOFCONTEXT)`: al cambio di foreground
un `SetWindowPos(HWND_TOPMOST, SWP_NOACTIVATE)`. In più un controllo raro verifica se la
barretta è finita coperta e solo in quel caso ri-asserisce. Sopra finestre di processi
elevati il re-assert può fallire silenziosamente (UIPI): accettato.

### 6.6 · Docked: AppBar

`ABM_NEW → ABM_QUERYPOS → ABM_SETPOS` riserva una striscia sul bordo scelto; `ABM_REMOVE`
allo switch di modalità e all'uscita, sempre. Le notifiche `ABN_*` gestiscono taskbar
spostata o nascosta e cambi di risoluzione; i rettangoli non si memorizzano, si ricalcolano
a ogni notifica e a ogni `WM_DPICHANGED`, perché su Windows 11 con monitor a DPI diversi
possono arrivare sfalsati. In docked niente click-through e opacità sempre piena: è UI "di
sistema".

### 6.7 · Taskbar: non è una deskband

Scrivere *dentro* la taskbar, come facevano le deskband, richiede che Explorer carichi una
DLL nel proprio processo: su Windows 11 il meccanismo non esiste più e sarebbe comunque
injection. MiniBar posiziona la propria finestra topmost *sopra* i pixel liberi della
taskbar: legge `TaskbarAl` (allineamento delle icone) e il rettangolo di `TrayNotifyWnd`,
entrambi in sola lettura e da fuori processo, e si registra come appbar con `ABM_NEW`
**senza** `ABM_SETPOS`, così riceve `ABN_POSCHANGED`, `ABN_STATECHANGE` e
`ABN_FULLSCREENAPP` senza sottrarre spazio. La pillola a riposo mostra copertina, comandi e
progress; in hover si apre con un'animazione per mostrare titolo e artista. Il contrasto lo
detta il colore della taskbar vera (`SystemUsesLightTheme`), non una preferenza.

### 6.8 · Il player: finestra, volume, alleggerimento

- **Finestra principale**: `EnumWindows`, match sul processo proprietario con lo stesso
  criterio della sessione SMTC (nome eseguibile o AUMID), poi filtro `WS_VISIBLE`, niente
  `WS_EX_TOOLWINDOW`, nessun owner, rettangolo e titolo non vuoti — Chromium crea molte
  top-level invisibili con la stessa classe, quindi la classe non è un criterio.
- **Alleggerimento**: `SW_MINIMIZE` di default, `SW_HIDE` opzionale con un marker su disco
  che dice quale player è nascosto; ripristino garantito allo shutdown, a `WM_ENDSESSION` e
  all'avvio successivo se il marker è rimasto.
- **Volume**: `IAudioSessionManager2` sul dispositivo predefinito, sessione del processo
  del player, `ISimpleAudioVolume`. Cache rilegata solo se la sessione scade o cambia il
  player. Il livello si mostra per un attimo sulla striscia della progress bar, in chiaro.

### 6.9 · Copertine e colore

Stream SMTC → WIC → scala **alla dimensione di visualizzazione** → `ID2D1Bitmap`. Mai
l'immagine a piena risoluzione in memoria. Nello stesso decode una miniatura 12×12 dà la
**tinta dominante**: istogramma su 18 fasce di tinta pesate per saturazione (grigi ed
estremi scartati), fasce adiacenti sommate, media circolare; il colore finale viene
riportato in un intervallo di saturazione e luminosità leggibile sul fondo su cui finisce.
Copertine senza un colore vero non tingono nulla. Costo: 144 pixel, una volta per traccia.

### 6.10 · Testo e transizioni

- **Marquee**: un titolo che non ci sta resta troncato con l'ellissi; in hover scorre
  (fermo, andata, fermo, ritorno) con una maschera di opacità ai bordi — non un rettangolo
  del colore di fondo, che su una finestra semitrasparente si vedrebbe come una toppa. Il
  timer esiste solo in hover e solo se il titolo sborda.
- **Transizioni**: un solo timer a 16 ms per opacità, dissolvenza della copertina
  (la nuova compare sopra la vecchia in 220 ms, e la tinta viaggia con lei) e apertura della
  pillola; parte quando qualcosa si muove e si spegne appena tutto è fermo.
- **Tema**: `AppsUseLightTheme` decide come la barretta disegna sé stessa in overlay e
  docked, `SystemUsesLightTheme` il contrasto nella taskbar; sono due valori distinti su
  Windows 11. Cambio a caldo su `WM_SETTINGCHANGE`, senza ricreare finestre;
  `theme=dark|light` nell'INI lo forza.

## 7 · Rischi e mitigazioni

| Rischio | Mitigazione |
|---|---|
| Timeline aggiornata pigramente dal player minimizzato | Interpolazione locale (§6.2), risincronizzazione a ogni evento. |
| Thumbnail assente o in ritardo al cambio traccia | Placeholder; si riprova al `MediaPropertiesChanged` successivo; la copertina vecchia sfuma invece di sparire. |
| Giochi che si ri-promuovono topmost senza cambio di focus | Controllo di copertura (§6.5). Se un titolo resiste, si documenta e si accetta. |
| AppBar e taskbar di Windows 11 (auto-hide, più monitor) | Notifiche ABN gestite, rettangoli mai memorizzati. |
| Falso positivo anti-cheat | Nessuna iniezione né hook per design; stesso profilo di EarTrumpet o del flyout del volume. |
| Il player cambia comportamento della finestra o i flag di avvio | Funzioni isolate in `PlayerManager`, degradano senza rompere il resto. |

## 8 · Fuori scope

- Visibilità sopra il fullscreen esclusivo (per design).
- Client musicale alternativo: MiniBar non suona nulla.
- Testi, code di riproduzione, like: le SMTC non li espongono e servirebbero account e API.
- Finestra impostazioni grafica: INI documentato + menu contestuale.
- Installer, auto-update, piattaforme diverse da Windows 11 x64.

> Ogni volta che una feature sembra richiedere un hook, un'iniezione o un thread in più, la
> risposta giusta è quasi certamente "no": rientra nei vincoli della §2 o nel principio
> zero-lavoro-a-riposo della §3.

## 9 · Riferimenti API

| Area | API |
|---|---|
| Media | `Windows.Media.Control` — `GlobalSystemMediaTransportControlsSessionManager` e tipi collegati (C++/WinRT) |
| Finestre | `CreateWindowExW`, `SetWindowLongPtrW (GWL_EXSTYLE)`, `SetWindowPos`, `UpdateLayeredWindow`, `WM_DPICHANGED`, `WM_SETTINGCHANGE` |
| Rendering | Direct2D (render target WIC), DirectWrite, WIC |
| Overlay | `SetWinEventHook` (`EVENT_SYSTEM_FOREGROUND`, `WINEVENT_OUTOFCONTEXT`), `GetCursorPos`, `SetTimer` |
| Docked / taskbar | `SHAppBarMessage` (`ABM_NEW`, `ABM_QUERYPOS`, `ABM_SETPOS`, `ABM_REMOVE`, notifiche `ABN_*`), `FindWindowW` su `Shell_TrayWnd`/`TrayNotifyWnd` |
| Player | `EnumWindows`, `GetWindowThreadProcessId`, `QueryFullProcessImageNameW`, `GetApplicationUserModelId`, `ShowWindow`, `CreateProcessW` |
| Volume | `IMMDeviceEnumerator`, `IAudioSessionManager2`, `IAudioSessionControl2`, `ISimpleAudioVolume` |
| Varie | `CreateMutexW`, `RegisterHotKey`, `Shell_NotifyIconW`, `GetPrivateProfileStringW` |

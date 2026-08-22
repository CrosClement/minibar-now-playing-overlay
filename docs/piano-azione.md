# MiniBar — Piano d'azione per la pubblicazione su GitHub

Obiettivo: trasformare MiniBar da progetto personale funzionante a repository pubblico
curato, con un posizionamento chiaro — **l'overlay musicale più leggero per Windows 11**
(6 MB di RAM, 226 KB di exe, 0% GPU, nessun hook: sicuro con gli anti-cheat).

Il piano è diviso in quattro fasi in ordine di esecuzione. Le fasi 1 e 2 vanno completate
**prima** di rendere pubblico il repo: su GitHub la prima impressione non si ripete.
Ogni voce ha un criterio di completamento verificabile.

Vincoli invariati dal [piano di implementazione](piano-implementazione.md): budget
< 15 MB di RAM e ~0% CPU a riposo, zero injection/hook, app portable, exe ≤ 2 MB.
Nessuna voce di questo piano può violarli.

---

## Fase 0 — Fondazione del repository ✅

Preparazione tecnica, senza impatto sul prodotto.

| # | Attività | Fatto quando |
|---|---|---|
| 0.1 | Git portable in `tools\git\` (MinGit), gh CLI in `tools\gh\`, entrambi nel PATH di `env.ps1` | `git --version` e `gh --version` funzionano dalla shell del progetto |
| 0.2 | `.gitignore` completo: toolchain, cache download, build, dist, INI runtime | `git status` non mostra nulla di rigenerabile |
| 0.3 | `git init` + primo commit della sola sorgente | storia pulita, niente binari tracciati |
| 0.4 | Login GitHub (`gh auth login`, config su disco E via `GH_CONFIG_DIR`) | `gh auth status` OK — **richiede intervento manuale dell'utente** |

## Fase 1 — Generalizzazione multi-player ✅

Cambia il posizionamento: da "companion per Spotify" a "overlay universale per qualsiasi
player". È il lavoro più sostanzioso e va fatto per primo perché ridefinisce nome delle
opzioni, README e descrizione del repo.

| # | Attività | Note | Fatto quando |
|---|---|---|---|
| 1.1 ✅ | Volume per-app agnostico | `volume.cpp` cerca la sessione audio dal PID della sessione SMTC attiva, non più da "Spotify" | la rotellina muove il volume di YouTube Music / Tidal / foobar2000 |
| 1.2 ✅ | Auto-minimize agnostico | `spotify.cpp` → `player.cpp`: minimizza/ripristina la finestra del player preferito, qualunque sia | con `preferred_app=` diverso da Spotify il comportamento è identico |
| 1.3 ✅ | Selettore di sessione | voce nel menu contestuale che elenca le sessioni SMTC attive e permette di bloccarsi su una | con due player attivi si sceglie quale mostrare |
| 1.4 ✅ | Sezione INI rinominata | `[spotify]` → `[player]` con retrocompatibilità in lettura | vecchi INI continuano a funzionare |

Estensione della 1.3 aggiunta su richiesta: cambiando player dal menu, quello che si lascia
va **in pausa** (`SmtcService::PauseApp`, chiave `[player] pause_on_switch`, default acceso).
Il confronto è fra il player seguito prima e quello seguito dopo, non fra le voci di menu:
se la scelta risolve sullo stesso player — "Automatico" con una sola sessione attiva — non
si sta lasciando indietro nessuno e non si tocca niente.

L'identità del player è il `SourceAppUserModelId` della sessione SMTC: da lì si risale al
processo (nome dell'eseguibile, o AUMID per le app impacchettate) e quindi sia alla sua
sessione nel mixer sia alla sua finestra. Nessun nome di applicazione è più cablato nel
codice, `preferred_app=` vuoto significa "segui la sessione di sistema".

Verificato: volume mosso e rimesso a posto su un player finto che non è Spotify
(`probe.exe`, 100 % → 80 % → 100 %), minimize/hide/restore e recupero dell'orfana su
un'app qualsiasi, INI in vecchio formato letto senza modifiche (con `[player]` che vince
su `[spotify]` se ci sono entrambe), e sul campo: MiniBar riconosce Spotify dalla sessione
SMTC e lo minimizza leggendo un INI in vecchio formato. Private working set 5,3 MB.

Fuori scope: integrazione con API web dei singoli servizi (like, playlist). Contraddice
il minimalismo e richiede segreti/OAuth.

## Fase 2 — Rifinitura estetica

Le due-tre cose che rendono bella la GIF del README. Tutte compatibili col rendering
software e con lo zero-redraw a riposo.

| # | Attività | Note | Fatto quando |
|---|---|---|---|
| 2.1 ✅ | Colore dominante dalla copertina | media/quantizzazione sui pixel della bitmap SMTC a ogni cambio traccia; tinta su progress bar e/o sfondo | il colore segue l'album, costo solo al cambio traccia |
| 2.2 ✅ | Marquee dei titoli lunghi | scorrimento con dissolvenza ai bordi, **solo in hover** | a riposo zero ridisegni, come oggi |
| 2.3 ✅ | Transizioni brevi | fade della copertina al cambio traccia, easing sull'espansione della pillola taskbar | timer attivo solo durante la transizione (poche centinaia di ms) |
| 2.4 ✅ | Tema chiaro/scuro dal sistema | lettura di `AppsUseLightTheme` + notifica `WM_SETTINGCHANGE` | la barretta segue il tema senza riavvio |

Criterio trasversale: dopo ogni voce, rimisurare RAM/CPU a riposo e confermare che i
numeri della §3 reggono.

**2.1** — La tinta si ricava da una miniatura di 12×12 pixel della copertina, scalata da
WIC insieme a quella che si disegna: si sceglie la fascia di tinta che pesa di più
(contando ogni pixel per quanto è saturo, ignorando grigi, nero e bianco, e sommando le
fasce vicine perché un colore a cavallo di due non perda metà del suo peso). Il colore
finale viene poi riportato in un intervallo di saturazione e luminosità leggibile sul
fondo su cui finisce. Copertine senza un colore vero (bianco e nero, collage) non tingono
niente: resta il verde. Costo: 144 pixel una volta per traccia.
Verificato su copertine note — rosso pieno → rosso, blu → blu, teal con banda arancione →
teal, scala di grigi → nessuna tinta, e sul campo con la copertina vera in riproduzione.
Misure dopo la voce: private working set 5,98 MB, CPU a riposo 0,29 % di un core con la
musica in riproduzione (0,018 % della macchina, cioè 0,0 % nel Task Manager).

**2.2** — Il titolo scorre solo se sborda davvero e solo mentre il mouse è sopra: il timer
a 30 Hz nasce con l'hover e muore con lui, quindi a riposo non cambia niente (a riposo il
titolo lungo resta troncato con l'ellissi, come prima). La corsa è andata, pausa, ritorno,
senza salti, e riparte da capo al cambio traccia o se cambia lo spazio disponibile. Ai
bordi il testo sfuma invece di essere tagliato, e sfuma solo dal lato in cui continua
davvero: a fine corsa il bordo destro è netto. La dissolvenza è una maschera di opacità
(gradiente lineare su un layer D2D), non un rettangolo del colore di fondo, che su una
finestra semitrasparente si vedrebbe.
Costo misurato per disegno, barretta 266×45 a 96 DPI: 0,367 ms a riposo, 0,392 ms in hover
senza scorrimento, 0,635 ms in hover mentre scorre — cioè ~1,9 % di un core mentre si
tiene il mouse sopra, e nulla appena lo si toglie.
Misure dopo la voce (riquadro, musica in riproduzione, 60 s): private working set 5,46 MB,
CPU a riposo 0,026 % di un core (0,0016 % della macchina).

**2.3** — Un solo timer per tutte le transizioni brevi (`IDT_ANIM`, 16 ms), acceso quando
comincia qualcosa e spento non appena opacità, copertina e pillola sono tutte ferme: fuori
dalle transizioni non esiste. Al cambio traccia la copertina nuova compare sopra quella
vecchia in 220 ms — sovrapposta, non dissolte entrambe a metà, che le farebbe schiarire
tutte e due — e insieme a lei si sposta la tinta della progress bar, altrimenti il colore
scatterebbe mentre l'immagine sta ancora sfumando. Se non c'è una copertina nuova (player
chiuso) la vecchia svanisce sul placeholder. La pillola della taskbar si apre con una
curva smoothstep: `LayoutSpec::expanded` è diventato `expand` 0-1, e la larghezza del
testo entra proporzionalmente nella misura.
Verificato fotogramma per fotogramma: rosso → blu attraversa il rosa e il viola sia nella
copertina sia nella progress; la pillola passa da 125 a 315 px con le larghezze intermedie
giuste e il titolo che si scopre man mano.
Misure dopo la voce (barra, musica in riproduzione, 60 s): private working set 6,29 MB,
CPU a riposo 0,23 % di un core (0,015 % della macchina) — cioè come prima della voce, che
è il punto: a transizione finita il timer non c'è più.

**2.4** — Windows tiene due valori distinti: `SystemUsesLightTheme` è il colore della
shell, quindi della taskbar sotto la pillola, e `AppsUseLightTheme` è quello che le
applicazioni devono seguire quando disegnano sé stesse. Possono essere diversi, e la
barretta li usa per due cose diverse: il primo decide il contrasto della pillola, il
secondo il colore del fondo che la barretta si disegna da sola in overlay e docked. La
tinta della copertina viene riportata su un intervallo di luminosità diverso a seconda del
fondo, quindi funziona già in entrambi i temi. `WM_SETTINGCHANGE` rilegge e basta, senza
ricreare niente: prima l'overlay non si ridisegnava su quel messaggio, ora sì. In più
`[general] theme=auto|dark|light` per chi vuole forzarlo — sopra un gioco lo scuro è
preferibile a prescindere da come è configurato il sistema.
Verificato: barra e riquadro nei due temi (glifi bianchi sul velo scuro, neri su quello
chiaro), e il cambio a caldo sull'app in esecuzione, cambiando `AppsUseLightTheme` e
mandando `WM_SETTINGCHANGE` alla sola finestra di MiniBar — la barretta passa a chiaro e
torna a scuro senza riavviare. Nota tecnica: `WM_SETTINGCHANGE` non si può *postare* fra
processi (`PostMessage` fallisce con `ERROR_MESSAGE_SYNC_ONLY`), va mandato con
`SendMessageTimeout`.
Misure dopo la voce (barra, musica in riproduzione, 60 s dopo l'avvio): private working
set 6,21 MB, CPU a riposo 0,078 % di un core (0,005 % della macchina).

Nota sulla ripetibilità delle misure di CPU: dipendono da quanti cambi traccia cadono nella
finestra di misura. Un cambio traccia costa il decode della copertina, il calcolo della
tinta e 220 ms di dissolvenza; in una finestra da 60 s con due o tre cambi si arriva anche
a 0,5 % di un core, in una senza cambi si sta sotto 0,1 %. Fra un evento e l'altro il
processo consuma zero: campionando ogni 4 secondi si leggono 0,000 s di CPU per quasi tutti
i campioni. Le misure qui sopra sono tutte prese 60 s dopo l'avvio, in layout barra, con la
musica in riproduzione e senza hover.

## Fase 3 — Presentazione

Da fare quando il prodotto è nella forma finale della fase 2, così screenshot e GIF non
vanno rifatti.

| # | Attività | Fatto quando |
|---|---|---|
| 3.1 | README in inglese come principale; l'attuale diventa `README.it.md` | un anglofono capisce cos'è e come installarlo senza tradurre |
| 3.2 | GIF animata in testa al README (hover → controlli → cambio traccia → volume) | GIF < 5 MB, visibile su GitHub senza click |
| 3.3 | Tabella comparativa RAM/CPU/GPU vs alternative note | numeri misurati, metodologia dichiarata |
| 3.4 | Licenza MIT, topics, descrizione, social preview | pagina repo completa |
| 3.5 | Screenshot delle tre modalità (overlay, docked, taskbar) e dei due layout | cartella `docs/media/` |

## Fase 4 — Distribuzione

| # | Attività | Note | Fatto quando |
|---|---|---|---|
| 4.1 | GitHub Actions: build a ogni push | preset CMake per MSVC dei runner `windows-latest`, senza toolchain portable | badge verde sul README |
| 4.2 | Release v1.0 con exe allegato | l'exe è già autosufficiente | `gh release` pubblicata, exe scaricabile |
| 4.3 | Manifest winget | PR su `microsoft/winget-pkgs`, richiede release stabile con URL versionato | `winget install minibar` funziona |
| 4.4 | Manifest Scoop | bucket personale o PR su `extras` | `scoop install minibar` funziona |
| 4.5 | Avvio automatico opzionale | scorciatoia in `shell:startup` dal menu (niente registro, coerente col vincolo portable) | opzione nel menu contestuale, reversibile |

## Rimandate (valutare dopo il feedback)

- **GUI impostazioni** — tanto codice per poco valore finché l'INI è documentato.
- **Hotkey aggiuntive configurabili** (next/prev/play globali) — estensione facile, ma
  meglio farla su richiesta degli utenti.
- **Localizzazione dell'interfaccia** — l'interfaccia ha pochissimo testo; il menu può
  restare inglese.

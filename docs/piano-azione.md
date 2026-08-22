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

## Fase 1 — Generalizzazione multi-player

Cambia il posizionamento: da "companion per Spotify" a "overlay universale per qualsiasi
player". È il lavoro più sostanzioso e va fatto per primo perché ridefinisce nome delle
opzioni, README e descrizione del repo.

| # | Attività | Note | Fatto quando |
|---|---|---|---|
| 1.1 | Volume per-app agnostico | `volume.cpp` cerca la sessione audio dal PID della sessione SMTC attiva, non più da "Spotify" | la rotellina muove il volume di YouTube Music / Tidal / foobar2000 |
| 1.2 | Auto-minimize agnostico | `spotify.cpp` → `player.cpp`: minimizza/ripristina la finestra del player preferito, qualunque sia | con `preferred_app=` diverso da Spotify il comportamento è identico |
| 1.3 | Selettore di sessione | voce nel menu contestuale che elenca le sessioni SMTC attive e permette di bloccarsi su una | con due player attivi si sceglie quale mostrare |
| 1.4 | Sezione INI rinominata | `[spotify]` → `[player]` con retrocompatibilità in lettura | vecchi INI continuano a funzionare |

Fuori scope: integrazione con API web dei singoli servizi (like, playlist). Contraddice
il minimalismo e richiede segreti/OAuth.

## Fase 2 — Rifinitura estetica

Le due-tre cose che rendono bella la GIF del README. Tutte compatibili col rendering
software e con lo zero-redraw a riposo.

| # | Attività | Note | Fatto quando |
|---|---|---|---|
| 2.1 | Colore dominante dalla copertina | media/quantizzazione sui pixel della bitmap SMTC a ogni cambio traccia; tinta su progress bar e/o sfondo | il colore segue l'album, costo solo al cambio traccia |
| 2.2 | Marquee dei titoli lunghi | scorrimento con dissolvenza ai bordi, **solo in hover** | a riposo zero ridisegni, come oggi |
| 2.3 | Transizioni brevi | fade della copertina al cambio traccia, easing sull'espansione della pillola taskbar | timer attivo solo durante la transizione (poche centinaia di ms) |
| 2.4 | Tema chiaro/scuro dal sistema | lettura di `AppsUseLightTheme` + notifica `WM_SETTINGCHANGE` | la barretta segue il tema senza riavvio |

Criterio trasversale: dopo ogni voce, rimisurare RAM/CPU a riposo e confermare che i
numeri della §3 reggono.

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

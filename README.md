# MiniBar

Mini player musicale nativo per Windows 11: una barretta overlay che legge le API media
di sistema (SMTC), controlla la riproduzione e alleggerisce Spotify minimizzandolo.

La specifica completa — vincoli, budget prestazioni, decisioni architetturali — sta in
[docs/piano-implementazione.md](docs/piano-implementazione.md). Questo file spiega solo
come si compila e si usa.

## Requisiti

Nessuno. Non serve Visual Studio, non serve il Windows SDK installato, non servono
privilegi di amministratore: la toolchain vive dentro `tools\` ed è portable, come
l'applicazione.

## Compilare

```powershell
.\build.ps1                 # Release (default)
.\build.ps1 -Config debug
.\build.ps1 -Clean          # riparte da zero
```

L'eseguibile finisce in `dist\minibar.exe`. È autosufficiente: CRT statica, nessun
VC Redist, nessuna DLL accanto.

## Ricostruire la toolchain

`tools\` è già popolata. Per rifarla da zero su un'altra macchina, in quest'ordine:

```powershell
# 1. PowerShell 7 portable (serve per parsare il manifest VS da 29 MB)
curl.exe -sSL -o pwsh.zip https://github.com/PowerShell/PowerShell/releases/latest/download/PowerShell-7.6.5-win-x64.zip
Expand-Archive pwsh.zip -DestinationPath tools\pwsh

# 2. MSVC + Windows SDK, estratti dai pacchetti ufficiali Microsoft
tools\pwsh\pwsh.exe -File tools\get-msvc.ps1

# 3. CMake e Ninja: zip ufficiali estratti in tools\cmake e tools\ninja
```

`tools\get-msvc.ps1` legge il channel manifest di Visual Studio 2022, scarica i `.vsix`
di MSVC e gli `.msi` del Windows SDK e li estrae (`msiexec /a`, che non richiede
elevazione). Niente installer, niente registro, niente file fuori da `E:\CLAUDE\MINIBAR`.
`tools\get-sdk-extra.ps1` aggiunge altri MSI dell'SDK se dovessero servire.

`tools\_dl\` è solo cache di download: si può cancellare (~2 GB) senza rompere nulla.

Per usare la toolchain a mano in una shell:

```powershell
. .\tools\env.ps1     # imposta INCLUDE, LIB, PATH
```

## Configurazione

Al primo avvio `minibar.exe` crea `minibar.ini` accanto a sé (quindi in `dist\`).

```ini
[general]
mode=overlay          ; overlay | docked | taskbar
preferred_app=Spotify ; sottostringa del SourceAppUserModelId della sessione preferita

[overlay]
layout=bar            ; bar (barretta orizzontale) | tile (riquadro con la copertina)
bar_width=266         ; larghezza della barretta in DIP, 168-840
tile_size=92          ; lato del riquadro in DIP, 68-224
x=…  y=…              ; scritti al rilascio del drag
idle_opacity=140      ; 0-255, opacità a riposo

[docked]
edge=bottom           ; bottom | top | left | right

[taskbar]
anchor=auto           ; auto | left | center | right
                      ; auto: a sinistra se le app sono al centro,
                      ;       al centro se le app sono a sinistra

[spotify]
auto_minimize=1       ; minimizza Spotify all'avvio di MiniBar
hide_instead=0        ; 1 = SW_HIDE invece di SW_MINIMIZE (più aggressivo)
launch_if_missing=0   ; avvia Spotify con --minimized se non è in esecuzione

[volume]
wheel_step=4          ; punti percentuali di volume per scatto di rotellina, 1-25

[hotkey]
enabled=0
modifiers=alt+shift
key=M
```

## Le tre modalità

| | Overlay | Agganciata al bordo | Dentro la taskbar |
|---|---|---|---|
| Posizione | libera, trascinabile | striscia su un bordo dello schermo | nello spazio vuoto della taskbar |
| Spazio riservato | no | sì, le finestre massimizzate la rispettano | no |
| Click-through a riposo | sì | no | no |
| Opacità a riposo | ridotta (`idle_opacity`) | piena | piena |
| Controlli | all'hover | sempre | sempre |
| Sopra i giochi | sì | no | sì, finché la taskbar è visibile |
| Aspetti disponibili | barretta o riquadro | barretta | pillola compatta |
| Ingombro predefinito | 266×45 o 92×92 DIP | 45 DIP di altezza | altezza della taskbar |

In modalità taskbar la pillola mostra a riposo solo copertina, comandi e progress;
passandoci sopra si allarga e compaiono titolo e artista. Segue altezza, bordo, DPI e
tema della taskbar, e sparisce insieme a lei quando si nasconde o quando parte un'app a
schermo intero.

Si cambia modalità e aspetto dal menu col tasto destro, o dall'icona nella tray.

## Uso

| Azione | Come |
|---|---|
| Mostrare i controlli | passare il mouse sopra la barretta |
| Play/pausa, precedente, successivo | click sui pulsanti |
| Seek | click sulla progress bar (se il player lo consente) |
| Spostare la barretta | trascinarla (solo in overlay); la posizione si salva |
| **Alzare/abbassare il volume di Spotify** | **rotellina del mouse sopra la barretta** |
| **Mute di Spotify** | **click col tasto centrale sulla barretta** |
| Cambiare larghezza | **Ctrl** + rotellina sopra la barretta (solo in overlay), oppure `bar_width` / `tile_size` nell'INI |
| Cambiare modalità o aspetto | tasto destro → Modalità / Aspetto |
| Menu | tasto destro sulla barretta o sull'icona nella tray |
| Mostrare/nascondere | doppio click sull'icona nella tray, o hotkey se attiva |

La rotellina (volume e ridimensionamento) dipende dall'impostazione di Windows "scorri le
finestre inattive quando ci passo sopra", attiva per impostazione predefinita.

### Il volume è quello di Spotify, non quello di sistema

MiniBar muove la levetta di Spotify nel mixer di Windows (`ISimpleAudioVolume` sulla
sessione audio del processo), quindi il volume degli altri programmi non si tocca. Se
Spotify non ha una sessione audio — non è in esecuzione, oppure non ha ancora emesso un
suono — non c'è niente da muovere e la rotellina torna a ridimensionare la barretta come
prima, senza Ctrl. Il livello si mostra per un secondo e mezzo sulla striscia della
progress bar, in chiaro invece che in verde: nessun pannello che compare, nessun pixel in
più occupato.

Le interfacce Core Audio si creano al primo uso della rotellina, non all'avvio, e la
sessione trovata resta in cache finché non scade: chi non tocca mai il volume non paga
niente, né in memoria né in thread.

Chiudendo MiniBar la finestra di Spotify viene **sempre** ripristinata. Se un'esecuzione
precedente è morta male con Spotify nascosto, il ripristino avviene all'avvio successivo.

## Note di implementazione

### Rendering: software invece di DirectComposition

Il piano (§6.3) indica come strada raccomandata una swapchain di composizione DXGI +
DirectComposition, con `WS_EX_LAYERED` + `UpdateLayeredWindow` come ripiego accettabile.
È stata implementata e misurata prima la strada raccomandata, poi il ripiego:

| | D3D11 + DXGI + DirectComposition | D2D software + UpdateLayeredWindow |
|---|---|---|
| Memoria privata | 37,7 MB | **8,7 MB** |
| Private working set (Task Manager) | ~34 MB | **6,1 MB** |
| Thread | 26–29 | **10–12** |
| Handle | ~1120 | **~320** |
| CPU a riposo | 0,29 % di un core | **0,10 % di un core** |

Il costo del device D3D11 e del driver grafico non è ammortizzabile su una superficie di
266×45 px ridisegnata al massimo una volta al secondo, e sfondava il budget della §3
(< 15 MB) di oltre il doppio. La rasterizzazione software di Direct2D su quella superficie
costa frazioni di millisecondo e non tocca affatto la GPU, il che rende il requisito
"GPU 0 % fra un ridisegno e l'altro" strutturale invece che sperato.

### La modalità taskbar non è una deskband

Scrivere *dentro* la taskbar, come facevano le deskband (`IDeskBand`), richiede che
Explorer carichi una DLL nel proprio processo. Su Windows 11 quel meccanismo è stato
rimosso — la taskbar è XAML e non ospita estensioni di terze parti — e comunque sarebbe
injection, vietata dalla §2 del piano.

Quello che fa MiniBar è posizionare la propria finestra topmost *sopra* i pixel liberi
della taskbar. Visivamente è indistinguibile; tecnicamente resta una finestra separata,
quindi non scorre insieme alle icone delle app. Per stare dove c'è spazio legge
`TaskbarAl` (l'allineamento delle icone) e il rect di `TrayNotifyWnd` (dove comincia
l'area di notifica), entrambi in sola lettura e da fuori processo. Si registra come
appbar con `ABM_NEW` **senza** mai chiamare `ABM_SETPOS`: così riceve `ABN_POSCHANGED`,
`ABN_STATECHANGE` e `ABN_FULLSCREENAPP` senza sottrarre spazio allo schermo.

Le icone delle app sulla taskbar di Windows 11 non sono finestre e non sono enumerabili:
la posizione si sceglie dall'allineamento dichiarato dal sistema, non misurando dove
finiscono i pulsanti. Con moltissime app aperte e allineamento a sinistra, la pillola
centrata può finirci sopra: in quel caso si usa `anchor=right` nell'INI.

### Numeri misurati

| Metrica | Target §3 | Misurato |
|---|---|---|
| Private working set | < 15 MB | 6,1 MB (piatto su 2 min) |
| CPU a riposo | ≈ 0,0 % | 0,10 % di un core (0,006 % su 16 core) |
| Dimensione eseguibile | ≤ 2 MB | 226 KB |
| Avvio → barretta visibile | < 200 ms | 82 ms |
| GPU fra i ridisegni | 0 % | 0 % (nessun device GPU) |

### Stato delle verifiche

Verificati automaticamente: avvio e stili della finestra (topmost, noactivate, toolwindow,
layered), lettura SMTC con metadati e copertina, interpolazione della progress bar,
click-through con isteresi (`WindowFromPoint` restituisce la finestra sottostante a riposo
e la barretta in hover), drag con persistenza della posizione, modalità docked con 45 px di
spazio riservato e rilascio pulito allo shutdown, ripristino di Spotify all'uscita, volume
per-applicazione di Spotify mosso dalla rotellina (verificato leggendo la sessione dal
mixer: una sola sessione, stato attivo, livello effettivamente cambiato).

Restano da provare a mano, perché richiedono uno scenario reale: la permanenza sopra un
gioco in borderless dopo Alt-Tab ripetuti (M4) e l'impatto sul frametime con PresentMon.

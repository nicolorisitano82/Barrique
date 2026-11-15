# Fase 5: grafica, audio e video senza regressioni

Piano dettagliato della parte audio/video della fase 5 (l'input resta nella ROADMAP). Scritto il 2026-11-15 dopo uno studio di CrossOver e del Game Porting Toolkit di Apple (GPTK), riassunto in fondo.

## Regole contro le regressioni

Valgono per ogni passo qui sotto.

1. **Prima la base, poi la modifica.** Ogni passo parte misurando lo stato attuale con gli stessi test che userà alla fine. Senza numeri "prima" non si fa la modifica.
2. **Nessun controllo che passava può fallire dopo.** `tests/compare.py` (da scrivere) confronta due giri di `run-conformance.sh` controllo per controllo (file:riga, come `classify.py`): ogni controllo nuovo che fallisce, ogni unità che passa da ok a crash/timeout è una regressione. Il passo non si chiude finché le regressioni non sono zero o spiegate una per una nel registro del passo.
3. **La base di fase 4 resta sempre verde.** A ogni chiusura di passo si rilanciano ntdll, kernel32, user32, gdi32 sulle tre architetture e `tests/apps/run-apps.sh` (Python, Java, 7-Zip, VC++). È il "test di fumo" generale: grafica e audio toccano winemac.drv, win32u e la memoria della VM, cioè codice che usano tutti.
4. **Ogni componente nuovo dietro un interruttore, spento finché non passa il suo cancello.** Variabile d'ambiente e chiave di registro per backend (es. `BARRIQUE_D3D=wined3d|dxmt|dxvk`), così una regressione in un programma si aggira senza ricompilare, e si confrontano due backend sullo stesso programma.
5. **Un componente per commit, una patch per componente** in `patches/<componente>/`, così `git bisect` isola il colpevole.
6. **Tre architetture sempre.** Ogni test grafico/audio gira come ARM64, x86-64 (ARM64EC) e i386 (WoW64). `classify.py` dice subito se un problema è di FEX, del lato ARM64EC, della finestra a 32 bit o comune.
7. **Licenza controllata prima di scaricare o includere** (tabella in fondo).

## Passo 0: base di partenza ⬜

- Compilare i test x86-64 dei moduli multimediali in `build-x64tests` (oggi ci sono solo ARM64 e i386): d3d8, d3d9, d3d10core, d3d11, d3d12, dxgi, ddraw, opengl32, mmdevapi, dsound, winmm, xaudio2_7, mf, mfplat, mfreadwrite, quartz, windowscodecs, dwrite, d2d1, dinput, xinput1_3, hid.
- Giro 5 su questi moduli, tre architetture, con lo stato attuale (wined3d su OpenGL di Apple, CoreAudio, winedmo con FFmpeg di Homebrew). È la base di confronto per tutto il resto. I test grafici aprono finestre: va eseguito con la sessione grafica attiva.
- Scrivere `tests/compare.py` (regola 2).
- Esito atteso: numeri e crash annotati come in fase 4, divisi per causa.

## Passo 1: FreeType e font ⬜

Va prima di tutto il resto perché i fallimenti dei font sporcano i test di gdi32, user32 `text`, dwrite e d2d1, e nasconderebbero le regressioni vere.

- Caricare FreeType senza dipendere da `/opt/homebrew/lib` (percorso `@rpath`, anticipo della fase 6).
- Font: Liberation con i nomi di Arial, Times New Roman, Courier New; sostituzioni CJK verso i font di sistema.
- Cancello: gdi32 `font`, user32 `text`, dwrite migliorano; regressioni zero sul resto.

## Passo 2: audio ⬜

È il passo a rischio più basso: `winecoreaudio.drv` è già codice Wine nativo arm64 e i buffer audio sono memoria normale del processo.

- Test: mmdevapi, dsound, winmm, xaudio2_7 sulle tre architetture.
- Programma di prova nostro (`tests/av/tone.exe`, compilato per le tre architetture): suona un tono via WASAPI, DirectSound, waveOut e XAudio2, e misura i periodi mancati (`GetCurrentPadding`, callback in ritardo). Esce con RESULT ok/fail, come gli altri campioni.
- Da verificare:
  - frequenze 44,1 / 48 / 96 kHz e un dispositivo Bluetooth. I fruscii segnalati dagli utenti di CrossOver dipendono dalla frequenza del dispositivo;
  - latenza col periodo minimo di 5 ms: nella VM le attese su eventi passano per le syscall, misurare se il costo di fase 3 basta;
  - `AudioConverterNew` vuole le eccezioni di divisione per zero mascherate: controllare che lo stato FP dell'host non erediti quello del codice Windows;
  - registrazione: serve il permesso del microfono di macOS (TCC). Il comportamento quando manca va provato e documentato.
- Prestazioni: CPU del processo durante 60 s di riproduzione, da non peggiorare nei passi successivi.
- Cancello: test senza regressioni, `tone.exe` ok sulle tre architetture a 44,1 e 48 kHz, nessun periodo perso a 10 ms.

## Passo 3: video (Media Foundation e DirectShow) ⬜

Upstream Wine ha già il backend FFmpeg (`winedmo`), che nella nostra build è compilato. GStreamer non lo usiamo: CrossOver lo include ma senza i plugin WMA (segnalato dagli utenti), è più grande e non serve.

- FFmpeg compilato da noi, solo LGPL (niente `--enable-gpl`/`--enable-nonfree`), con VideoToolbox e AudioToolbox, dentro la `.app`.
- `winedmo` attivo di default (`DisableGstByteStreamHandler`).
- File di prova generati in locale con ffmpeg, niente download: H.264, HEVC, VP9, AAC, MP3, WMV/WMA, MPEG-2. Per ognuno la somma di controllo dei fotogrammi decodificati (`-f framemd5`) è il riferimento.
- Programma di prova `tests/av/mfplay.exe`: decodifica con IMFSourceReader e confronta le somme coi riferimenti. Per H.264/HEVC con VideoToolbox basta una tolleranza, non l'uguaglianza esatta.
- I fotogrammi decodificati in hardware sono IOSurface: vanno sempre **copiati** in memoria del processo, mai mappati nella VM (vedi passo 4).
- Bink e altri codec proprietari: nessun file di prova libero. Si verificano coi primi giochi.
- Cancello: mf, mfplat, mfreadwrite, quartz senza regressioni; `mfplay.exe` ok sulle tre architetture; nessuna dipendenza da Homebrew (`otool -L`).

## Passo 4: memoria della GPU e la VM, esperimento ⬜ ⚠️

**Questo rischio ce l'abbiamo solo noi.** CrossOver e il GPTK non usano una VM. La VM accetta solo memoria anonima e privata: gli altri tipi di pagina mandano il Mac in kernel panic (SPTM `VIOLATION_ILLEGAL_MAPPING_TYPE`). La memoria che la GPU condivide (MTLBuffer condivisi, IOSurface, memoria Vulkan mappata in modo persistente) è di un altro tipo. `host_page_is_safe` rifiuta oggi le pagine sospette, quindi il panic non dovrebbe arrivare, ma il codice Windows riceverebbe un errore.

Il passo decide **come il codice Windows vede la memoria della GPU**, prima di scrivere qualsiasi backend.

- **A, copia:** il lato Windows scrive in memoria propria, il lato host copia nel buffer Metal (stessa strategia di winevulkan per i processi a 32 bit). Sicura, costa banda.
- **B, importazione:** memoria anonima nostra, data a Metal con `newBufferWithBytesNoCopy` (o a MoltenVK con `VK_EXT_external_memory_host`). Niente copie, ma va verificato che Metal non cambi il tipo delle pagine.
- Esperimento in `hvproto`, mai dentro Wine: alloca, importa in Metal, interroga il tipo di pagina **senza** `hv_vm_map`. Solo se le pagine risultano identiche a prima si prova la mappatura. Ogni esecuzione con l'OK esplicito dell'utente e con il lavoro salvato: un errore qui fa ripartire il Mac.
- Cancello: strategia scelta e scritta qui, con le prestazioni misurate (MB/s copiati).

## Passo 5: Vulkan con MoltenVK ⬜

- Loader Vulkan e MoltenVK (Apache-2.0) compilati da noi, dentro la `.app`, versione fissata. CrossOver fa lo stesso.
- winevulkan con la strategia del passo 4; cache delle pipeline di MoltenVK.
- Test: vulkan-1 e i nostri campioni del passo 6.
- Cancello: test senza regressioni sulle tre architetture, base di fase 4 verde.

## Passo 6: banco di prova grafico ⬜

Si costruisce prima dei backend D3D, così ognuno nasce già misurato.

- **Campioni nostri** (`tests/gfx/`), compilati per ARM64, x86-64 e i386: disegnano una scena fissa fuori schermo con D3D9, D3D11, D3D12, OpenGL, Vulkan e DirectDraw, rileggono i pixel e li confrontano con un'immagine di riferimento con tolleranza. Non serve catturare la finestra, quindi girano anche senza nessuno davanti.
- I riferimenti si generano una volta, idealmente su un PC Windows; altrimenti dal primo giro buono, controllati a occhio.
- I test si eseguono col livello di validazione di Metal attivo (`MTL_DEBUG_LAYER=1`, `MTL_SHADER_VALIDATION=1`): un errore di validazione conta come fallimento. Per capire un problema: `MTL_HUD_ENABLED=1` e `gpucapture` (macOS 27), come consiglia Apple per il GPTK.
- **Prestazioni:** tempo per fotogramma di ogni campione, registrato a ogni giro; un peggioramento oltre il 5% va spiegato.
- **Programmi veri:** demo e giochi gratuiti scelti più avanti (con approvazione per i download), con un elenco fisso per ogni backend.

## Passo 7: Direct3D ⬜

L'ordine segue il rischio e quello che fa CrossOver nella sua anteprima Arm64 (FEX + DXMT, niente D3DMetal).

1. **wined3d su OpenGL di Apple** (quello che c'è oggi): resta il ripiego per tutto. Copre D3D 1-11 con i limiti del GL 4.1 di Apple.
2. **DXMT** (LGPL-2.1, D3D10/11 su Metal). Ha già un file di build ARM64EC: si compila in ARM64X, un solo binario per ARM64 e x86-64. Il suo lato host `winemetal.so` usa simboli esportati da winemac.drv nella versione di CrossOver: vanno aggiunti al nostro winemac.drv. È il backend D3D11 di default **solo dopo** che batte wined3d sul banco del passo 6 senza regressioni.
3. **D3D12:** VKD3D-Proton su MoltenVK (scelta di MetalSharp per arm64), da valutare dopo il passo 5. Metal Shader Converter di Apple (DXIL → Metal) è utilizzabile come libreria: è universale arm64/x86_64 e ridistribuibile "solo per la conversione degli shader".
4. **D3D9:** wined3d oppure DXVK su MoltenVK, decisione presa coi numeri del passo 6. Da verificare quali versioni di DXVK funzionano con le estensioni di MoltenVK.
5. **OpenGL 4.6 su Metal** ([MS] WineMetalGL): dopo, solo se servono programmi GL > 4.1.

**Scelta del backend per programma:** come CrossOver, ma pubblica. Un file di configurazione nel pacchetto (`backends.conf`: eseguibile o hash → backend), modificabile dall'utente; di default il backend con meno regressioni sul banco. Il wizard della fase 6b lo usa.

## Cosa NON prendiamo

| Cosa | Perché |
|---|---|
| **D3DMetal (GPTK)** | Tutte le versioni verificabili sono solo x86_64 e caricano la libreria di Rosetta: non possono girare in un processo arm64 senza Rosetta. La licenza vieta l'uso commerciale, le modifiche e il reverse engineering. CodeWeavers stessa, nella sua anteprima Arm64, non lo include. Rosetta inoltre resta completa solo fino a macOS 27. |
| Database proprietario dei backend di CrossOver | Non disponibile; lo sostituisce `backends.conf` con i numeri del banco. |
| GStreamer | FFmpeg via winedmo copre lo stesso e lo abbiamo già. |
| ESync | CrossOver 26 l'ha tolto; MSync resta un'idea [MS] per la ROADMAP. |

## Licenze

| Componente | Licenza | Nella `.app` |
|---|---|---|
| FFmpeg (senza parti GPL/nonfree) | LGPL-2.1 | sì |
| MoltenVK, loader Vulkan | Apache-2.0 | sì |
| DXMT | LGPL-2.1 | sì |
| VKD3D-Proton | LGPL-2.1 | sì |
| DXVK | zlib | sì |
| FAudio, SDL2 | zlib | sì |
| FreeType | FTL / GPL-2 | sì |
| Font Liberation | OFL-1.1 | sì |
| Metal Shader Converter | Apple, ridistribuibile solo per la conversione degli shader, solo su hardware Apple | da decidere |
| D3DMetal | Apple, non commerciale, niente modifiche | no |

## Studio: cosa fanno CrossOver e il GPTK (novembre 2026)

**CrossOver (CodeWeavers).** Versione stabile 26.3 (luglio 2026, Wine 11): Wine x86_64 sotto Rosetta. Per bottiglia si sceglie Auto (database proprietario per gioco, ripiego wined3d), DXMT (D3D11 su Metal), D3DMetal 3.0 (D3D11/12), DXVK su MoltenVK, o wined3d. DLSS emulato con MetalFX. Include MoltenVK, vkd3d upstream, FAudio, SDL, GStreamer. Audio con winecoreaudio.drv. L'anteprima Arm64 (luglio 2026, macOS 26.5+) usa una FEX adattata a macOS e DXMT arm64, senza D3DMetal né D3D12, ed è dichiarata solo per prove. CrossOver 27 sarà solo Apple Silicon e senza bottiglie a 32 bit. Il controllo di qualità pubblico è il database di compatibilità votato dagli utenti; nessuna CI documentata. Le regressioni note (crash D3DMetal su un gioco in 26.1) si aggirano cambiando backend: da qui la regola 4.

**Game Porting Toolkit (Apple).** GPTK 1 (2023): D3DMetal per D3D11/12 e un ambiente di valutazione basato su Wine. GPTK 2: ray tracing, AVX2. GPTK 3 (dicembre 2025): MetalFX, sparse. GPTK 4 (beta, giugno 2026): Metal 4, HDR, frame generation, repository open source con documentazione e strumenti. D3DMetal è una DLL Wine x86_64 più una libreria host x86_64 che si appoggia a Rosetta e a winemac.drv. Utili per noi: Metal Shader Converter, l'HUD di Metal, la validazione, `gpucapture`/`gpudebug` e il metodo di Apple di confrontare le catture con quelle di riferimento.

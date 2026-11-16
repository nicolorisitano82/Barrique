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

## Passo 0: base di partenza ✅

- Compilare i test x86-64 dei moduli multimediali in `build-x64tests` (oggi ci sono solo ARM64 e i386): d3d8, d3d9, d3d10core, d3d11, d3d12, dxgi, ddraw, opengl32, mmdevapi, dsound, winmm, xaudio2_7, mf, mfplat, mfreadwrite, quartz, windowscodecs, dwrite, d2d1, dinput, xinput1_3, hid.
- Giro 5 su questi moduli, tre architetture, con lo stato attuale (wined3d su OpenGL di Apple, CoreAudio, winedmo con FFmpeg di Homebrew). È la base di confronto per tutto il resto. I test grafici aprono finestre: va eseguito con la sessione grafica attiva.
- Scrivere `tests/compare.py` (regola 2).
- Esito atteso: numeri e crash annotati come in fase 4, divisi per causa.

**Fatto il 2026-11-15** (giro 5, `tests/results/round5`: `summary.txt`, `classify.txt`, script `run.sh av|gpu`). Test x86-64 di 27 moduli compilati (d3d8 esiste solo a 32 bit); `run-conformance.sh` salta i moduli senza eseguibile; `tests/compare.py` scritto e provato sul giro 3 contro il giro 4. Nessun kernel panic.
- **Audio pulito sulle tre architetture:** mmdevapi, dsound, xaudio2_7, xaudio2_8 a 0 fallimenti; winmm circa 280 (uguali ovunque). Anche windowscodecs, hid, xinput1_3 a 0.
- **Media Foundation:** mfplat ok (4 fallimenti su i386). mf, mfreadwrite, evr, dxva2, mfmediaengine, wmvcore vanno in crash o timeout.
- **dwrite:** crash sulle tre architetture, FreeType non caricato (passo 1).
- **Grafica a 64 bit:** d3d9, d3d10core, d3d11, d3d12, dxgi, ddraw, opengl32, d2d1 in crash entro 2 s su ARM64 e x86-64. Causa trovata con un rilancio di d3d11 (`round5/diag`): `glGetString` restituisce al codice Windows un puntatore nella shared cache di macOS (`0x1E5D7090E`), memoria che la VM non mappa, e ucrtbase va in access violation leggendo la stringa. Nei processi WoW64 opengl32 copia già le stringhe in memoria del processo; a 64 bit no.
- **Grafica a 32 bit:** i test d3d non vanno in crash ma saltano quasi tutti i controlli; opengl32 gira con 157 fallimenti.
- **Bug collegato:** dopo l'access violation parte winedbg, e quel processo muore subito lato host con lo stack del thread in memoria non mappata (report `wine-*.ips` di macOS, un thread, solo `ntdll.so` caricato). Da capire: oggi ogni crash Windows non mostra il debugger. *Corretto nel passo 1: era `fork()` da un thread Windows.*
- Il passo 0 conta questi numeri come base. La grafica a 64 bit si potrà misurare davvero solo dopo la correzione delle stringhe GL e della mappatura dei buffer GL (passo 4: stesso problema dei puntatori dell'host, che la VM non vede).

### Correzioni grafica a 64 bit sotto HVF (2026-11-15, solo ARM64 provato)

Tre bug, trovati con i rilanci in `round5/diag`:

1. **Puntatori dell'host al codice Windows (opengl32).** `glGetString`/`glGetStringi` restituivano stringhe nella shared cache; `glMapBuffer*` e `glGetBufferPointerv` restituivano mappe del driver. Con `WINE_HVF=1` i processi a 64 bit usano ora la stessa copia dei processi WoW64: stringhe copiate in un pool di `NtAllocateVirtualMemory` (cache per contenuto), buffer mappati con la copia di `wow64_map_buffer`. File: `unix_wgl.c`, `unix_private.h`, e **`unix_thunks.c` modificato a mano** (34 thunk): `make_opengl` non è aggiornato, rigenerarlo cancella la correzione.
2. **Deadlock in winemac (`OnMainThread`).** `-[WineContentView viewWillDraw]` gira sul thread principale e chiama `macdrv_update_opengl_context`, che chiama `OnMainThread`: il blocco va in coda al thread principale e lo stesso thread aspetta il semaforo, per sempre. Bug anche in Wine upstream 11.22. Ora `OnMainThread` esegue subito il blocco se è già sul thread principale (`cocoa_event.m`). Causava i blocchi casuali del test opengl.
3. **Use-after-free di `WineOpenGLContext` (winemac).** `-[NSOpenGLContext dealloc]` chiama `-setView:`/`-clearDrawable` sull'oggetto che si sta distruggendo; i nostri override catturavano `self` in un blocco per `OnMainThread`, il blocco faceva retain su un oggetto morto e la release sul thread principale finiva in memoria liberata (`objc_release`, trovato con `NSZombieEnabled` e una traccia di retain/release). Ora i blocchi usano un riferimento non trattenuto (`cocoa_opengl.m`). Il thread colpito veniva chiuso con `abort_thread` mentre teneva il lock `wined3d_cs`: da qui il blocco di d3d11.

Più uno strumento: con `WINE_HVF` un'eccezione nel codice host scrive lo stack host nel log (`dump_host_backtrace` in `signal_arm64.c`, frame pointer + `dladdr`).

Risultati ARM64 (aarch64): d3d11 completo e stabile, 3931 controlli, 17 fallimenti, 191 saltati (prima: crash in 2 s, poi blocco). opengl stabile in 3 s, 37 fallimenti, poi crash fisso in `test_framebuffer` (riga 3803): il test chiama `glGetFramebufferParameteriv` (GL 4.3) che nel profilo GL 2.1 legacy di Apple è un puntatore NULL. Limite del test con GL 2.1, non di Barrique; wined3d qui crea solo device di livello 9.x per lo stesso motivo (profilo legacy). Il crash host all'uscita dei processi (report `wine-*.ips` con stack non mappato) era `fork()` da un thread Windows: corretto nel passo 1.

**Giro 6 (2026-11-16, `tests/results/round6`, moduli grafici, tre architetture), confronto con il giro 5: nessuna regressione, 36 unità migliorate.**
- ARM64 e x86-64 ora uguali: d3d11 3931 controlli e 17 fallimenti; d3d10core, d3d12, dxgi, d2d1 ok; d3d9 circa 390 000 controlli e 40 fallimenti; ddraw circa 90 000 controlli e 357 fallimenti (ddraw1 ancora in timeout). Prima: tutto in crash entro 2 s.
- opengl32 a 64 bit: ancora "crash", per la chiamata GL 4.3 del test (sopra).
- i386 invariato (dxgi in crash anche nel giro 5, ddraw1/2/4/7 in timeout).
- `compare.py` corretto: se l'unità del giro di base era in crash o timeout, i controlli falliti non sono confrontabili (il punto in cui si fermava dipende dai tempi), anche se l'unità resta in crash o timeout. Prima segnalava 218 falsi "nuovi".

## Passo 1: FreeType e font ✅

Va prima di tutto il resto perché i fallimenti dei font sporcano i test di gdi32, user32 `text`, dwrite e d2d1, e nasconderebbero le regressioni vere.

- Caricare FreeType senza dipendere da `/opt/homebrew/lib` (percorso `@rpath`, anticipo della fase 6).
- Font: Liberation con i nomi di Arial, Times New Roman, Courier New; sostituzioni CJK verso i font di sistema.
- Cancello: gdi32 `font`, user32 `text`, dwrite migliorano; regressioni zero sul resto.

**Fatto (2026-11-16).**
- `win32u/freetype.c`: su macOS prova prima `dlopen("@rpath/libfreetype.6.dylib")`; `win32u.so` ha l'rpath `@loader_path/../../deps` (albero di build: `build-arm64ec/deps`, installazione: `lib/deps`). Se non la trova, cade sul vecchio percorso.
- `scripts/bundle-deps.sh <build o lib>`: copia in `deps/` FreeType 2.15.0 e libpng 1.6.59 di Homebrew, riscrive i nomi di installazione (`@rpath/…`, libpng accanto a FreeType con `@loader_path`), firma ad hoc e fallisce se resta un percorso esterno. **Le due librerie di Homebrew hanno minos 26.0**: per supportare macOS 15 vanno ricompilate dai sorgenti (fase 6).
- Font Liberation non servono: macOS ha Arial, Times New Roman e Courier New in `/System/Library/Fonts/Supplemental`, che Wine carica già tramite CoreText. Sostituzioni CJK fatte nel giro 9 (vedi sotto).

**Giro 7 (`tests/results/round7`, gdi32 user32 dwrite d2d1 usp10, tre architetture), prima e dopo FreeType: 35 unità migliorate, nessuna peggiorata, 1990 controlli corretti.**
- usp10: da crash a ok ovunque. dwrite: analyzer e font da crash a ok, layout da crash a 7 fallimenti.
- gdi32: metafile e dib ok; font esce dal timeout (39 fallimenti su ARM64 e x86-64).
- user32: text da timeout a ok (78 controlli corretti); combo, dialog, listbox, menu ok.
- 5 controlli "nuovi" spiegati: `edit.c:1485` dipende dalla misura del font (falliva già nella base i386), `clipboard.c:1362` era già instabile nel giro 3, i due di `input.c` (focus) e `input.c:4635` (posizione del mouse) spariscono rilanciando.
- `run-conformance.sh` corretto: non leggeva il riepilogo al singolare ("1 failure") e segnava ok l'unità. I tsv dei giri vecchi hanno lo stesso difetto (57 log); `tests/fixtsv.py` li sistema leggendo i log.

**Correzione trovata dal test di fumo (regola 3): fork() da un thread Windows sotto HVF.** Gli installer VC++ fallivano ("Failed to create window"): la prima finestra la crea un thread secondario, Wine lancia `explorer.exe /desktop` e il figlio di `fork()` va in crash subito. La memoria Windows non è ereditata dai figli di `fork()` (`VM_INHERIT_NONE`, vedi `hvf.c`), e gli stack dei thread Windows sono memoria Windows. `ntdll/unix/process.c` ora ha `fork_child()`: sotto HVF fa `fork()` da un pthread host con gli argomenti copiati nella heap host; la usano `spawn_process`, `fork_and_exec` e `__wine_unix_spawnvp`. Era anche il "crash host all'uscita dei processi" (report `wine-*.ips` a un thread con stack non mappato): 108 report il 16/11 prima della correzione, nessuno dopo.

Test di fumo dopo la correzione (`round7/smoke2`, `round7/apps2.out`):
- app: Python 32/64, Java 21 e 8, 7-Zip x64/x86, VC++ x64/x86 tutti ok.
- user32 e gdi32: nessuna regressione rispetto al giro 7 "dopo".
- ntdll e kernel32 rispetto al giro 3: 64 controlli corretti; restano `i386 ntdll threadpool` (tempi, a volte fail a volte timeout) e `x86_64 kernel32 debugger` in crash (`c0000354`, single-step x86), stabile ma non legato a questo passo: da indagare a parte. `info.c:2003` è un contatore di memoria che cambia a ogni giro.

### `x86_64 kernel32 debugger` (2026-11-16)

Non era una regressione: anche nel giro 3 il processo del test moriva alla riga 1796, ma il runner aveva preso il riepilogo di un processo figlio ("9 tests executed") e l'aveva segnato ok. Corretto il runner: il processo principale di un test esce con il numero dei suoi fallimenti (al massimo 255), quindi se il codice di uscita non coincide con i fallimenti dell'ultimo riepilogo quel riepilogo è di un figlio e l'unità è "crash". I tsv dei giri precedenti non hanno il codice di uscita e restano come sono.

Due bug di Wine trovati sotto (ci sono anche upstream), corretti:
- **winedbg**: se capstone non decodifica l'istruzione al PC (codice x64 in un processo ARM64EC letto come ARM64), usava lo stesso `insn[0]` e andava in crash (`strlen`). Ora stampa `.word 0x…` (`be_arm64.c`) o `(bad)` (`memory.c`, x86). Il controllo `debugger.c:761` (codice di uscita del figlio che va in crash) passa.
- **wineserver**: con `DBG_REPLY_LATER` su un thread sospeso ripassava anche gli eventi già rimandati e chiamava `resume_process` una seconda volta; un evento rimandato non tiene il processo sospeso, quindi `assert(process->suspend > 0)` fermava il server. `server/debugger.c` salta gli eventi `EVENT_DELAYED`.

**Limite noto, breakpoint x64 sotto FEX.** Su x64 Windows il debugger vede Rip dopo `int3` (+1) e il −1 si applica solo passando l'eccezione ai gestori del programma. FEX nel ramo ARM64EC (`RaiseException` in `Source/Windows/ARM64EC/Module.cpp`) fa `Pc -= 1` prima di `NtRaiseException`, cioè prima del debugger (il commento dice che Windows ARM64EC fa lo stesso). Il debugger vede il PC sull'`int3`, continuando lo riesegue, e i test di single-step e dei breakpoint dei thread si sfasano: il test arriva alla riga 1980 e muore (contesto di un thread nullo). Nel ramo WoW64 FEX lascia il −1 a wow64.dll e i386 passa quasi tutto. Uguale con Wine upstream e FEX su Linux; FEX non si tocca. Si potrebbe compensare in `NtRaiseException` di Wine (PC+1 verso il debugger per i breakpoint x64 ARM64EC), da valutare se un debugger x64 reale ne ha bisogno.

**Giro 9 (2026-11-16, `tests/results/round9`): compensazione dei breakpoint x64 e font CJK.**
- `ntdll/unix/thread.c`, `NtRaiseException` (solo aarch64): se l'eccezione è `EXCEPTION_BREAKPOINT`, il PC coincide con `ExceptionAddress` (l'`int3` su cui FEX l'ha riportato) ed è codice emulato (`is_emulated_code`), il debugger vede PC+1 come su x64 Windows; se il debugger non la gestisce, il PC torna sull'`int3` per i gestori del programma. Stesso schema di `setup_raise_exception` in `signal_x86_64.c`. i386 (WoW64) e ARM64 non toccati.
- Risultato, kernel32 `debugger` x86_64: 16 controlli in meno che falliscono (righe 1716-1744: single-step, breakpoint, EFlags), nessuno nuovo. Il test muore ancora alla riga 1980 per un altro motivo: il debugger scrive un `int3` nel codice del figlio con `WriteProcessMemory` e FEX nel figlio continua a eseguire la traduzione vecchia, perché `NtFlushInstructionCache` verso un altro processo non è implementato in Wine ("other process not supported"). Serve un APC di sistema che faccia invalidare la cache a FEX nel processo destinazione; riguarda anche mod loader e trainer. Aperto. Resta anche la riga 1753 (`int 2d` BREAKPOINT_PROMPT segnalato al debugger).
- `win32u/font.c`, `load_mac_cjk_replacements` (solo macOS): sostituzioni integrate, come una chiave `Fonts\Replacements` predefinita, dopo quelle del registro; un font vero con quel nome o una sostituzione nel registro hanno la precedenza. Giapponese: MS Gothic, MS PGothic, MS UI Gothic, Meiryo, Yu Gothic → Hiragino Sans; MS Mincho, Yu Mincho → Hiragino Mincho ProN. Cinese semplificato: SimSun, NSimSun → Songti SC (poi STSong, Hiragino Sans GB); SimHei, Microsoft YaHei, DengXian → PingFang SC (poi Hiragino Sans GB, Heiti SC). Tradizionale: PMingLiU, MingLiU → Songti TC; Microsoft JhengHei → PingFang TC. Coreano: Gulim, Dotum, Malgun Gothic → Apple SD Gothic Neo; Batang, Gungsuh → AppleMyungjo. PingFang è un asset scaricato da macOS, per questo ogni voce ha alternative installate di sicuro. Anche i collegamenti di sistema di Wine ("MS UI Gothic", "SimSun") ora trovano un font.
- `tests/av/cjkfont.c`: per 17 famiglie, il font creato deve mantenere il nome Windows e avere il glifo CJK. Ok sulle tre architetture.
- Confronto con `round8/smoke` (kernel32, gdi32, user32) e dwrite: unica differenza attesa gdi32 `font.c:5698/5700` (tre architetture): il test controlla che "SimSun" enumerato abbia SimSun nella tabella dei nomi del file, che invece dice "Songti SC". È il comportamento di ogni sostituzione di Wine; l'alternativa (sostituzione semplice) farebbe restituire "Songti SC" a `GetTextFace`. Il resto: debugger i386 da crash a timeout (al limite dei 180 s, stessi controlli), kernel32 `loader` aarch64 che alterna crash e timeout, controlli di `input.c` già instabili.

Test di fumo dopo le due correzioni (`round7/smoke3`, `apps3.out`), confronto con `smoke2`: app tutte ok; nessun controllo nuovo che non fosse già comparso nei giri precedenti (`menu.c:2472` i386, `input.c` da mouse su ARM64, `kernel32 loader` ARM64 che alterna crash e timeout con lo stesso dump di winedbg).

## Passo 2: audio 🔧

**In corso (2026-11-16).** Giro di base (`tests/results/round8/base`), giro dopo la correzione di coreaudio (`after`, `compare.txt`: nessuna regressione, 6 unità migliorate) e giro col salvataggio del registro in un figlio (`fork`, `smoke`, `apps.out`). Restano da provare 96 kHz e un dispositivo Bluetooth.
- `tests/av/tone.c` scritto e compilato per aarch64, x86_64, i686 (`samples/phase5/tone-*.exe`): WASAPI, waveOut, DirectSound, XAudio2, periodi persi e risvegli in ritardo, periodo del motore audio.
- Base: `tone.exe` ok a 44,1 e 48 kHz sulle tre architetture tranne un periodo perso (buco di 73 ms) in WASAPI x86-64 a 44,1 kHz. DirectSound e XAudio2 si risvegliano entro 6-9 ms; WASAPI e waveOut ogni 20-25 ms, da confrontare col periodo del dispositivo (ora stampato da `tone.exe`).
- Test: mmdevapi, dsound, xaudio2_7 quasi puliti. Fallimenti di base: `render.c:1282` (evento segnalato prima di `Start`, tre architetture), `xaudio2.c:1514` (voce WMA: serve il decoder, passo 3), `wave.c:1281` (formato a 4 canali: `MMSYSERR_ERROR` invece di `WAVERR_BADFORMAT`), winmm `mci` 274 (nessun dispositivo MCI), `spatialaudio` e `capture` che dipendono dal dispositivo di uscita (ora a 48 kHz) e dal microfono, cambiati rispetto al giro 5: la base del passo è il giro 8, non il 5.
- Correzione `winecoreaudio.drv/coreaudio.c` (bug anche upstream): un `Start` fallito perché l'handle dell'evento mancava avviava comunque il timer, che poi segnalava l'evento prima del vero `Start`. Ora il timer parte solo se `Start` riesce.
- **Buco periodico di 60-75 ms ogni ~20 s** (WASAPI 44,1 kHz, tutte le architetture; log in `round8/gap`). Tracciato: `NtSetEvent` dal thread timer di coreaudio resta bloccato ~55 ms nel wineserver. Il wineserver è a thread singolo e ogni 30 s salva il registro in modo sincrono (`system.reg` del prefisso di test è 4,3 MB): tutti i client aspettano. La priorità tempo reale Mach sul thread timer non serviva (provata e tolta).
- Correzione `server/registry.c` (solo macOS): il salvataggio periodico gira in un processo figlio (`fork`), che scrive i rami sporchi ed esce; il padre li segna puliti e continua. Se il figlio fallisce, i rami tornano sporchi (`registry_save_child_exited`, chiamata dal `sigchld_callback` di `server/mach.c`, che prima era `assert(0)`). Un solo figlio alla volta; `flush_registry` (uscita del server) aspetta il figlio prima del salvataggio finale. Se `fork` fallisce si salva come prima.
- Verifica (`round8/verify-fork.sh`): `tone.exe` WASAPI 44,1 kHz 20 s, 2 run per architettura: 5 su 6 senza periodi persi (salto massimo 17-27 ms), uno aarch64 con un solo buco di 38 ms (prima uno da ~62 ms ogni 20 s). Test audio contro la base (`compare-fork.txt`): 5 unità migliorate; i due peggioramenti sono `capture` i386 (dsound, winmm) col "Microfono di iPhone" (Continuity) comparso durante il giro, che fa fallire anche la base aarch64 winmm. Test di fumo contro `round7/smoke3` (`smoke-compare.txt`): i "crash" nuovi (kernel32 `virtual`, ntdll `info`) hanno gli stessi fallimenti della base, che era stata girata prima del controllo sul codice d'uscita del runner; `virtual.c:1697` aarch64 dipende dall'indirizzo assegnato. App tutte ok, nessun file temporaneo del registro rimasto, `system.reg` salvato.


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
- **Dispositivo a 96 kHz (2026-11-16, `round8/r96`, `run96.sh`)**: `tone.exe` con tutte e quattro le API, 20 s, a 44,1/48/96 kHz sulle tre architetture: 33 su 36 senza periodi persi; un periodo perso di ~31 ms in WASAPI aarch64 a 48 e 96 kHz (il buffer WASAPI è di 30 ms) e uno all'avvio in XAudio2 i686 a 44,1 kHz. Il periodo del dispositivo resta 10 ms. Test audio contro `fork`: nessuna regressione, tre `capture` migliorati (microfono dell'iPhone assente). Resta da provare un dispositivo Bluetooth.

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
| libpng | libpng-2.0 | sì |
| Font Liberation | OFL-1.1 | sì |
| Metal Shader Converter | Apple, ridistribuibile solo per la conversione degli shader, solo su hardware Apple | da decidere |
| D3DMetal | Apple, non commerciale, niente modifiche | no |

## Studio: cosa fanno CrossOver e il GPTK (novembre 2026)

**CrossOver (CodeWeavers).** Versione stabile 26.3 (luglio 2026, Wine 11): Wine x86_64 sotto Rosetta. Per bottiglia si sceglie Auto (database proprietario per gioco, ripiego wined3d), DXMT (D3D11 su Metal), D3DMetal 3.0 (D3D11/12), DXVK su MoltenVK, o wined3d. DLSS emulato con MetalFX. Include MoltenVK, vkd3d upstream, FAudio, SDL, GStreamer. Audio con winecoreaudio.drv. L'anteprima Arm64 (luglio 2026, macOS 26.5+) usa una FEX adattata a macOS e DXMT arm64, senza D3DMetal né D3D12, ed è dichiarata solo per prove. CrossOver 27 sarà solo Apple Silicon e senza bottiglie a 32 bit. Il controllo di qualità pubblico è il database di compatibilità votato dagli utenti; nessuna CI documentata. Le regressioni note (crash D3DMetal su un gioco in 26.1) si aggirano cambiando backend: da qui la regola 4.

**Game Porting Toolkit (Apple).** GPTK 1 (2023): D3DMetal per D3D11/12 e un ambiente di valutazione basato su Wine. GPTK 2: ray tracing, AVX2. GPTK 3 (dicembre 2025): MetalFX, sparse. GPTK 4 (beta, giugno 2026): Metal 4, HDR, frame generation, repository open source con documentazione e strumenti. D3DMetal è una DLL Wine x86_64 più una libreria host x86_64 che si appoggia a Rosetta e a winemac.drv. Utili per noi: Metal Shader Converter, l'HUD di Metal, la validazione, `gpucapture`/`gpudebug` e il metodo di Apple di confrontare le catture con quelle di riferimento.

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

## Passo 1b: qualità del testo ✅ (parziale)

Il difetto più visibile su un Mac non è FreeType ma la risoluzione: con `LogPixels=96` e senza `RetinaMode`, Wine disegna a 1× e macOS ingrandisce la finestra 2×, quindi ogni testo è sfocato. Ordine deciso (2026-11-17):
1. **Retina nativo**: `HKCU\Software\Wine\Mac Driver` `RetinaMode=y` e `LogPixels=192` nei prefissi creati dalla `.app`. Programmi che non dichiarano di gestire i DPI: verificato nel giro 12, Wine li scala 2× (vedi sotto). Da verificare: test user32 di finestre/DPI a 192, app di prova.
2. **Hinting e contrasto**: `FontSmoothing` in scala di grigi (ClearType subpixel non serve su Retina), `FontSmoothingGamma`, interprete TrueType di FreeType (`truetype:interpreter-version=35` contro il v40 predefinito), da scegliere confrontando le immagini.
3. **Font con le metriche di Windows**: Selawik (Segoe UI, OFL), Carlito (Calibri, OFL), Caladea (Cambria, Apache) nella `.app` e nelle sostituzioni integrate di `win32u/font.c` come i CJK; `FontLink\SystemLink` di Segoe UI e Tahoma verso i font Apple per simboli e scritture non latine. Download da approvare.
4. **DirectWrite**: lista di fallback di `dwrite` per macOS (PingFang, Hiragino, Apple Color Emoji, Apple Symbols), oggi pensata per Linux.

Escluso: disegnare con CoreText invece di FreeType (metriche diverse da Windows, layout rotti, molto codice nostro).

Misura: programma di prova `tests/av/textshot.exe` (tabella di font e dimensioni, GDI e DirectWrite, salvata in PNG) confrontato prima/dopo e, se disponibile, con un riferimento generato su Windows; test gdi32 `font`, user32 `text`, dwrite senza regressioni.

**Fatto (2026-11-17, giro 11, `tests/results/round11`).**
- **Retina**: `scripts/prefix-defaults.reg` (`RetinaMode=y`, `LogPixels=192` utente e macchina, `FontSmoothing=2` con `FontSmoothingType=1`, scala di grigi), da applicare ai prefissi della `.app`; non in `wine.inf` perché i test di conformità si aspettano 96 DPI. Provato su un prefisso nuovo (`prefix-retina`): DPI 192, schermo 3024×1964, testo nitido invece che ingrandito (`round11/after/textshot-compare.jpg`). Da provare ancora: app reali, test user32 con 192 DPI.
- **Hinting e gamma**: a 192 DPI l'interprete TrueType 35 e `FontSmoothingGamma=1800` cambiano poco (gamma un po' più scura): restano i valori predefiniti (v40, gamma standard).
- **Font con le metriche di Windows**: `scripts/fetch-fonts.sh` scarica a commit fissato, con somme SHA-256, Selawik 1.01 (Segoe UI, 5 pesi, OFL, nome riservato: file non modificati), Carlito (Calibri) e Caladea (Cambria) da google/fonts (OFL); `bundle-deps.sh` li copia nella cartella `fonts` di Wine (`share/wine/fonts` installato).
- **Sostituzioni in `wine.inf`** (`HKCU\Software\Wine\Fonts\Replacements`, MULTI_SZ, non sovrascrivono valori esistenti), lette sia da GDI sia da DirectWrite: Segoe UI e i suoi pesi → Selawik, Segoe UI Symbol → Apple Symbols, Calibri → Carlito, Cambria → Caladea, Consolas → Menlo, e le famiglie CJK (come in `win32u/font.c`, che resta per i prefissi vecchi). DirectWrite chiama le famiglie Hiragino col peso ("Hiragino Sans W3", "Hiragino Mincho ProN W3", "Hiragino Sans GB W3"): sono tra i candidati. Segoe UI Emoji non è sostituito: chi lo chiede si aspetta le tabelle a colori COLR/CPAL, Apple Color Emoji usa `sbix` e il test dwrite `font` (come un programma) andava in crash.
- **Fallback di DirectWrite** (`dwrite/analyzer.c`): i font macOS dopo i Noto in 41 voci (PingFang, Hiragino, Songti, Apple SD Gothic Neo, Geeza Pro, Arial Hebrew, Thonburi, Kohinoor, Apple Symbols) e una voce per le emoji (Apple Color Emoji).
- **`tests/av/textshot.c`**: 13 famiglie a 9/11/14 pt con GDI e DirectWrite in BMP, più DPI, font scelto, larghezze e fallback per carattere. Risultato: tutte le famiglie trovate in GDI e DirectWrite, fallback giapponese e cinese → Hiragino Sans W3, coreano → Apple SD Gothic Neo, simboli → Apple Symbols, emoji → Apple Color Emoji; identico su aarch64, x86_64, i686.
- Test (contro il giro 9): gdi32, user32 senza novità (resta `menu.c:2472`, già instabile); dwrite `layout` 3 controlli corretti; dwrite `font` ok (479.119 controlli, 0 fallimenti) ma ora dura 176-193 s per i font in più: col limite di 180 s del runner va in timeout su x86_64 e i386, con 300 s passa.
- **Programmi non DPI-aware (giro 12)**: `tests/av/dpitest.c` apre una finestra per contesto (non DPI-aware, system-aware, per-monitor v2) con cornice 300×150 logica, testo a 10 pt e scacchiera di 1 pixel; `tests/av/winbounds.swift` legge da macOS la dimensione reale delle finestre (CGWindowList, senza permesso di registrazione schermo). In `prefix-retina` le tre finestre misurano 340×232 punti (680×464 pixel): la non DPI-aware vede 96 DPI e 340×200, e Wine la ingrandisce 2× come Windows (un po' sfocata, come su Windows); le altre due vedono 192 DPI, 680×400, testo nitido. Uguale su aarch64, x86_64, i686. A 96 DPI (`prefix-tests`) tutte a 340×200.
- Aperti: ebraico, arabo e thai in DirectWrite ricadono su Tahoma (che li ha); il default nella `.app` (fase 6).

## Passo 2: audio ✅

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
- **Bluetooth (2026-11-17, giro 14, `tests/results/round14`, AirPods)**: `tone.exe` con le quattro API a 48 e 44,1 kHz sulle tre architetture: nessun periodo perso (`bt.log`). Test audio (`bt.sh`): nessuna regressione dovuta al Bluetooth, winmm `mci` migliorato; i timeout di `capture` i386 (dsound, winmm) e i fallimenti di `wave`/`midi` sono quelli già noti (microfono Continuity dell'iPhone, 3 o 4 dispositivi a seconda del momento). All'ascolto si sentivano dei buchi: sono i cambi di API di `tone.exe`, che chiude e riapre il flusso, e l'apertura del microfono che porta le AirPods in HFP. Prova di 60 s continui (`bt2.sh`): `afplay` e Wine WASAPI senza interruzioni udibili, WASAPI 5576 periodi, 0 persi, 1 risveglio in ritardo (22 ms).

**Chiuso (2026-11-17).** Cancello rispettato: test senza regressioni, `tone.exe` ok sulle tre architetture a 44,1 e 48 kHz (anche 96 kHz e Bluetooth). Restano fuori dal passo: latenza col periodo minimo di 5 ms, stato FP per `AudioConverterNew`, comportamento senza permesso del microfono.

## Passo 3: video (Media Foundation e DirectShow) ✅

Upstream Wine ha già il backend FFmpeg (`winedmo`), che nella nostra build è compilato. GStreamer non lo usiamo: CrossOver lo include ma senza i plugin WMA (segnalato dagli utenti), è più grande e non serve.

- FFmpeg compilato da noi, solo LGPL (niente `--enable-gpl`/`--enable-nonfree`), con VideoToolbox e AudioToolbox, dentro la `.app`.
- `winedmo` attivo di default (`DisableGstByteStreamHandler`).
- File di prova generati in locale con ffmpeg, niente download: H.264, HEVC, VP9, AAC, MP3, WMV/WMA, MPEG-2. Per ognuno la somma di controllo dei fotogrammi decodificati (`-f framemd5`) è il riferimento.
- Programma di prova `tests/av/mfplay.exe`: decodifica con IMFSourceReader e confronta le somme coi riferimenti. Per H.264/HEVC con VideoToolbox basta una tolleranza, non l'uguaglianza esatta.
- I fotogrammi decodificati in hardware sono IOSurface: vanno sempre **copiati** in memoria del processo, mai mappati nella VM (vedi passo 4).
- Bink e altri codec proprietari: nessun file di prova libero. Si verificano coi primi giochi.
- Cancello: mf, mfplat, mfreadwrite, quartz senza regressioni; `mfplay.exe` ok sulle tre architetture; nessuna dipendenza da Homebrew (`otool -L`).

**Cambio di piano (2026-11-16): GStreamer con gst-libav.** In Wine 11.22 `winedmo` fa solo il demuxing: tutti i decoder di Media Foundation (`msmpeg2vdec`, `msauddecmft`, WMA/WMV) e di DirectShow/quartz sono involucri di winegstreamer, e `mfsrcsnk` sceglie di default il demuxer di GStreamer. Scelta la strada di CrossOver: GStreamer compilato da noi, sopra un FFmpeg LGPL.

**Fatto (2026-11-16/17, in verifica).**
- `scripts/build-gst.sh <sorgenti> [prefisso] [fasi]`: FFmpeg 9.0.2 (LGPL, VideoToolbox e AudioToolbox, solo decoder/demuxer, niente librerie esterne rilevate da sole), pcre2 10.49 statico, glib 2.90.1 (libffi e zlib dell'SDK), GStreamer 1.28.9 senza parser di pipeline (`gst_parse`, chiedeva bison ≥ 2.4 e Wine non lo usa), plugin base (playback, typefind, app, convert, resample), good (isomp4, matroska, avi, flv, wavparse, id3, audioparsers, deinterlace, videofilter), ugly (asf, realmedia), bad (mpegdemux, videoparsers), libav. Target macOS 15. pkg-config vede solo il prefisso. La fase `names` riscrive i nomi di installazione in `@rpath/<id>` e toglie da `gstreamer-1.0.pc` l'rpath assoluto. Sorgenti scaricati con le somme SHA-256 verificate.
- `scripts/bundle-deps.sh`: copia anche lo stack media in `deps/` (librerie col nome del loro id, plugin in `deps/gstreamer-1.0`), firma ad hoc e controlla che non resti nessun percorso esterno. Oggi 73 librerie, circa 35 MB.
- Wine: `winegstreamer.so` e `winedmo.so` con rpath `@loader_path/../../deps`. `winegstreamer/unixlib.c` (macOS): plugin cercati in `<dir del .so>/../../deps/gstreamer-1.0`, `GST_REGISTRY_FORK=no` (niente `gst-plugin-scanner`, niente `fork()` da un thread Windows, che sotto HVF va in crash). Configurazione: GStreamer e FFmpeg da `deps-gst` (`PKG_CONFIG_PATH`), bison di Homebrew nel PATH come prima.
- Un prefisso creato senza GStreamer non ha `winegstreamer.dll` né le sue registrazioni (byte stream handler, MFT, filtri): serve `wineboot -u`. I prefissi della `.app` nascono già con GStreamer.
- Plugin mancanti scoperti dai test: `deinterlace` e `videoflip` (Wine li crea per nome; senza, mfplat e gli altri si bloccavano), `mpegpsdemux` per MPEG-1/2.
- Bug WoW64 sotto HVF: `wg_sample_data()` (winegstreamer) e la copia dei pacchetti in `winedmo` convertivano l'indirizzo Windows del buffer con un cast diretto; sotto i 4 GB quella memoria sta a `WINE_LOW_WINDOW_BASE` + indirizzo sull'host. Ora passano da `wine_low_window_ptr()`. Corretto, da verificare.
- Prova: `tests/av/mfplay.c` (IMFSourceReader, un reader per flusso; NV12 e PCM 16 bit), `mkref.py` (riferimento con l'ffmpeg dell'host: luminanza media per fotogramma, campioni e RMS dell'audio; le somme `framemd5` non servono perché la conversione di colore non è esatta), `cmpref.py` (fotogrammi ±1, luminanza ±2, campioni ±3%, RMS ±5%). File di prova generati in locale: H.264+AAC, HEVC, VP9, MP3, WMV2+WMA2, MPEG-2+MP2, PCM.

**Giro 10 (`tests/results/round10`).** Base: nessun file si apre (`MFCreateSourceReaderFromURL` 0xc00d36c4), mf e mfreadwrite in crash. Dopo (con deinterlace e videoflip, prima di MPEG e della correzione WoW64): `mfplay` ok su 6 file su 7 sulle tre architetture (manca MPEG-2); 23 unità migliorate, 487 controlli corretti: mfplat ok (5,7 milioni di controlli), mf `topology` ok, mfmediaengine ok, wmvcore ok, mf `mf` da crash a 1-2 fallimenti, mfreadwrite da crash a 14. Resta mf `transform` in crash all'avvio. I peggioramenti sono solo in quartz: `mpegsplit`, `mpegaudio`, `mpegvideo`, `mpeglayer3` erano saltati nella base (filtro assente) e ora girano: falliscono per il demuxer MPEG mancante e, su i386, per il bug WoW64 qui sopra (0xd0000005 e timeout). Il giro di verifica delle due correzioni è da fare.

Giro 10 rifatto (`round10/after2`, 2026-11-17): la correzione WoW64 funziona (su i386 spariscono 0xd0000005 e timeout, mf `transform` da crash a ok) e col demuxer MPEG quartz `mpegsplit` passa da crash a ok sulle tre architetture, `filtergraph` ok; 10 unità migliorate, 60 controlli corretti. MPEG-2 in Media Foundation resta non supportato: Wine upstream non registra un byte stream handler per `.mpg` (solo mp4/mov/3gp, asf/wmv/wma, avi, mp3, wav); in DirectShow funziona.

**MPEG in Media Foundation (giro 12, `tests/results/round12`).** Wine upstream non ha un byte stream handler per MPEG-1/2; Barrique lo aggiunge come su Windows:
- `mfsrcsnk`: `MPEG2ByteStreamPlugin` (CLSID di Windows `40871c59-…`) per `.mpg .mpeg .mpe .m1v .m2v .mpv2 .vob .ts .m2ts .mts .tts`, `video/mpeg`, `video/mp2t`; demuxer FFmpeg di winedmo (program e transport stream). Usa sempre winedmo e i decoder MF, non il gestore GStreamer di upstream: lì il decodebin sceglie `avdec_mp2` senza parser e l'audio MP2 fallisce ("Received decoded subframe, but no pending frame").
- `mfplat`: l'intestazione dei program stream (`00 00 01 BA`) fa riconoscere come `.mpg` anche i flussi senza URL.
- winedmo: il video MPEG-2 esce come `MFVideoFormat_MPEG2` (prima come FOURCC).
- winegstreamer: decoder MFT MPEG-1/2 (`wg_mpeg2_decoder`, gst-libav), esposto da `msmpeg2vdec` come `CLSID_MSMPEGDecoderMFT` ("Microsoft MPEG Video Decoder MFT"); caps `video/mpeg` mpegversion 2 nei due versi.
- `mp3dmod`: accetta anche MPEG-1 layer I/II (`MFAudioFormat_MPEG`), che il suo mpg123 decodifica già; prima nessun decoder MF per MP2.
- WoW64: i thunk di winedmo traducevano `url` e `context` con `UintToPtr` e il buffer del formato (`media_type.format`) non era tradotto: sotto HVF sono indirizzi della finestra bassa (`wine_low_window_ptr`). Su i686 nessun formato passava da winedmo prima di MPEG, per questo non si era visto.

Risultato: `mpeg2.mpg` (MPEG-2 + MP2) ok sulle tre architetture, 50/50 fotogrammi, audio identico al riferimento (`round12/after2`); gli altri 6 file invariati; conformità aarch64: mfsrcsnk, mfplat, mp3dmod 0 fallimenti, mfreadwrite 11 (erano 14). Non coperto: audio AC3 dei DVD (nessun decoder MF).

**mf `transform` (giro 13, `tests/results/round13`).** Il crash su aarch64 e x86_64 era il test `h264enc`: nessun encoder H.264 (FFmpeg senza encoder, x264 è GPL, gst-libav scarta gli encoder hardware come `h264_videotoolbox`), `CoCreateInstance` fallisce con 0xd0000001 e il test prosegue con un puntatore nullo; su i386 quella parte è saltata. Correzioni:
- Encoder H.264 di VideoToolbox: `applemedia` da gst-plugins-bad (`vtenc_h264`, LGPL), che richiede la libreria GL di gst-plugins-base (CGL/Cocoa; il plugin `opengl` non è incluso). `vtenc_h264` produce solo `stream-format=avc`: `wg_transform` lo fa seguire da `h264parse` (byte-stream per Media Foundation, SPS/PPS prima di ogni IDR). `vtdec`/`vtdec_hw` restano spenti (`GST_PLUGIN_FEATURE_RANK`): la decodifica hardware è un lavoro a parte, da misurare.
- `mp3dmod`: MPEG-1 layer I/II registrato come MFT a sé, "Microsoft MPEG Audio Decoder MFT" (`CLSID_MSMPEGAudDecMFT`, come Windows); il test `mp3dec` vuole l'MFT MP3 con il solo input MP3.
- I test `videoproc` e `colorconv` (NV12 → RGB32), visibili solo dopo il crash, davano 2% (aarch64) e 11–13% (x86_64) di differenza. Non è GStreamer: `msvproc` e `colorcnv` convertono con lo `swscale` di FFmpeg compilato come PE per ogni architettura, e su ARM64/ARM64EC, per larghezze multiple di 16, `swscale` sceglie i convertitori NEON "unscaled" YUV→RGB: approssimati su aarch64, sbagliati sotto ARM64EC (blocchi di colore). Su ARM64/ARM64EC ora `sws_flags=neighbor+accurate_rnd` li evita; x86 invariato.

Risultato (`round13/verify`): mf `transform` 0 fallimenti sulle tre architetture (prima crash), mfreadwrite 0 (erano 11), mfplat, mfmediaengine, mf `topology` ok; `mfplay` ok su 7 file × 3 architetture. Instabile, da indagare: su i386 un processo figlio del test mfplat ogni tanto si appende (timeout, 1 fallimento): `mfplat u1` in verify, `mfplat startup` in una delle due riprove (l'altra ok, `round13/mfplat-i386-*`); nel giro 10 passava. Sembra una questione di tempi nell'avvio/chiusura dei processi sotto i386/FEX, non legata alle modifiche del giro. Da tenere presente: gli altri percorsi NEON di `swscale` (scalatura) sotto ARM64EC non sono verificati.

**Decodifica con VideoToolbox (giro 14, `tests/results/round14`).** `vtdec_hw` di `applemedia` (rango primary + 1) prende H.264 e HEVC dopo `h264parse`/`h265parse`. File 1080p di 10 s a 30 fps (`samples/phase5/media-perf`, `vt.sh`): uscita identica a gst-libav (differenza 0,000 per fotogramma); CPU utente H.264 1,95 → 0,49 s (aarch64), 2,16 → 0,61 s (x86_64), HEVC simile; tempo reale uguale (domina l'avvio). Acceso di default però (`verify.sh`) rompe tre cose:
- MPEG-2: `vtdec_hw` accetta `video/mpeg` mpegversion 2 e non produce fotogrammi (0/50 sulle tre architetture);
- H.264 4:4:4: accettato anche quello, fotogrammi sbagliati invece di lasciare il posto a gst-libav;
- mf `transform` `h264dec`: 128–147 fallimenti (`ProcessOutput` 0xd0000023, campioni vuoti, cambio di risoluzione), l'uscita asincrona non va d'accordo con `wg_transform`.

Quindi `vtdec` e `vtdec_hw` restano spenti (`GST_PLUGIN_FEATURE_RANK=vtdec:NONE,vtdec_hw:NONE`, impostato solo se manca: `GST_PLUGIN_FEATURE_RANK=vtdec:NONE` lo riaccende per prova). Usarlo davvero vuol dire limitarlo a H.264/HEVC 4:2:0 e rendere sincrona l'uscita: lavoro futuro, vale ~4× di CPU in meno.

Prova più severa: `mkref.py` calcolava la luminanza dei riferimenti con `-pix_fmt gray` (gamma piena) mentre Wine dà NV12 a gamma limitata, da qui uno scarto fisso di ~2 che la tolleranza nascondeva. Ora usa il piano Y di `yuv420p`: tutti i file coincidono con dY 0,00 (WMV2 0,21, non bit-exact) e `cmpref.py` ha tolleranza 0,5. H.264 High 4:4:4 (`samples/phase5/media-unsupported`): il source reader lo rifiuta (0xc00d36bb) come Windows, il cui decoder H.264 prende solo 4:2:0.

Verifica finale (`round14/verify2`, vtdec spento): `mfplay` ok su 7 file × 3 architetture, MPEG-2 compreso; mf `transform` 0 fallimenti, mfreadwrite 0, mf `topology` ok; mf `mf` 1-2 fallimenti come nel giro 13 (`mf.c:8666`, 0x800c0008: URL di rete). Nel giro completo (`round14/verify`) quartz ha gli stessi fallimenti del giro 11 (`mpegaudio` 10, `mpegvideo` 7, i386 `vmr7` 4 e `vmr9` in crash).

**Chiuso (2026-11-17).** Cancello: mf, mfplat, mfreadwrite senza regressioni, quartz come prima, `mfplay` ok sulle tre architetture, librerie tutte in `deps/`. Aperti, fuori dal passo: decodifica VideoToolbox (sopra), audio AC3, avvisi di GStreamer (`gst_video_info_from_caps`, `gst_object_unref`) e `video_frame_wrap_buffer` di mfplat, figlio di mfplat i386 che ogni tanto si appende, percorsi NEON di `swscale` (scalatura) sotto ARM64EC, copia degli IOSurface (passo 4), fallimenti quartz `mpegaudio`/`mpegvideo`.

## Passo 4: memoria della GPU e la VM, esperimento ✅

**Questo rischio ce l'abbiamo solo noi.** CrossOver e il GPTK non usano una VM. La VM accetta solo memoria anonima e privata: gli altri tipi di pagina mandano il Mac in kernel panic (SPTM `VIOLATION_ILLEGAL_MAPPING_TYPE`). La memoria che la GPU condivide (MTLBuffer condivisi, IOSurface, memoria Vulkan mappata in modo persistente) è di un altro tipo. `host_page_is_safe` rifiuta oggi le pagine sospette, quindi il panic non dovrebbe arrivare, ma il codice Windows riceverebbe un errore.

Il passo decide **come il codice Windows vede la memoria della GPU**, prima di scrivere qualsiasi backend.

- **A, copia:** il lato Windows scrive in memoria propria, il lato host copia nel buffer Metal (stessa strategia di winevulkan per i processi a 32 bit). Sicura, costa banda.
- **B, importazione:** memoria anonima nostra, data a Metal con `newBufferWithBytesNoCopy` (o a MoltenVK con `VK_EXT_external_memory_host`). Niente copie, ma va verificato che Metal non cambi il tipo delle pagine.
- Esperimento in `hvproto`, mai dentro Wine: alloca, importa in Metal, interroga il tipo di pagina **senza** `hv_vm_map`. Solo se le pagine risultano identiche a prima si prova la mappatura. Ogni esecuzione con l'OK esplicito dell'utente e con il lavoro salvato: un errore qui fa ripartire il Mac.
- Cancello: strategia scelta e scritta qui, con le prestazioni misurate (MB/s copiati).

**Punto 1 (2026-11-17, `research/hvproto/gpumem.m`, `gpumem.log`, M3 Max).** Nessuna VM, nessun `hv_vm_map`: solo interrogazioni delle pagine con lo stesso controllo di `hvf.c`, più i campi che quel controllo non guarda.
- **Importazione** (memoria anonima come quella della VM, `newBufferWithBytesNoCopy`): appena creato il buffer la regione passa da `SM_PRIVATE` a `SM_TRUESHARED` con due riferimenti (Metal tiene l'oggetto di memoria); dopo il primo uso della GPU le pagine diventano **wired** (`VM_PAGE_QUERY_PAGE_WIRED`, presumibilmente mappate nella DART della GPU). Restano così anche dopo il rilascio del buffer. La GPU scrive e la CPU vede, senza copie.
- **Memoria di Metal** (`MTLBuffer` condiviso, tag `VM_MEMORY_IOACCELERATOR` 100) e **IOSurface** (tag `VM_MEMORY_IOSURFACE` 88): anonime, `SM_TRUESHARED`, senza pager.
- **`host_page_is_safe` accetta tutti questi casi.** L'ipotesi sopra (che rifiuti già le pagine della GPU) è sbagliata: il controllo non vede il tag né il wiring. Lo spazio utente non vede il tipo di pagina dello SPTM, quindi non sappiamo se queste pagine nella VM manderebbero in panic. Finché il punto 3 non lo prova, `hvf.c` deve rifiutare le pagine coi tag 88/100 e quelle wired dalla GPU.
- **Copia** (64 MB): `memcpy` da memoria anonima a un `MTLBuffer` condiviso 31,8 GB/s, blit della GPU dal buffer importato a uno privato 73,9 GB/s. 100 MB/s di dati dinamici (molto per un gioco) costano ~0,3% di un core con la copia.

**Controllo stretto in `hvf.c` (2026-11-17).** `page_type_ok` rifiuta ora i tag `VM_MEMORY_IOACCELERATOR`/`VM_MEMORY_IOSURFACE` e le pagine wired non private (le private wired restano: `VirtualLock` usa `mlock`); il messaggio di rifiuto stampa anche il tag. `gpumem` con lo stesso controllo: rifiutati A2–A4, B, C, D3; accettati A0, A1 e `mlock` (E1). Resta scoperto il caso A1 (memoria della VM importata in Metal ma non ancora usata dalla GPU): con la strategia A Wine non importa mai memoria della VM. Copia ripetuta: `memcpy` 31,8–54,7 GB/s a seconda del giro. Verifica (`tests/results/round15`, moduli grafici del giro 6 e `virtual` di kernel32/ntdll sulle tre architetture, `WINEDEBUG=-all,err+hvf`): **nessun rifiuto** in nessun log, quindi nessuna pagina legittima cambia strada. `virtual` identico ai giri 7–8 (kernel32 aarch64 167 fallimenti, ntdll aarch64 86, i386 31, x86_64 in timeout come prima). Contro il giro 6, `compare.py` segna d3d9ex e ddraw peggiorati: i test chiedono di cambiare risoluzione e trovano lo schermo a 1512×982 (lo schermo del portatile, il giro 6 girava con un'altra configurazione di schermi), da cui i nuovi fallimenti di d3d9ex x86_64 e i timeout/crash di ddraw aarch64; `dsurface` 1 fallimento (`dsurface.c:2112`, 0x80004005). Senza rifiuti il codice nuovo non cambia nulla, quindi non dipendono da questa modifica; da rifare con la stessa configurazione di schermi del giro 6 se servono.

**Punto 3 (2026-11-17, `research/hvproto/gpumap.m`, `gpumap.log`, M3 Max, macOS 27.3).** Una VM, un vCPU a EL1 con MMU spenta che legge la pagina, scrive un marcatore ed esce con HVC; poi il marcatore si controlla dalla CPU e dalla GPU (blit verso un buffer privato e ritorno). Un caso per processo, ognuno con l'OK dell'utente.
- Caso 1, memoria anonima (controllo): ok.
- Caso 2, memoria anonima importata con `newBufferWithBytesNoCopy`, GPU non ancora usata: **ok, nessun panic**; la GPU legge la pagina mentre è mappata nella VM e vede il marcatore del guest.
- Caso 3, la stessa dopo che la GPU l'ha riempita (`SM_TRUESHARED` + wired): **ok, nessun panic**; il guest legge il riempimento della GPU (0xa5…), CPU e GPU vedono il marcatore del guest.
- Caso 4, `MTLBuffer` condiviso allocato da Metal (tag 100): **ok, nessun panic**, CPU e GPU vedono il marcatore del guest.
- Caso 5, IOSurface bloccato per la CPU (tag 88): **ok, nessun panic**, la CPU vede il marcatore (GPU non controllata: nessun `MTLBuffer` sopra).

Nessuno dei tipi di memoria della GPU che abbiamo trovato manda in panic. Quindi B (importazione) funziona sul nostro hardware: memoria anonima nostra, mappata nella VM e data alla GPU, coerente nei due versi, senza copie. Una prova non copre tutto (pagine compresse o purgeable, versioni future di macOS); `hvf.c` continua a rifiutare tag 88/100 e pagine condivise wired finché un backend non ne ha davvero bisogno.

**Chiuso (2026-11-17). Strategia scelta:** **A (copia) come strategia di base**, costo trascurabile (copia 31,8–54,7 GB/s, blit GPU 64–74 GB/s) e nessun rischio; B (importazione) provato possibile nel punto 3, da usare dove la copia pesa (buffer grandi mappati in modo persistente, Vulkan `HOST_COHERENT`), allentando il controllo di `hvf.c` solo per la memoria che Barrique stessa importa.

## Passo 5: Vulkan con MoltenVK 🔧

- Loader Vulkan e MoltenVK (Apache-2.0) compilati da noi, dentro la `.app`, versione fissata. CrossOver fa lo stesso.
- winevulkan con la strategia del passo 4; cache delle pipeline di MoltenVK.
- Test: vulkan-1 e i nostri campioni del passo 6.
- Cancello: test senza regressioni sulle tre architetture, base di fase 4 verde.

**In corso (2026-11-17, giro 16, `tests/results/round16`).**
- Solo MoltenVK, niente loader: il `configure` di Wine accetta `libMoltenVK.dylib` come `SONAME_LIBVULKAN` e win32u usa direttamente il suo `vkGetInstanceProcAddr`. Il loader servirebbe solo per i layer (validazione): si aggiunge se serve.
- `scripts/build-moltenvk.sh`: MoltenVK 1.4.2 (24 luglio 2026) dagli archivi delle revisioni fissate in `ExternalRevisions` (MoltenVK, SPIRV-Cross, SPIRV-Tools, SPIRV-Headers, Vulkan-Headers, cereal, Volk: 12 MB, SHA-256 controllate), senza clonare e senza la build CMake (che scarica CPM). Solo arm64, macOS 15. `libMoltenVK.dylib` 5,3 MB, `@rpath`; `bundle-deps.sh` lo copia in `deps/`, win32u lo apre con `dlopen("@rpath/...")` come FreeType. Wine riconfigurato con `LDFLAGS=-L.../deps-vk/lib`: unica differenza i due define Vulkan. Dopo il rilink il loader va rifirmato con `entitlements.plist` (il primo giro è fallito per questo).
- **Memoria (passo 4, strategia B):** sotto HVF anche i processi a 64 bit prendono la strada WoW64 di win32u: la memoria Vulkan visibile dalla CPU è allocata con `NtAllocateVirtualMemory` e importata con `VK_EXT_external_memory_host`. Prima a MoltenVK arrivava l'indirizzo Windows: ora `wine_low_window_ptr` (sotto i 4 GB l'host la vede a `WINE_LOW_WINDOW_BASE`). `hvf.c` accetta di nuovo le pagine condivise e wired (sono quelle importate, provate nel caso 3) e rifiuta solo i tag 88/100. Nei test d3d12 la memoria importata è usata e nessuna pagina è rifiutata.
- **WoW64 sotto HVF:** `command_pool_from_handle` e `wine_vk_unwrap_handle` usavano senza traduzione gli indirizzi degli oggetti del client (command pool, device, queue, command buffer, instance) passati come handle a 64 bit; gli handle (HWND, HANDLE, HMONITOR, HINSTANCE) passavano invece da `UlongToPtr`, che sotto HVF li trattava come indirizzi. Corretti in `vulkan_thunks.c` e nel generatore `make_vulkan` (non rigenerato).
- **MoltenVK senza `vkGetPhysicalDevicePresentRectanglesKHR`:** il test chiamava un puntatore nullo. win32u ora la emula (un rettangolo grande quanto `currentExtent`) e winevulkan la dichiara disponibile con `VK_KHR_surface`.

Risultati (`round16/vk2`): vulkan-1 1173 controlli con 19 fallimenti su tutte e tre le architetture quando arriva in fondo (prima: istanza non creata, `vr -3`); d3d12 su vkd3d e MoltenVK 671 controlli, 0 fallimenti su aarch64 e x86_64 (prima saltati); d3d11 invariato (15 fallimenti, wined3d GL). Aperti:
- I fallimenti di vulkan-1 sono finestre ridimensionate o minimizzate: MoltenVK risponde `VK_ERROR_OUT_OF_DATE_KHR` all'acquisizione dove Windows non lo fa, e win32u non può nasconderlo (senza immagine valida). Dopo uno di questi il test ogni tanto si blocca (attesa infinita su una fence: una volta x86_64, una volta aarch64).
- i386: d3d12 non crea il device (vkd3d a 32 bit), d3d11 0 controlli.
- dxgi in crash sulle tre architetture: ora che d3d12 funziona, `test_create_swapchain` arriva a una parte che crea un device D3D10 con BGRA a livello 10_0 e non controlla il risultato; wined3d sul GL 2.1 di Apple arriva solo a 9.x. Su Windows i due vanno insieme: si risolve con un D3D10/11 a livello ≥ 10 (renderer Vulkan di wined3d o DXMT, passo 7).

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

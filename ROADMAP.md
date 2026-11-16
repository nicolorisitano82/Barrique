# Roadmap: Wine nativo per Apple Silicon con VM Hypervisor

Obiettivo: Wine nativo arm64 su macOS, che esegue programmi Windows ARM64, x86-64 e x86 a 32 bit senza Rosetta, sfruttando Apple Silicon. Il codice Windows gira in una VM Hypervisor.framework (pagine 4K, memoria sotto 4GB, x18 libero, TSO hardware); il codice x86 è tradotto da FEX.

Legenda: **[MS]** = idea presa da [MetalSharp-Wine](https://github.com/metalsharp/MetalSharp-Wine), da reimplementare (il loro codice è AGPL-3.0, Wine è LGPL: si prendono le idee, non il codice). Stato: ✅ fatto · 🔧 scritto, da provare · ⬜ da fare.

## Stato attuale (2026-11-15)

| | Stato |
|---|---|
| Wine arm64 nativo, codice Windows ARM64 nella VM | ✅ `cmd /c echo` funziona, prefix creato da zero |
| Syscall, unix call, callback, eccezioni nella VM | ✅ |
| Sospensione e contesto dei thread (SIGUSR1/SIGQUIT) | ✅ test kernel32 `thread` 962/962, test mirato 2000/2000 |
| FEX compilato (`libwow64fex.dll`, `libarm64ecfex.dll`) | ✅ |
| Build Wine ARM64EC + i386 | ✅ compila |
| x86-64 via ARM64EC nella VM (FEX) | ✅ `hello64.exe` RESULT ok |
| x86 a 32 bit via WoW64 (FEX) nella VM, finestra di memoria a `0xE000000000` | ✅ `hello32.exe` RESULT ok, `syswow64` installata da wineboot |

## Fase 1: x86 funzionante ✅ (chiusa il 2026-11-14)

- ✅ Test `hello64.exe` (x86-64): FEX ARM64EC nella VM, risultato corretto.
- ✅ Test `hello32.exe` (x86 a 32 bit): FEX WoW64 nella VM, risultato corretto, senza entitlement a pagamento.
- ✅ Nessun fault lato host durante wineboot e i test: conversione dei puntatori a 32 bit comune a tutte le librerie host (`wine/unixlib.h`), messaggi di finestra con puntatore (`is_pointer_message`), tabelle delle code page, trap dell'host gestite come errore della syscall.
- ✅ **[MS]** Chiavi `Wow64\amd64` e `Wow64\x86` in `wine.inf` (`PreInstall.ntarm64`, senza sovrascrivere) e DLL di FEX trovate da Wine come DLL interne (sviluppo: `WINEDLLPATH=~/Work/wine/fex-dlls`; nella `.app` accanto alle DLL di Wine).
- ✅ Revisione di `ULongToPtr` in `wow64win` fatta: conversione attiva, handle (`himc`, `hwndTarget`) esclusi, valori sotto 64K e da `0xfffe0000` in su lasciati intatti.
- ✅ Test di `CreateRemoteThread`: wineserver sceglieva le basi delle immagini a 64 bit oltre 1TB (exe `0x7ff6…`, DLL `0x6000…`). Ora su macOS arm64: DLL a 64 bit tra 512 e 768GB, exe tra 832 e 864GB (`server/mapping.c`). Verificato su aarch64: `kernel32` `thread` 979 controlli, 0 fallimenti, 0 saltati. Su i386 il test è ancora saltato: passa alla fase 2.
- ✅ Test di conformità, primo giro (`tests/run-conformance.sh`, `ntdll` + `kernel32`, aarch64 e i386): circa 17,7 milioni di controlli, 99,994% superati. Bug trovato e corretto: il percorso HVF dei fault dell'host scavalcava il "try" del codice host di Wine (`ntdll_set_exception_jmp_buf`), con crash in `console` e `file`.
- ✅ **[MS]** FEX compilato senza LTO e con CPU generica (`-mcpu=apple-m1` provoca un bug di unwind in ARM64EC).
- ✅ **[MS]** Verificato che le nostre DLL i386 mantengono i caricamenti ritardati: da loro il `dlltool` GNU di Homebrew li eliminava e ogni processo a 32 bit andava in crash.
- ✅ **[MS]** Verificato l'allineamento delle sezioni a 64K in ARM64X: da loro, quando si perdeva, wineboot passava da 29 a 305 secondi.

## Fase 2: correttezza della traduzione x86 ✅ (chiusa il 2026-11-14)

**Risultato** (secondo giro di conformità, `tests/results/round2/`): su i386 i timeout passano da 16 a 0 (ntdll 5→0, kernel32 11→0); kernel32 i386 da 4,0 a 5,3 milioni di controlli e da 275 a 174 fallimenti; 19,6 milioni di controlli in tutto sulle due architetture. Restano su i386 3 crash (`ntdll` `exception`, `kernel32` `debugger` e `loader`, più `virtual` che muore in `test_stack_commit`), tutti spiegati sotto e passati ad altre fasi.

Lavoro fatto (2026-11-14):
- 🔧 **Timeout su i386 sotto FEX** (16 unità contro 3 su aarch64). Causa principale trovata e corretta: la ntdll a 64 bit riconosceva i thread a 32 bit dall'indirizzo di partenza sotto i 4GB, e con la finestra non lo erano più, quindi FEX non veniva inizializzato nei thread secondari (`loader.c`, `loader_init`). `thread32.exe` ora `RESULT ok`; 6 unità su 16 ora completano (`ntdll` `pipe`, `sync`; `kernel32` `fiber`, `heap`, `module`, `toolhelp`).
- ✅ **`CreateProcess` da codice a 32 bit**: `wow64` passava all'host il percorso dell'exe (`PS_ATTRIBUTE_IMAGE_NAME`) senza convertirlo; funzionava solo per i programmi di `system32`, reindirizzati. `proc32.exe` ora `RESULT ok`; niente più errori 998 né `CreateRemoteThread` saltato.
- ✅ **Sospensione dentro FEX**: i `brk` sconosciuti diventano `EXCEPTION_ILLEGAL_INSTRUCTION` come su Linux, così FEX riconosce la sua trappola `brk #0xCAFE`. `kernel32` `thread` i386: 1034 test, 4 fallimenti (prima crash senza output).
- ✅ **Memoria nella finestra bassa**: `zero_bits` (ricerca top-down sotto il limite, `NO_MEMORY` invece di `INVALID_PARAMETER`), mappature tra processi (controllo `zero_bits` sull'indirizzo a 32 bit, limite 4GB per figli wow64), limiti di `MEM_ADDRESS_REQUIREMENTS` passati come numeri. `ntdll` `virtual` i386: da timeout a 2169 test, 31 fallimenti, tutti dovuti alle pagine host da 16K.
- ✅ **Mappature di file scrivibili e condivise** (prima rifiutate per non dare pagine di file alla VM): vista anonima riempita dal file e pagine modificate riscritte su flush, unmap e uscita del processo (`hvf_shadow`). Test `mapfile` ok su i386, x86-64, arm64. Limite: due viste dello stesso file non si vedono finché non si riscrive.
- ✅ **vCPU**: venivano persi alla fine di ogni thread (limite 64 per processo); ora distrutti, e un thread che si blocca in attesa rilascia il suo quando ne sono in uso più di metà. Test `threads100`: 100 thread vivi insieme ok in 81–154 ms su tutte e tre le architetture.
- ✅ Scatter/gather (`NtReadFileScatter`/`NtWriteFileGather`) da codice a 32 bit. `kernel32` `file` i386: 571.424 test, 8 fallimenti (16K).
- ✅ **FEX**: un unmap su un indirizzo non mappato toglieva i permessi di esecuzione a tutta la memoria da 0 in su (anche in FEX upstream); scansione DEP e ricerca della sezione ora convertono gli indirizzi della finestra bassa.
- ✅ **Notifiche tra processi** (`VirtualAllocEx`/`VirtualProtectEx` verso un figlio a 32 bit): wow64 passava al FEX del figlio l'indirizzo della finestra bassa, e il codice scritto dal padre risultava non eseguibile. `kernel32` `debugger` i386: da timeout a 1669 test, 9 fallimenti, 9 s.
- 🔧 i386 ancora aperti: `kernel32` `virtual` arriva alla riga 4650 e muore in `test_stack_commit` (lo stack cresce a passi di 16K e il test tocca la riserva in fondo: altro effetto delle pagine da 16K); `debugger` 5 fallimenti (3966 test; wow64 ora svuota la coda di FEX anche all'uscita dalla syscall, così un breakpoint scritto da un debugger durante un'attesa viene visto; restano `CreateProcess` con `DEBUG_PROCESS` 0x2db, anche su aarch64, e l'ordine degli eventi); `loader` (test di `Wow64Transition`, legato a FEX); `process` 21 fallimenti (prima 40: `VirtualQuery` da 0 ora parte dalla finestra bassa; restano livello CPU di FEX, handle standard come su aarch64, NUMA/cpuset).
- ⚠️ **FEX vieta il codice generato da AI** (`CLAUDE.md` nel loro repo): FEX è ora in `fex/` dentro Barrique con le nostre modifiche (anche in `patches/fex/`), scritte con Claude e da non proporre mai a FEX; i problemi lato FEX si segnalano a parole o si aggirano lato Wine.
- ➡️ Fase 7 — Limiti delle pagine host da 16K (anche su aarch64): liberare o proteggere 4K dentro una pagina da 16K, guard page e crescita dello stack (`test_stack_commit`), write-watch per pagina host. Le tabelle delle pagine della VM sono a 4K: si potrebbero usare per gestire questi casi a 4K.
- ➡️ Fase 7 — `ntdll` `exception`: aarch64 fallisce le eccezioni "collise" (unwind annidato, `c0000027`); i386, dopo `RtlUnwind`, FEX va in fault sulla pagina di codice protetta per il codice auto-modificante e ogni gestione ne genera un'altra fino allo stack overflow.
- ✅ i386 `kernel32` `virtual`: `CreateProcess` con errore 998 (corretto con `PS_ATTRIBUTE_IMAGE_NAME`).
- ➖ Livello e revisione della CPU x86 (`ProcessorLevel`, `process` 2418/2419): FEX li ricava dalla sua CPUID emulata (Intel famiglia 6); i test si aspettano i valori dell'emulatore Microsoft (15, 0x40a). Differenza voluta, non corretta per non rendere incoerenti CPUID e informazioni di sistema.
- ➡️ Fase 7 — aarch64 nativo: `kernel32` `debugger` (crash), `loader` (timeout), `ntdll` `rtl` (crash): non riguardano la traduzione x86. `CreateProcess` con `DEBUG_PROCESS` da un thread secondario (errore 0x2db) su entrambe.
- ➡️ Fase 4 — Fallimenti uguali sulle due architetture, quindi non dovuti a FEX: `ntdll` `wow64` (~140), `kernel32` `resource` (95), `version`, `actctx`, `console` (45 su aarch64), `ntdll` `virtual` (115 su aarch64).
- ➖ `change` (notifiche di modifica delle cartelle): limite noto di Wine su macOS, uguale in MetalSharp.
- ➡️ Test di conformità x86-64 veri (serve una build dei test come codice x64): spostato alla fase 4.


- ✅ **[MS]** Registri che descrivono la CPU (ID registers): letti dal vCPU con `hv_vcpu_config_get_feature_reg` (`get_core_id_regs_arm64`).
- ➡️ Fase 4 — **[MS]** Correzioni a FEX da rifare se incontriamo lo stesso errore (i flag di eccezione SSE/x87 mancano: `kernel32` `thread` 2084/2204/2209):
  - flag di eccezione x87 persi in FLDENV, FRSTOR, FXRSTOR e nella conversione di contesto WoW64 (`fetestexcept` a 32 bit leggeva 0);
  - `POP` verso la memoria: RSP aggiornato prima di una scrittura che va in fault, quindi l'istruzione non riparte pulita;
  - flag DF/AF persi nei contesti di eccezione ARM64EC;
  - terminazione di thread di altri processi.
- ✅ **TSO hardware**: FEX chiede a Wine `ProcessFexHardwareTso` (`NtSetInformationProcess`, classe 2000) quando manca la sua libreria Linux; Wine accende il bit TSO di Apple in `ACTLR_EL1` su ogni vCPU (ognuno si aggiorna dal suo thread prima di rientrare nella VM) e FEX smette di emulare l'ordine della memoria con accessi acquire/release. Test `tso.c`: con il nuovo FEX Wine attiva il TSO ("hardware TSO on"), 0 violazioni su 2 milioni di scambi di messaggi a 32 e 64 bit; il ciclo di prova (20 ms) è troppo breve per vedere il guadagno, da misurare in fase 3. È il nostro vantaggio su MetalSharp, che emula l'ordine della memoria in software.

## Fase 3: prestazioni e avvio ✅ (chiusa il 2026-11-14)

Misure con `tests/pe/bench.c` (stesso sorgente nativo macOS / ARM64 / x86-64 / i386, checksum confrontati col nativo; risultati in `tests/results/bench-*.txt`), in ms:

| Prova | Nativo | ARM64 (VM) | x86-64 (FEX) | i386 (FEX) |
|---|---|---|---|---|
| int | 173–191 | 178 | 274 | 218–223 |
| fp | 322–334 | 327 | 355–360 | 348–365 |
| mem | 26–28 | 116 → **71** | 113 → **73** | 115 → **75** |
| atomic | 89–95 | 170 (llvm-mingw senza LSE) | 91–96 | 92 |
| order | 155–233 | (test non valido su ARM) | 270–307 | 181–221 |
| syscall (1M) | 104–114 | **1917–2050** | 2016–2083 | 2148–2208 |

- ✅ **[MS]** `dnsapi`: su macOS un nome `.local` non passa più dal DNS multicast (`getaddrinfo`); gli indirizzi arrivano da `getifaddrs`. Su questo Mac la ricerca costava solo 20 ms, ma senza rete o con un nome cambiato può attendere 5 secondi a ogni avvio.
- ✅ **[MS]** Suite di benchmark (`bench.c`: int, fp, mem, atomic, order, syscall). Codice tradotto da FEX: interi 1,1–1,4×, virgola mobile 1,05–1,1×, atomici come il nativo.
- ✅ TSO hardware misurato: sul test `order` il risultato oscilla da un lancio all'altro (x86-64: −12% in un giro, +25% nell'altro), quindi il guadagno è sotto il rumore di questo test; gli altri test non cambiano. Come previsto da MetalSharp, su M-series l'emulazione software costa poco.
- ✅ **Fault-around**: al primo accesso la VM mappa l'intero blocco allineato da 64K (16 pagine) leggendo le protezioni con un solo lock; memoria nuova da 4,4× a 2,8× il nativo. Il resto è il controllo di sicurezza delle pagine host prima di darle alla VM (protezione dal kernel panic), che resta per pagina.
- ✅ Avvio: `hello32` 0,58 s a freddo / 0,08 s a caldo, `hello64` 0,51–0,56 / 0,04 s.
- ✅ **Registri SIMD pigri sulle syscall**: all'uscita per syscall o unix call si leggono solo i registri interi; i 32 SIMD restano nel vCPU e si leggono solo per un contesto del thread, una callback nel codice Windows o il rilascio del vCPU. Syscall da 2,05 a 1,76 µs (ARM64), da 2,08 a 1,85 (x86-64), da 2,20 a 1,99 (i386); test `fpsys.c` (valori in virgola mobile vivi attraverso syscall e callback) identico al bit sulle tre architetture.
- ✅ **Quanto costano le uscite dalla VM nei programmi reali** (`WINE_HVF_STATS=1` stampa uscite e syscall al secondo a fine processo; `tests/results/syscall-stats.txt`): `reg query /s` 97.000 syscall/s (~16% del tempo nelle uscite), `cmd dir /s` i386 68.000/s (~11%), ARM64 37.000/s (~6%), test kernel32 `file` 25–30.000/s (~4–5%).
- ➡️ Fase 7 — Uscita dalla VM (~1,7 µs per syscall contro 0,1 µs nativi): gestire alcune syscall frequenti senza uscire (per esempio quelle che leggono solo dati del processo); conta per i programmi con molte syscall (fino al 16% nelle misure).
- ✅ **LSE**: le parti ARM64 e ARM64EC di Wine si compilano con `-march=armv8.1-a` (`aarch64_CFLAGS`/`arm64ec_CFLAGS`): atomici con istruzioni LSE invece dei cicli LL/SC. Nel benchmark gli incrementi atomici passano da 175 a 92 ms (nativo 89–95). Tutti gli Apple Silicon hanno LSE, e Windows 11 su ARM lo richiede.

## Fase 4: validazione ✅ (chiusa il 2026-11-15)

- ✅ **[MS]** Test di conformità per tutte e tre le architetture (giro 3, `tests/results/round3`). I test x86-64 sono compilati come codice x64 in una build a parte (`build-x64tests`, `--enable-archs=aarch64,x86_64`) e girano sulla build principale attraverso ARM64EC; `run-conformance.sh` accetta `x86_64`. `tests/classify.py` divide i fallimenti per architettura: comuni a tutte (Wine/macOS/VM), solo x86 (FEX), solo i386 (WoW64 e finestra a 32 bit), solo x86-64 (lato ARM64EC), solo aarch64.
  - 16 moduli oltre a ntdll e kernel32 (kernelbase, advapi32, msvcrt, ucrtbase, ole32, oleaut32, rpcrt4, shlwapi, version, crypt32, bcrypt, combase, dbghelp, user32, gdi32, ws2_32), circa 10 milioni di controlli per architettura: 966 fallimenti su ARM64, 1017 su x86-64, 1103 su i386 (`round3/summary.txt`, `classify-modules.txt`).
  - 600 controlli falliscono uguali sulle tre architetture, quindi non dipendono da FEX. Solo 14 falliscono su entrambe le x86 e non su ARM64 (user32 `edit`/`listbox`, dopo le correzioni sotto). Il traduttore è quasi trasparente.
  - msvcrt, ucrtbase (a 64 bit), shlwapi, bcrypt, combase e version: 0 fallimenti ovunque.
- ✅ **Programmi reali** (`tests/apps/run-apps.sh`, prefix separati `prefix-apps` e `prefix-apps32`), tutti RESULT ok:
  - CPython 3.13 embeddable x64 e x86: interprete, hashlib, zlib, json, sqlite3, ssl, ctypes, decimal, thread, subprocess, file (`tests/apps/probe.py`);
  - Java Temurin 21 x64 e 8 x86: JIT, GC, pool di thread, gzip, SHA-256, file (`tests/apps/Probe.java`). Il JIT di Java tradotto da FEX: ciclo da 172 ms nativi a 616 ms (x64) e 206 ms (x86);
  - 7-Zip 23.01 MSI x64 e x86: installazione silenziosa, compressione e decompressione confrontate col sorgente;
  - VC++ 2015-2022 redistributable x64 e x86: installazione silenziosa ok. Su ARM64 il pacchetto x64 installa il runtime Arm64, come su Windows 11 ARM.
- ✅ Correzioni trovate dai programmi e dai test:
  - **Exe x64 con base sotto i 4GB** (7-Zip, ImageBase 0x400000): il server sceglieva un indirizzo dinamico basso, l'immagine finiva nella finestra (0xE0…) ma veniva rilocata ed eseguita all'indirizzo basso. Le immagini a 64 bit rilocabili ora stanno sopra i 4GB, come fa l'ASLR di Windows.
  - **Eccezioni x64 rilanciate da FEX** (Java, 7-Zip, test `exception`): `hvf_restore_frame` si fidava del flag `RESTORE_FLAGS_EMULATION` lasciato da `NtSetContextThread`, e rientrava nell'emulatore invece di chiamare `KiUserExceptionDispatcher`. Ora decide in base a dove torna il frame, come `usr2_handler`.
  - **FEX**: un processore in più riportato al codice x86 (15 su 14 core).
  - **wow64win**: i thunk generici `NtUserCallHwndParam`/`OneParam`/`TwoParam` passavano i puntatori a 32 bit senza convertirli nella finestra (`GetWindowInfo`, `GetMonitorInfo`, `ClientToScreen`, `GetPrivateData`… ). user32 i386 da 18.893 fallimenti e 5 crash a 826 fallimenti senza crash: ora monitor, sysparams, win e clipboard danno gli stessi numeri di ARM64.
- ✅ Fase 5 passo 1 — **FreeType non caricato** (risolto il 2026-11-16, FreeType incluso con `@rpath`): `dlopen("libfreetype.6.dylib")` non trova `/opt/homebrew/lib`, quindi niente font TrueType e fallimenti nei test di font (gdi32 `font`, user32 `text`). Si risolve con le librerie dentro la `.app` (fase 6).
- ➡️ Fase 7 — Resti dei test, per architettura:
  - solo i386: ws2_32 `afd` (108, strutture AFD a 32 bit con puntatori), user32 `class` (classi cercate per `hInstance` a 32 bit), ole32 `dragdrop`, ucrtbase `misc` e kernel32 `thread` (flag di eccezione x87/SSE persi da FEX a 32 bit: la correzione MetalSharp della fase 2);
  - solo x86-64: rpcrt4 `cstub` (vtable dei delegati), dbghelp (stack walk), crypt32 `sip`, ntdll `info` (affinità, informazioni CPU emulate: atteso);
  - ntdll `exception` x86-64: dopo la correzione le eccezioni arrivano, ma lo srotolamento da pc 0 finisce in ricorsione (`RtlIsEcCode` con un pc non valido); va in crash anche su ARM64 e i386;
  - exe x64 senza rilocazioni con base sotto i 4GB: non caricabili (rari).
- ✅ ntdll e kernel32 rilanciati sulle tre architetture con le correzioni (giro 4, `tests/results/round4`, `classify-ntdll-kernel32.txt`):
  - x86-64: ntdll da 239 a 206 fallimenti (`info` da 68 a 35); kernel32 `loader` non va più in crash (completa con 78 fallimenti, prima zero controlli contati), `debugger` da 61 a 27 fallimenti nei processi figli. Il runner segnava `debugger` come crash perché i figli vanno in crash apposta; nel 2026-11-16 si è visto che il processo principale moriva davvero (limite dei breakpoint x64 sotto FEX, vedi FASE5-AV.md) e il runner ora lo distingue dal codice di uscita. kernel32 sale da 143 a 223 solo perché `loader` ora conta i suoi controlli.
  - ARM64 e i386: invariati entro 2-6 controlli (i386 kernel32 da 174 a 168, `debugger` da 5 a 0).
  - Fallimenti solo x86 (FEX) da 17 a 6, solo x86-64 da 45 a 36, solo i386 da 37 a 32; comuni alle tre: 199, invariati.
  - kernel32 `pipe` x86-64: i 2 fallimenti del giro 4 su `FlushFileBuffers` (pipe.c:2878) non si ripetono, 0 fallimenti in 5 esecuzioni di fila (`round4/pipe-recheck`): è una gara di tempi del test, non una regressione.

## Fase 5: grafica, audio e input 🔧 (in corso)

Piano dettagliato di grafica, audio e video, con le regole contro le regressioni e lo studio di CrossOver e del Game Porting Toolkit: [FASE5-AV.md](FASE5-AV.md). D3DMetal escluso (solo x86_64 con Rosetta, licenza non commerciale).

Stato dei passi di [FASE5-AV.md](FASE5-AV.md):
- ✅ Passo 0, base di partenza (giro 5, 2026-11-15).
- ✅ Grafica a 64 bit sotto HVF: stringhe e buffer GL, blocco e use-after-free in winemac (giro 6, 2026-11-16).
- ✅ Passo 1, FreeType e font (giro 7, 2026-11-16): FreeType incluso con `@rpath`, 35 unità migliorate. Dal test di fumo: `fork()` da un thread Windows sotto HVF (installer VC++, crash all'uscita dei processi), winedbg e wineserver col debugger.
- 🔧 Passo 2, audio (giro 8, in corso): timer di coreaudio, salvataggio del registro in un processo figlio (via il buco audio di ~60 ms ogni 20 s). Da provare 96 kHz e Bluetooth.
- ⬜ Passi 3-7: video, memoria GPU, Vulkan/MoltenVK, banco di prova grafico, Direct3D.

- ⬜ **[MS]** Direct3D su Metal in versione ARM64X, così un'unica build serve sia ai programmi ARM64 sia a quelli x86-64 (ARM64EC). Componenti da valutare:
  - D3D10/11/12 con un fork di DXMT (Mullion);
  - D3D9/10/11 con DXVK su MoltenVK;
  - D3D12 con VKD3D-Proton.
- ⬜ **[MS]** OpenGL 4.6 su Metal (WineMetalGL) al posto del GL 4.1 di Apple.
- ⬜ **[MS]** MoltenVK con cache delle pipeline.
- ⬜ **[MS]** FFmpeg (VideoToolbox/AudioToolbox) per `winedmo` / Media Foundation.
- ⬜ **[MS]** Controller: winebus con SDL2 + IOHID (XInput e DirectInput).
- ⬜ **[MS]** Mouse raw con GCMouse e schermo intero nativo di macOS.
- 🔧 **[MS]** Font:
  - ✅ Arial, Times New Roman e Courier New: macOS li ha già in `/System/Library/Fonts/Supplemental` e Wine li carica tramite CoreText, Liberation non serve;
  - ⬜ famiglie CJK di Windows associate ai font di sistema di macOS.
- ⬜ **[MS]** Mono (arm64, x86, x86-64) e Gecko inclusi, invece della finestra che chiede di scaricarli.
- ⬜ **[MS]** CEF/Chromium: `--in-process-gpu` automatico (senza, la pagina resta bianca).
- ⬜ **[MS]** msync, sincronizzazione su memoria condivisa. Da verificare con la VM: la memoria condivisa `shm` nella VM è già provata.

Per ognuno dei componenti grafici e multimediali va controllata la licenza.

## Fase 6: pacchetto e distribuzione

**Requisito: il prodotto finale è un'unica `.app` autosufficiente, senza file sparsi nel sistema.** Dentro il bundle: loader, wineserver, librerie (`@rpath`/`@loader_path`, nessun percorso di Homebrew), DLL Wine per tutte le architetture, FEX, Mono, Gecko, font e strumenti. Niente installazioni in `/usr/local`, nessun `~/.wine` di default, nessun LaunchAgent. I dati che cambiano (prefix Windows, registro, programmi installati) stanno in **un'unica cartella dell'app**, `~/Library/Application Support/<NomeApp>/` (scelta dell'utente, 2026-11-14): scrivere dentro la `.app` ne romperebbe la firma. Cancellando la `.app` e quella cartella non resta nulla nel sistema.

- ⬜ App `.app` con loader firmato e permesso `com.apple.security.hypervisor`. Basta la firma ad-hoc: **non serve un account sviluppatore Apple a pagamento**, a differenza dei 32 bit di MetalSharp.
- ⬜ **[MS]** Pacchetto `dist/` rilocabile, con controllo dei Mach-O: solo arm64, `minos` ≤ 15.0, nessun percorso di libreria esterno.
- ⬜ **[MS]** Strumenti inclusi: `innoextract` (installer Inno/GOG) e `cabextract`.
- ⬜ **[MS]** VC++ 2015-2022 redistributable ufficiali, offerti all'installazione senza modifiche.

## Fase 6b: wizard "da installer a .app"

Un piccolo assistente, sul modello di winetricks, Whisky, CrossOver e Porting Kit, che parte dall'installer di un programma Windows e produce una **`.app` a sé stante per quel programma**: si apre dal Finder e dal Dock come un'app macOS qualsiasi.

**Flusso**
1. Scegli l'installer: `.exe`, `.msi`, oppure installer Inno/GOG da estrarre con `innoextract`. Nome dell'app proposto dal file.
2. Si crea un prefix dedicato al programma (wineboot, FEX già configurato).
3. Opzionale: le dipendenze più comuni, con ricette in stile winetricks: VC++ redistributable, .NET/Mono, font core, d3dcompiler, ecc.
4. Si esegue l'installer, normalmente con la sua interfaccia.
5. Rilevamento dell'eseguibile principale: dai collegamenti `.lnk` creati dall'installer (Desktop, menu Start), altrimenti un elenco degli `.exe` installati da cui scegliere.
6. Icona estratta dall'eseguibile (`.ico` → `.icns`), nome e versione dalle risorse del file.
7. Si genera la `.app`: `Info.plist` (nome, icona, identificativo `bundle id`), un piccolo launcher nativo che avvia Wine con il prefix e l'eseguibile giusti, firma ad-hoc con il permesso `com.apple.security.hypervisor`.

**Come resta "a sé stante" senza file sparsi**
- Il runtime (Wine, FEX, librerie) viene copiato dentro la `.app` del programma come **clone APFS** (`cp -c` / `clonefile`): sul disco non occupa spazio finché i file non cambiano, ma ogni app è completa e funziona da sola, anche se si cancella l'app principale.
- I dati che cambiano (prefix, salvataggi, impostazioni del programma) stanno in `~/Library/Application Support/<NomeApp>/apps/<programma>/`, coerente con la regola della fase 6: scrivere dentro una `.app` firmata ne romperebbe la firma.
- Cancellando la `.app` del programma e la sua cartella dati non resta nulla.

**Da decidere più avanti**
- Interfaccia: finestra SwiftUI nell'app principale, con un comando da terminale equivalente per l'automazione.
- Impostazioni per programma: variabili d'ambiente, override delle DLL, backend grafico, risoluzione, salvati nella cartella dati (non nella `.app`).
- Aggiornamento del runtime nelle `.app` già create: ricreare il clone dal runtime nuovo, mantenendo i dati.
- Esportare un programma installato (prefix incluso) come archivio da spostare su un altro Mac.

## Fase 7: robustezza della VM

- ⬜ Segnalare ad Apple il kernel panic SPTM (`VIOLATION_ILLEGAL_MAPPING_TYPE`) causato da `hv_vm_map` su una pagina non adatta: un'API usata da un processo utente non dovrebbe poter mandare in panic il kernel.
- ⬜ Distruggere la vCPU quando il thread termina.
- ⬜ Verificare se `hv_vcpus_exit` su una vCPU non ancora partita viene ricordato: è il caso limite della sospensione.
- ⬜ Sezioni condivise scrivibili basate su file: oggi non supportate sotto HVF.
- ⬜ Contatori e log diagnostici attivabili, sul modello del loro `*_TRACE`.

## Idee di MetalSharp che NON adottiamo (le risolve già la VM)

| Loro soluzione | Perché da noi non serve |
|---|---|
| Trucco della versione SDK per conservare x18 | nella VM x18 appartiene al guest |
| `KUSER_SHARED_DATA` spostata a `0x17ffe0000`, letture emulate, ribasamento nel JIT di FEX | nella VM è all'indirizzo vero `0x7ffe0000` |
| Pagine 16K: emulazione W^X, emulazione delle scritture, alternanza RX/RW | nella VM le pagine sono 4K e i permessi li decide la tabella del guest |
| Entitlement `cross-architecture-support` per memoria sotto 4GB e pagine 4K | la finestra a `0xE000000000` + doppia mappatura nella VM, senza account a pagamento |
| `NtFlushInstructionCache` che riporta le pagine a RX | lato host le pagine non sono mai eseguibili; il codice gira solo nella VM |

Da tenere comunque presente come ricerca: chi ha l'entitlement potrebbe usare un modo misto, processo nativo per ARM64/x86-64 e VM solo dove serve, per evitare il costo delle syscall.

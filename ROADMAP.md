# Roadmap: Wine nativo per Apple Silicon con VM Hypervisor

Obiettivo: Wine nativo arm64 su macOS, che esegue programmi Windows ARM64, x86-64 e x86 a 32 bit senza Rosetta, sfruttando Apple Silicon. Il codice Windows gira in una VM Hypervisor.framework (pagine 4K, memoria sotto 4GB, x18 libero, TSO hardware); il codice x86 è tradotto da FEX.

Legenda: **[MS]** = idea presa da [MetalSharp-Wine](https://github.com/metalsharp/MetalSharp-Wine), da reimplementare (il loro codice è AGPL-3.0, Wine è LGPL: si prendono le idee, non il codice). Stato: ✅ fatto · 🔧 scritto, da provare · ⬜ da fare.

## Stato attuale (2026-11-19)

| | Stato |
|---|---|
| Wine arm64 nativo, codice Windows ARM64 nella VM | ✅ `cmd /c echo` funziona, prefix creato da zero |
| Syscall, unix call, callback, eccezioni nella VM | ✅ |
| Sospensione e contesto dei thread (SIGUSR1/SIGQUIT) | ✅ test kernel32 `thread` 962/962, test mirato 2000/2000 |
| FEX compilato (`libwow64fex.dll`, `libarm64ecfex.dll`) | ✅ |
| Build Wine ARM64EC + i386 | ✅ compila |
| x86-64 via ARM64EC nella VM (FEX) | ✅ `hello64.exe` RESULT ok |
| x86 a 32 bit via WoW64 (FEX) nella VM, finestra di memoria a `0xE000000000` | ✅ congelato (decisione del 2026-11-18): resta, nessun lavoro nuovo specifico; controllo rapido `tests/i386-smoke.sh` |
| Audio (CoreAudio, 44,1-96 kHz, Bluetooth) | ✅ fase 5 passo 2 |
| Video (Media Foundation e DirectShow: H.264, HEVC, VP9, MPEG-1/2, WMV, AAC, MP3) | ✅ fase 5 passo 3, tre architetture |
| OpenGL (GL 2.1 di Apple), D3D9 con wined3d | ✅ tre architetture |
| Vulkan con MoltenVK, D3D12 con vkd3d | ✅ tre architetture (fase 5 passo 5) |
| D3D11 con DXMT | ✅ ARM64 e x86-64, livello 11_1, dentro la `.app` (`BARRIQUE_D3D=dxmt`); ⬜ 32 bit (decisione: fuori) |
| `Barrique.app` unica (macOS 26+), prefisso in `~/Library/Application Support/Barrique` | ✅ costruita da `scripts/make-dist.sh`, provata con un prefisso nuovo (fase 6) |
| Firefox ARM64 (sandbox, WebGL su GPU) | ✅ 120 fps, sandbox di Chromium attiva |
| Firefox x64 (FEX) | 🔧 pagine ok in 5 run su 6 dopo la correzione della sospensione; il processo GPU crasha ancora (fase 5) |
| Input: tastiera e mouse di base | ✅ attraverso winemac; ⬜ mouse raw, cursore bloccato, controller, metodi di input (vedi sotto) |
| Doppio clic sulla `.app`, apertura di un `.exe` dal Finder | ✅ lanciatore nativo, "Apri con" per `.exe`/`.msi`/`.bat`/`.lnk` (fase 6) |
| Steam | ⬜ fase 8 |
| msync (sincronizzazione in-process) | 🔧 port da CrossOver 26.3, spento di default (`WINEMSYNC=1`): primitive del server da 27-28 µs a 3,6-4 µs, test di sincronizzazione come senza msync |

## Cosa manca: quadro completo (2026-11-19)

**x86 a 32 bit congelato** (decisione dell'utente, 2026-11-18, studio in `research/drop-x86.md`): resta perché molti installer anche di programmi x64 sono a 32 bit (Firefox, 7-Zip), oltre a giochi e programmi a 32 bit e a Gecko; non si fa lavoro nuovo specifico per i 32 bit (come per D3D10/11) e i fallimenti solo i386 restano aperti. Verifica: `tests/i386-smoke.sh` (installer a 32 bit di un programma x64, `hello32`, OpenTTD x86, .NET x86, HTML con Gecko x86).

**Prima alfa pubblica su GitHub** (domanda dell'utente, 2026-11-19): possibile tra pochi giorni, con i bug noti dichiarati. Fatto il 2026-11-19: licenza del repo LGPL 2.1+ (`LICENSE`, `COPYING.LIB`; font Barrique Caption sotto SIL OFL 1.1), `NOTICE.md` con versione, licenza e sorgente di ogni componente, testi delle licenze nella `.app` (`Contents/Resources/licenses`, da `scripts/collect-licenses.sh`, che fallisce se ne manca uno), `docs/README-alpha.txt` per gli utenti (requisiti, passo Gatekeeper con `xattr -dr com.apple.quarantine`, problemi noti, log), `scripts/make-release.sh <versione>` (zip con `ditto`, 0,44 GB, firme intatte, `SHA256SUMS`; provato: l'app estratta parte). Manca: prova su un vero macOS 26 (qui c'è macOS 27); tag e release su GitHub.

Tutto ciò che è aperto, per area, compresi i punti minori. Priorità: **A** blocca l'uso quotidiano o un obiettivo dichiarato (Steam, `.app`), **B** importante ma aggirabile, **C** rifinitura o caso raro. Il dettaglio è nelle fasi indicate.

**Bug noti**
- **A** Firefox (x64 e ARM64): crash nell'uso normale anche della build ARM64 (2026-11-18: `ClearBlackMarkedNodes` nel processo delle pagine, `Receiver::recv` di WebRender su memoria liberata nel processo GPU), quindi non dipende da FEX. Ipotesi in prova: gara in `map_guest_page` (protezioni lette fuori dal lock dell'HVF; un decommit in mezzo lasciava la pagina mappata nella VM con le protezioni vecchie), corretta con un contatore di generazione (`map_generation` in `hvf.c`, incrementato da `hvf_invalidate`; il fault rilegge le protezioni se è cambiato). Dopo la correzione `memstress` e `shmstress` restano puliti, ma **i crash di Firefox restano** (8+8 run: ARM64 crash in 3 run, x64 in 5; stesse firme, `ClearBlackMarkedNodes` e WebRender): la gara era reale e la correzione resta, però non è la causa. Causa ancora da trovare. Esclusi il 2026-11-18/19 con interruttori diagnostici dell'HVF (`BARRIQUE_HVF_FAULTAROUND=0`, `BARRIQUE_HVF_LAZY_FP=0`, `BARRIQUE_HVF_VCPU_RELEASE=0` mai / `=2` a ogni attesa; 8 run ARM64 ciascuno, crash in 1-5 run su 8 senza differenze reali: con rilascio "mai" 5/16, "sempre" 1/8) e `WriteProcessMemory` tra processi (`tests/pe/wpmstress.c`: `mach_vm_write` del wineserver potrebbe dare pagine fisiche nuove al processo di destinazione senza che la sua VM lo sappia; 2000 generazioni su ARM64 e x64, 0 dati vecchi in entrambe le direzioni). Pista TLB provata il 2026-11-19: corretto un difetto (un vCPU fermato nel codice utente veniva segnato come "TLB aggiornato" senza svuotarlo; ora passa prima dal punto sicuro e poi dallo svuotamento), e aggiunto uno "shootdown" sperimentale (`BARRIQUE_HVF_TLB_KICK=1`: a ogni cambio di mappatura tutti i vCPU in esecuzione escono dalla VM). Risultato su 12 run ARM64: base con la correzione 9/12 bench finiti (3 fermi, 2 crash report, come prima); con lo shootdown 0/12 (Firefox cambia mappature di continuo, le uscite lo rendono inutilizzabile). Il TLB non sembra la causa; lo shootdown resta spento. In passato: il processo GPU crasha ogni tanto in WebRender, sia con DXMT sia con wined3d; Firefox lo riavvia da solo (letture di memoria non mappata `base 64K + 0x14c4` in WebRender, memoria liberata in `Renderer::update`). Causa principale delle pagine bloccate già corretta (sospensione dei thread x64 sotto HVF, giro 23). Fase 5.
- **B** D3D9 su aarch64 nel round 22: il cambio di risoluzione non avviene (1024x640 resta 1512x982), poi crash e timeout a catena. Da rifare per chiudere il cancello del passo 5. Fase 5.
- **B** `VirtualAlloc` sotto i 4 GB in un processo a 64 bit restituisce l'indirizzo della finestra bassa (`0xE0xxxxxxxx`) invece di quello chiesto (visto il 2026-11-19 con `wpmstress`): funziona perché il guest vede entrambi, ma su Windows si riceve l'indirizzo esatto; programmi che vogliono memoria sotto i 2 GB (JIT come LuaJIT, alcuni motori) o che confrontano l'indirizzo restituito con quello chiesto potrebbero fallire. Fase 7.
- **C** kernel32 `console`: a volte il figlio non esce con Ctrl-C (instabile anche prima). Fase 7.
- **C** i386: kernel32 `virtual` (`test_stack_commit`, pagine da 16K), `debugger` (`DEBUG_PROCESS` da un thread secondario, 0x2db), `loader` (`Wow64Transition`), `process` (livello CPU di FEX). Fase 7.
- **C** ntdll `exception`: eccezioni "collise" su aarch64, ricorsione dello srotolamento da pc 0 su x86-64, stack overflow dopo `RtlUnwind` su i386. Fase 7.
- **C** Resti dei test per architettura (ws2_32 `afd` i386, user32 `class` i386, rpcrt4 `cstub` x86-64, dbghelp stack walk x86-64, crypt32 `sip`, exe x64 senza rilocazioni sotto i 4GB). Fase 7.

**Input**
- 🔧 **A** Mouse "raw" (movimento grezzo, `WM_INPUT`): registrazione ok; resta da verificare a mano che il movimento relativo arrivi con il cursore bloccato (`tests/pe/inputtest.c`, modalità interattiva) e valutare GCMouse. **[MS]**
- 🔧 **A** Cursore bloccato nella finestra (`ClipCursor`, metodo "confinement" di winemac, senza permessi) verificato sulle tre architetture; cursore nascosto e schermo intero nativo di macOS da provare a mano (`inputtest`). **[MS]**
- ✅ Elenco dei dispositivi raw (`GetRawInputDeviceList`) vuoto sotto HVF: `win32u` riceve l'handle raw di mouse e tastiera virtuali in un buffer sul proprio stack, che è lo stack macOS del thread e non una vista Windows, e `check_write_access` lo rifiutava (`STATUS_INVALID_USER_BUFFER`). Ora i buffer sullo stack host del thread sono accettati: 2 dispositivi su ARM64, x64 e i386; user32 `input` +8 controlli per architettura, dinput pulito su ARM64 (round 25). Correzione generale: vale per ogni chiamata al server dal lato Unix con risposta su stack.
- 🔧 **A** Controller: SDL2 2.32.10 compilata da noi (`scripts/build-sdl2.sh`, macOS 26; quella di Homebrew è sdl2-compat e richiede macOS 27) e inclusa in `lib/deps`; `winebus` la carica da `@loader_path/../../deps` e il backend SDL parte (2026-11-18). Da provare con un controller vero (`inputtest`). **[MS]**
- **B** Tastiera italiana e tasti accentati, tasti morti; corrispondenza Cmd/Ctrl (copia/incolla con Cmd), tasti funzione e `Fn`. Da verificare.
- **B** Metodi di input (giapponese, cinese, coreano: IME di macOS verso IMM32/TSF). Da verificare.
- **B** Trackpad: scorrimento fluido e "naturale", velocità della rotellina, zoom a due dita (`WM_GESTURE`/Ctrl+rotellina). Da verificare.
- **C** Cursore su Retina a 192 DPI: dimensione e nitidezza dei cursori Windows. Da verificare.
- **C** Appunti (testo, immagini, file) e trascinamento di file dal Finder ai programmi Windows: winemac li supporta, mai provati sotto Barrique.

**Grafica**
- **B** DXMT: confronto sul banco con wined3d, test d3d11/dxgi con DXMT (oggi si fermano su bug di casi limite di DXMT), buffer negli heap Metal (finestra Metal sperimentale, spenta).
- **B** D3D9/10/11 con DXVK su MoltenVK, da confrontare con wined3d e DXMT. **[MS]**
- **B** Banco di prova: campioni DirectDraw e Vulkan diretto, riferimenti, validazione di Metal.
- **C** OpenGL 4.6 su Metal (Zink su KosmicKrisp): prova fatta il 2026-11-18, **soglia non superata**: KosmicKrisp 26.3.1 non ha transform feedback né geometry shader, quindi Zink si ferma a GL 2.1 (sotto il GL 4.1 di Apple). Si resta su CGL; da riprovare a ogni release di Mesa. **[MS]**
- **C** VKD3D-Proton al posto del vkd3d di Wine: da valutare.
- ➖ D3D10/11 per i programmi a 32 bit: fuori per decisione dell'utente.

**Componenti da includere nella `.app`**
- **B** Gecko per i programmi x64: crasha in `xul.dll` (hash di stringa su un puntatore `2`) sotto ARM64EC/FEX; Gecko a 32 bit funziona, per ARM64 non esiste. Fase 5.
- **C** .NET AnyCPU parte come processo x64 (ARM64EC + FEX) invece che ARM64 nativo: va bene, ma più lento.
- **B** `innoextract` e `cabextract`; VC++ 2015-2022 redistributable offerti all'installazione. **[MS]**

**App e distribuzione**
- **B** Prova di Barrique su un vero macOS 26 (le funzioni di macOS 27 sono state tolte o protette, fase 6).
- **B** Fase 6b: assistente "da installer a `.app`" (prefix dedicato, icona dall'exe, clone APFS del runtime).
- **C** Aggiornamento del runtime nelle `.app` create, esportazione di un programma installato.

**Prestazioni**
- 🔧 **B** msync: sincronizzazione in-process su memoria condivisa, port del msync di CrossOver 26.3 (LGPL, sorgenti scaricati il 2026-11-18) dentro lo strato `inproc_sync` di Wine 11.22, spento di default (`WINEMSYNC=1`). Prima: mutex, semafori, eventi e `WaitForMultipleObjects` costavano **26-32 µs** (wineserver); con msync **3,6-4 µs** (`tests/pe/syncbench.c`, tre architetture; il resto è l'uscita dalla VM a ogni syscall), ping-pong di eventi da 31-32 a 13-16 µs. Test di conformità (`ntdll` sync/om, `kernel32` sync/thread) uguali a quelli senza msync dopo due correzioni: conteggio precedente di `NtReleaseMutant` (msync dà il numero di acquisizioni) e mutex abbandonato quando l'ultimo handle è chiuso mentre un altro thread aspetta (il server lo teneva solo finché c'era l'handle; ora resta tra gli "orfani" finché il proprietario lo rilascia o termina; il difetto viene da CrossOver). Programmi veri (2026-11-19): OpenTTD sulle tre architetture identico, 7-Zip `b -mmt8` compressione +8%, decompressione uguale; **Firefox si blocca** (1/12 run finiti, CPU a zero, thread fermi in attese msync, per esempio il thread dei socket in `NtWaitForSingleObject`: probabile evento segnalato dal server che msync non sveglia) e dopo il blocco `wineserver -k` lascia processi vivi (un thread resta in `read()` sulla pipe del server), che tengono le VM fino a `HV_NO_RESOURCES`. Da guardare anche `mach_message_pump` in `server/msync.c`: `if (i > 1) unregister_wait` sembra dover essere `i > 0` (attesa lasciata registrata, risvegli spuri). Da fare prima di accenderlo: blocco di Firefox e processi orfani; poi valutare `os_sync_wait_on_address` (pubblica) al posto di `__ulock_wait`. **[MS]**
- **B** Costo dell'uscita dalla VM (~1,7 µs per syscall contro 0,1 nativi): gestire nella VM le syscall frequenti che leggono solo dati del processo. Fase 7.
- **C** Firefox x64 sotto FEX ~20 s all'avvio: limite della traduzione, la build ARM64 non ce l'ha.

**Robustezza della VM (fase 7)**
- **B** Pagine host da 16K: liberare o proteggere 4K dentro una pagina da 16K, guard page e crescita dello stack, write-watch per pagina.
- **B** Sezioni condivise scrivibili basate su file: due viste dello stesso file non si vedono finché non si riscrive (`hvf_shadow`).
- **C** `hv_vcpus_exit` su una vCPU non ancora partita (caso limite della sospensione).
- **C** Contatori e log diagnostici (`WINE_HVF_STATS` e `+fexnotify` esistono già).
- **C** Segnalare ad Apple il kernel panic SPTM di `hv_vm_map`.

**Gioco di prova**
- ✅ **OpenTTD 15.3** (GPL, build ufficiali ARM64, x86, x64 da `cdn.openttd.org`, grafica OpenGFX 8.0), scelto come gioco di riferimento senza problemi di licenza (`tests/apps/openttd.sh`). Senza finestra (`-v null:ticks=50000`): 12 run su 12 ok, 4,4-4,5 s ARM64, 5,3-5,5 s x64, 5,5-5,9 s x86; **problema noto, solo x86:** il **primo avvio** di OpenTTD x86 in un prefisso nuovo a volte si blocca (2 volte su 7 prefissi nuovi; i lanci successivi partono in meno di 3 s, oltre 20 su 20). Mai su x64 e ARM64, mai con `WINEDEBUG` attivo: dipende dalla temporizzazione. Con x86 congelato non si insegue; `tests/apps/openttd-firstrun.sh` prova più prefissi nuovi e fotografa i processi bloccati con `sample` quando ricapita. Il controllo `tests/i386-smoke.sh` può quindi fallire su OpenTTD al primo giro. Con la finestra: OpenTTD ARM64 aperto e giocato a mano (2026-11-18), chiuso senza errori né crash (nel log solo avvisi innocui di macOS sullo spostamento della finestra); x64 e x86 con la finestra da provare (`openttd.sh play <arch>`).
- ⬜ Un secondo gioco open per Direct3D 11/12 e 3D: nessuno di quelli guardati (SuperTuxKart, Luanti, GZDoom) ha una build ARM64 Windows ufficiale.

**Steam (fase 8)**: tutto da fare. Installer, client a 32/64 bit, `steamwebhelper` (CEF, sandbox, processo GPU), login, download e avvio di giochi, overlay, controller.

## Fase 1: x86 funzionante ✅ (chiusa il 2026-11-14)

- ✅ Test `hello64.exe` (x86-64): FEX ARM64EC nella VM, risultato corretto.
- ✅ Test `hello32.exe` (x86 a 32 bit): FEX WoW64 nella VM, risultato corretto, senza entitlement a pagamento.
- ✅ Nessun fault lato host durante wineboot e i test: conversione dei puntatori a 32 bit comune a tutte le librerie host (`wine/unixlib.h`), messaggi di finestra con puntatore (`is_pointer_message`), tabelle delle code page, trap dell'host gestite come errore della syscall.
- ✅ **[MS]** Chiavi `Wow64\amd64` e `Wow64\x86` in `wine.inf` (`PreInstall.ntarm64`, senza sovrascrivere) e DLL di FEX trovate da Wine come DLL interne (sviluppo: `WINEDLLPATH=~/Work/Personale/wine/fex-dlls`; nella `.app` accanto alle DLL di Wine).
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

**d3dmetal-native (utmapp, MIT, valutato il 2026-11-17).** Fa da ospite a `D3DMetal.framework` fuori da Wine: implementa l'interfaccia `GFXT` (finestre, eventi, registro, memoria) che D3DMetal si aspetta dal Wine di Apple, e aggiunge risorse condivise tra processi via fd, risorse tiled di D3D12 su heap sparsi di Metal e `MTLResidencySet`. Non usabile in Barrique:
- D3DMetal è solo x86_64 (verificato sulla 2.0: framework, `libdxcompiler`, `libmetalirconverter`, `libdxilconv`) e dovrebbe stare sul nostro lato host, che è arm64. Portare d3dmetal-native ad arm64 non serve finché il framework di Apple resta x86_64.
- L'alternativa, un processo x86_64 sotto Rosetta a cui inoltrare ogni chiamata D3D, reintroduce Rosetta e aggiunge latenza a ogni draw call e un problema di memoria GPU condivisa (vedi passo 4 di FASE5-AV).
- La licenza del Game Porting Toolkit non permette di includere il framework nella `.app` (lo dice anche il README).

Riusabile (MIT, con attribuzione) per il nostro D3D su Metal (DXMT, passo 7): il disegno delle risorse e fence condivise, dei keyed mutex, delle risorse tiled su heap sparsi con budget, dei `MTLResidencySet`; i test (triangolo e cubo D3D11/D3D12, condivisione tra processi, stress multi-thread) come programmi per il banco di prova del passo 6. Da rivalutare solo se Apple pubblica un D3DMetal arm64 con una licenza che ne permette la distribuzione: allora servirebbe un'interfaccia `GFXT` nostra sul lato unix, e d3dmetal-native sarebbe il riferimento.

Stato dei passi di [FASE5-AV.md](FASE5-AV.md):
- ✅ Passo 0, base di partenza (giro 5, 2026-11-15).
- ✅ Grafica a 64 bit sotto HVF: stringhe e buffer GL, blocco e use-after-free in winemac (giro 6, 2026-11-16).
- ✅ Passo 1, FreeType e font (giro 7, 2026-11-16): FreeType incluso con `@rpath`, 35 unità migliorate. Dal test di fumo: `fork()` da un thread Windows sotto HVF (installer VC++, crash all'uscita dei processi), winedbg e wineserver col debugger.
- ✅ Passo 2, audio (giri 8 e 14): timer di coreaudio, salvataggio del registro in un processo figlio (via il buco audio di ~60 ms ogni 20 s). 44,1/48/96 kHz e Bluetooth (AirPods) senza periodi persi.
- ✅ Passo 3, video (giri 10-14): GStreamer 1.28 con gst-libav sopra FFmpeg LGPL compilati da noi in `deps/`; Media Foundation decodifica H.264, HEVC, VP9, AAC, MP3, WMV2/WMA2 sulle tre architetture. Verificati i386 (correzione WoW64) e MPEG in DirectShow. Giro 12: MPEG-1/2 anche in Media Foundation (gestore `.mpg/.vob/.ts`, decoder MPEG MFT, MP2 in `mp3dmod`), ok sulle tre architetture. Giro 13: mf `transform` da crash a 0 fallimenti sulle tre architetture (encoder H.264 di VideoToolbox, MPEG audio MFT a sé, `swscale` con `accurate_rnd` su ARM), mfreadwrite 0 fallimenti. Giro 14: decodifica VideoToolbox misurata (~4× meno CPU, bit-exact) ma spenta, rompe MPEG-2 e `h264dec`; riferimenti di prova più severi (dY 0,00).
- ✅ Passo 1b, qualità del testo (giro 11): Retina a 192 DPI nei default dei prefissi della `.app`, Selawik/Carlito/Caladea per Segoe UI/Calibri/Cambria, sostituzioni CJK anche per DirectWrite, fallback DirectWrite sui font macOS. Giro 12: i programmi non DPI-aware sono scalati 2× da Wine come su Windows.
- ✅ Passo 4, memoria della GPU nella VM (giro 15): copia come base (31,8–54,7 GB/s), importazione in Metal provata senza panic (memoria anonima prima e dopo l'uso della GPU, `MTLBuffer`, IOSurface); `hvf.c` rifiuta comunque le pagine della GPU finché un backend non le usa.
- 🔧 Passo 5, Vulkan con MoltenVK (giri 16-18): MoltenVK 1.4.2 compilato da noi (`scripts/build-moltenvk.sh`), senza loader. Sotto HVF la memoria Vulkan visibile dalla CPU è memoria Windows importata (`VK_EXT_external_memory_host`, strategia del passo 4). vulkan-1 in fondo sulle tre architetture (13 fallimenti, tutti "succeeded inside todo"); d3d12 su vkd3d 671 controlli, 0 fallimenti anche su i386. Corretti: handle e puntatori WoW64 in winevulkan/win32u, `VK_EXT_surface_maintenance1` mai attivata (bug di Wine), blocchi su finestre nascoste/minimizzate/vuote, crash di MoltenVK sul thread di Metal, swapchain a 3 immagini (D3D12 da ~350 a ~850 fps). La grafica a 32 bit (opengl32, wined3d, d3d9/11/12) funziona dopo cinque correzioni WoW64 sotto HVF. Cache delle pipeline: MoltenVK serializza già `VkPipelineCache` (non c'è niente da configurare in MoltenVK); il salvataggio su disco spetta a vkd3d e DXVK. Manca: il cancello del passo (round 22: vulkan-1, d3d11, dxgi senza regressioni; d3d9 aarch64 da rifare).
- 🔧 Passo 6, banco di prova grafico (giro 17): `tests/av/gears.c`, ingranaggi con OpenGL, D3D9, D3D11, D3D12 (FPS, primo fotogramma, controllo su un fotogramma riletto), tre architetture. OpenGL ~3300 fps, D3D9 ~2200, D3D12 ~850, D3D11 con DXMT 120-465 (sincronia col display). Mancano gli altri campioni (DirectDraw, Vulkan diretto), i riferimenti e la validazione di Metal.
- 🔧 Passo 7, Direct3D (giro 19): **D3D11 con DXMT funziona** sotto HVF, livello 11_1, su programmi ARM64 e x86-64 (FEX). DXMT (main `0df8610`, LGPL) compilato senza modifiche: vieta il codice generato da AI, come FEX, quindi gli adattamenti sono dal lato Wine: `macdrv_functions` in winemac, buffer di DXMT su memoria Windows (`cocoa_dxmt.m`, swizzling di `newBufferWithLength`), DLL ARM64X (`scripts/build-dxmt.sh`), LLVM 15 per il suo compilatore di shader (`scripts/build-llvm15.sh`). wined3d resta il ripiego (D3D9 ok; D3D11 solo livello 9_3, senza shader). Fatto poi (giro 21): installazione nella `.app` e interruttore `BARRIQUE_D3D=dxmt` nel loader. Mancano: buffer negli heap Metal (finestra Metal sperimentale, spenta), confronto sul banco, i test d3d11/dxgi con DXMT.
- **Programmi a 32 bit e D3D10/11 (decisione dell'utente, 2026-11-17: restano fuori):** DXMT non funziona a 32 bit sotto HVF. La sua libreria unix usa gli indirizzi Windows sotto i 4 GB senza tradurli (Barrique li tiene a `0xe000000000` + indirizzo, perché macOS non mappa sotto i 4 GB), e la correzione andrebbe fatta in DXMT, che non possiamo modificare. Per ora i 32 bit restano su wined3d (D3D9 e precedenti, OpenGL) e vkd3d (D3D12); D3D10/11 a 32 bit è fuori, come in CrossOver 27. Da riprendere se l'utente scrive la traduzione in DXMT (`wine_low_window_ptr` sui puntatori dei 32 bit in `winemetal_unix.c`).

- 🔧 **[MS]** Direct3D su Metal in versione ARM64X, così un'unica build serve sia ai programmi ARM64 sia a quelli x86-64 (ARM64EC):
  - ✅ D3D10/11 con DXMT, ARM64X (passo 7); niente fork, DXMT è usato com'è;
  - ⬜ D3D9/10/11 con DXVK su MoltenVK (da confrontare con wined3d e DXMT sul banco);
  - 🔧 D3D12 con vkd3d (quello di Wine) su MoltenVK funziona; VKD3D-Proton da valutare.
- ⏸️ **[MS]** OpenGL 4.6 su Metal al posto del GL 4.1 di Apple (prova del 2026-11-18: KosmicKrisp 26.3.1 senza transform feedback e geometry shader, Zink a GL 2.1; si resta su CGL, dettagli in `research/opengl46-metal.md`). Studio in `research/opengl46-metal.md` (2026-11-18): Zink su MoltenVK si ferma a ~GL 2.1 (niente geometry shader né transform feedback); l'unica strada è Zink su KosmicKrisp (Mesa) dietro `BARRIQUE_GL=zink`, CGL resta il default. Prima una prova di 1-2 giorni fuori da Wine (soglia: GL 3.3), poi 4-6 settimane; il punto duro sono i buffer persistenti sotto HVF.
- ✅ **[MS]** MoltenVK con cache delle pipeline: MoltenVK incluso (passo 5) e serializza `VkPipelineCache`; la persistenza su disco è di vkd3d/DXVK.
- ✅ **[MS]** FFmpeg (VideoToolbox/AudioToolbox) e GStreamer/gst-libav per `winedmo`, `winegstreamer`, Media Foundation e DirectShow (passo 3 di FASE5-AV).
- **Input** (stato in "Cosa manca"): tastiera e mouse di base funzionano attraverso winemac; da fare:
  - ⬜ **[MS]** Controller: winebus con SDL2 + IOHID (XInput e DirectInput).
  - ⬜ **[MS]** Mouse raw con GCMouse e schermo intero nativo di macOS; cursore bloccato (`ClipCursor`) e nascosto nei giochi.
  - ⬜ Da verificare: tastiera italiana e tasti morti, Cmd/Ctrl, metodi di input (IME), trackpad (scorrimento, zoom), cursore su Retina, appunti e trascinamento dal Finder.
- 🔧 **[MS]** Font:
  - ✅ Arial, Times New Roman e Courier New: macOS li ha già in `/System/Library/Fonts/Supplemental` e Wine li carica tramite CoreText, Liberation non serve;
  - ✅ famiglie CJK di Windows associate ai font di sistema di macOS (Hiragino, Songti, PingFang, Apple SD Gothic Neo; giro 9);
  - ✅ glifi dei pulsanti delle finestre (Segoe Fluent Icons / Segoe MDL2 Assets, usati per esempio dalla barra del titolo di Firefox): **Barrique Caption**, font nostro con disegni originali, generato da `resources/fonts/make-caption-font.py`, sostituzioni in `prefix-defaults.reg` (giro 23).
- 🔧 **[MS]** Mono e Gecko inclusi (2026-11-18), invece della finestra che chiede di scaricarli: Wine Mono 11.3.0 (pacchetto arm64, che contiene anche quello x86, con i DLL nativi x86_64 compilati ARM64EC) e Wine Gecko 2.47.4 x86 e x86_64, estratti in `share/wine/mono` e `share/wine/gecko` (installazione condivisa, `~/Work/Personale/wine/deps-addons`), tolto `mscoree=d;mshtml=d` dal lanciatore; la `.app` passa a 1,5 GB. Provato dalla `.app` con un prefisso nuovo (23 s, `tests/addons/run.sh`): C# compilato con `mcs` di Mono e programmi .NET AnyCPU, x86 e x64 ok; HTML (`mshtml`, `tests/addons/html.c`) ok a 32 bit; Gecko x86_64 va in crash in `xul.dll` sotto FEX (da indagare); Gecko ARM64 non esiste (come in Wine upstream).
- ✅ **[MS]** CEF/Chromium: `--in-process-gpu` automatico (senza, la pagina resta bianca). Nel loader di ntdll (`cef_in_process_gpu`: processi browser di CEF, cioè `libcef.dll` accanto all'exe o `steamwebhelper.exe`, senza `--type=`; `BARRIQUE_CEF_GPU=0` lo spegne), test `tests/pe/cmdline.c`: verificato dalla `.app` il 2026-11-18 su ARM64 e x86-64 (switch aggiunto al processo browser, assente con `--type=renderer`, in una cartella senza `libcef.dll` e con `BARRIQUE_CEF_GPU=0`).
- 🔧 **[MS]** msync, sincronizzazione su memoria condivisa: port da CrossOver fatto il 2026-11-18, spento di default (stato e numeri in "Cosa manca", Prestazioni). Studio in `research/msync.md`.
- 🔧 **[MS]** Firefox (giro 23, 2026-11-18): la build x64 passa da FEX ed è lenta (~20 s all'avvio, JS ~2,7× più lento); la build **ARM64** va a 120 fps con WebGL hardware (ANGLE su D3D11 con DXMT). Corretti per l'ARM64: (1) la sandbox di Chromium controlla che gli stub syscall di `ntdll` siano `svc #N; ret` come su Windows, sotto HVF ora lo sono (`hvf_windows_syscall_stubs`, `BARRIQUE_SVC_STUBS=0` per spegnerli); (2) `GetProcessMitigationPolicy` lasciava il buffer non scritto e il processo GPU si fermava su `!IsWin32kLockedDown()`. Con wined3d niente WebGL: per Firefox serve `BARRIQUE_D3D=dxmt`. **Aperto, x64:** i processi delle pagine e quello GPU della build x64 crashano spesso, con salti verso memoria allocata a runtime e puntatori a memoria già liberata, sia con DXMT sia con wined3d. Esclusi TSO hardware (`WINE_HVF_TSO=0` lo spegne), stub `svc` (`BARRIQUE_SVC_STUBS=0` torna al comportamento precedente), `GetProcessMitigationPolicy`, il font, la cache shader di DXMT, e una regressione (stesso esito con i sorgenti del giorno prima). Con `FEX_SMCCHECKS=full` i crash spariscono (0 su 4 run contro 9 su 6) ma Firefox diventa inutilizzabile: FEX esegue traduzioni non più valide, cioè non viene avvisato di qualche modifica al codice x64. I simboli di Firefox (servizio di Mozilla) mostrano però crash in sottosistemi diversi (cycle collector, WebRender, SWGL): memoria che si rovina in generale, non codice vecchio di FEX (con `full` cambiano solo i tempi). Esclusi anche: porte di completamento I/O (`tests/pe/iocp.c`, schema IPC di Chromium anche tra processi, 0 anomalie), coerenza della memoria tra vCPU (`tests/pe/memstress.c`: decommit/recommit e `VirtualProtect` con thread concorrenti, 0 errori), opzioni del JIT di FEX (multiblocco, AVX, AFP, LRCPC2). Traccia `WINEDEBUG=+fexnotify` per le notifiche di memoria a FEX. **Causa trovata (giro 23, 2026-11-18):** gli indirizzi dei salti sbagliati erano tutti `multiplo di 16K + 0x7a0`, cioè `hvf_guest_syscall_stub` in `ntdll.so`. Due difetti nostri nella sospensione dei thread x64 sotto HVF: (1) `is_emulated_code` considerava x64 la pagina degli stub HVF, quindi un thread sospeso lì ripartiva in FEX a quell'indirizzo; (2) un segnale rimandato mentre la vCPU girava veniva gestito leggendo e riscrivendo il contesto nativo anche se nel frattempo il thread era entrato in FEX, invece di usare la sospensione cooperativa (il "doorbell") come `usr1_handler`. Dopo la correzione la pagina carica in 5 run su 6 (prima ~1 su 15) e i crash dei processi delle pagine spariscono; restano crash nel processo GPU (memoria non mappata `base 64K + 0x14c4` in `SceneBuilderThread::run`, memoria liberata in `Renderer::update`), uno per run sia con DXMT sia con wined3d: non dipendono dal backend D3D. Lo stesso crash capita anche alla build **ARM64** (1 run su 6, `SceneBuilderThread::run`, riga `self.rx.recv()`): non è FEX. Su crash-stats di Mozilla la firma è rarissima e assente su Windows, quindi è specifico di Barrique. Esclusi con test mirati, tutti puliti: TSO dopo la correzione, parcheggio dei thread in stile Rust con campionamento (`tests/pe/parkstress.c`), eccezioni gestite con tutti i registri vettoriali verificati (`tests/pe/sehstress.c`). Firefox riavvia da solo il processo GPU: di solito si vede solo uno sfarfallio. Test aggiunti, tutti puliti: `simdsusp.c` (registri e stato FP attraverso sospensioni e rilascio delle vCPU, 80 thread), `shmstress.c` (memoria condivisa tra processi con viste che cambiano), benchmark di 7-Zip x64. Per ora conviene la build ARM64.

Per ognuno dei componenti grafici e multimediali va controllata la licenza.

## Fase 6: pacchetto e distribuzione 🔧 (in corso)

**Requisito: il prodotto finale è un'unica `.app` autosufficiente, senza file sparsi nel sistema.** Dentro il bundle: loader, wineserver, librerie (`@rpath`/`@loader_path`, nessun percorso di Homebrew), DLL Wine per tutte le architetture, FEX, Mono, Gecko, font e strumenti. Niente installazioni in `/usr/local`, nessun `~/.wine` di default, nessun LaunchAgent. I dati che cambiano (prefix Windows, registro, programmi installati) stanno in **un'unica cartella dell'app**, `~/Library/Application Support/<NomeApp>/` (scelta dell'utente, 2026-11-14): scrivere dentro la `.app` ne romperebbe la firma. Cancellando la `.app` e quella cartella non resta nulla nel sistema.

- ✅ App `.app` con loader firmato e permesso `com.apple.security.hypervisor` (giro 21, 2026-11-17): `scripts/make-dist.sh` costruisce `dist/Barrique.app` (1,1 GB): Wine installato nel bundle (`Contents/Resources/wine`), librerie in `lib/deps` con `@rpath` (FreeType, GStreamer/FFmpeg, MoltenVK), FEX nelle DLL di Wine, DXMT in `lib/wine/dxmt` (`winemetal.so` accanto a `winemac.so`), font, informazioni di debug tolte, firma ad hoc di ogni Mach-O e della `.app`; il permesso va al loader vero, `lib/wine/aarch64-unix/wine` (`bin/wine` lo avvia soltanto). Lanciatore `Contents/MacOS/barrique` (`barrique programma.exe`, `barrique --wine <comando>`): dati in `~/Library/Application Support/Barrique`, al primo avvio crea il prefisso (wineboot, Retina e font, chiavi di FEX, segnaposto di `winemetal.dll`), `BARRIQUE_D3D` sceglie il backend. Basta la firma ad hoc: **non serve un account sviluppatore Apple a pagamento**. Prova con solo il bundle e un prefisso nuovo (17 s): gears OpenGL, D3D9, D3D12 ok su aarch64, x86_64, i686; D3D11 con DXMT ok su aarch64 e x86_64.
- ✅ **[MS]** Pacchetto rilocabile con controllo dei Mach-O: solo arm64, `minos` ≤ **26.0** (decisione dell'utente, 2026-11-17: Barrique richiede macOS 26; prima era 15), nessun percorso di libreria esterno. Wine riconfigurato con `-mmacosx-version-min=26.0` e `--without-usb` (libusb veniva da Homebrew).
- ✅ Icona: `resources/icon.svg` (barrique su fondo color vino), `make-dist.sh` la converte in `AppIcon.icns`.
- ✅ Doppio clic sulla `.app` e apertura dal Finder (2026-11-18): lanciatore nativo `Contents/MacOS/barrique` (`launcher/main.m`), perché il Finder passa i file con un Apple Event che uno script non riceve. "Apri con" su `.exe`, `.msi`, `.bat`/`.cmd`, `.lnk` (Barrique è un'app *alternativa*, non predefinita) avvia il programma con l'output nel log della cartella dati; doppio clic sull'app apre il file manager di Wine; dal terminale gli argomenti passano allo script, ora in `Contents/Resources/barrique.sh`. Provato: terminale, `.bat` aperto tramite LaunchServices, doppio clic.
- ✅ Impostazioni di default aggiornate da sole (2026-11-19): il lanciatore applicava `prefix-defaults.reg` solo alla creazione del prefisso, quindi una cartella dati creata prima della correzione dei pulsanti di Firefox (font Barrique Caption) non li aveva. Ora ricorda l'impronta del file applicato (`prefix/.barrique-defaults`) e lo riapplica quando cambia. Verificato sulla cartella dati dell'utente: pulsanti di nuovo visibili. Se l'applicazione fallisce, il programma parte lo stesso e si riprova al giro dopo (prima `set -e` chiudeva il lanciatore in silenzio).
- 🔧 Mono e Gecko inclusi (vedi fase 5): .NET su tutte le architetture, HTML a 32 bit; manca Gecko x64.
- ⬜ **[MS]** Strumenti inclusi: `innoextract` (installer Inno/GOG) e `cabextract`.
- ⬜ **[MS]** VC++ 2015-2022 redistributable ufficiali, offerti all'installazione senza modifiche.
- 🔧 **macOS 26 davvero supportato** (2026-11-18): `pipe2` esiste solo da macOS 27 e, compilando con target 26, il linker lo lascia debole (su macOS 26 la chiamata salterebbe a 0). Corretto in ntdll (`pipe2_cloexec`, dietro `__builtin_available`, ripiego `pipe()`) e in GLib (`build-gst.sh` toglie `HAVE_PIPE2`, GLib usa il suo ripiego). `make-dist.sh` ora rifiuta qualsiasi funzione C di `libSystem` importata in modo debole, salvo le eccezioni verificate. `HV_FEATURE_REG_ID_AA64ISAR2_EL1` è solo una costante, il suo fallimento è già gestito. Manca una prova su un vero macOS 26.

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
- ✅ Distruggere la vCPU quando il thread termina (`destroy_vcpu`, fase 2), e rilascio della vCPU dei thread in attesa quando ne sono in uso più di metà; verificato con 80 thread e ~800.000 rilasci (`simdsusp.c`, giro 23).
- ✅ Sospensione dei thread ARM64EC (x64) sotto HVF: sospensione cooperativa con FEX anche per i segnali rimandati, pagina degli stub HVF mai trattata come codice x64 (giro 23).
- ⬜ Verificare se `hv_vcpus_exit` su una vCPU non ancora partita viene ricordato: è il caso limite della sospensione.
- ⬜ Sezioni condivise scrivibili basate su file: oggi non supportate sotto HVF.
- ⬜ Contatori e log diagnostici attivabili, sul modello del loro `*_TRACE`. Esistono già `WINE_HVF_STATS=1` (uscite dalla VM) e `WINEDEBUG=+fexnotify` (notifiche di memoria a FEX).
- ⬜ Pagine host da 16K (dalla fase 2): liberare o proteggere 4K dentro una pagina da 16K, guard page e crescita dello stack (`test_stack_commit`), write-watch per pagina host.
- ⬜ Uscita dalla VM (dalla fase 3): gestire nella VM alcune syscall frequenti che leggono solo dati del processo.
- ⬜ Eccezioni (dalla fase 2): `ntdll` `exception` sulle tre architetture (eccezioni collise, srotolamento da pc 0, stack overflow dopo `RtlUnwind` su i386).
- ⬜ Resti dei test (dalle fasi 2 e 4): `debugger`, `loader`, `rtl` su aarch64; ws2_32 `afd`, user32 `class`, ole32 `dragdrop` su i386; rpcrt4 `cstub`, dbghelp, crypt32 `sip` su x86-64; kernel32 `console` instabile.

## Fase 8: Steam ⬜

Richiesta dell'utente (2026-11-17): far funzionare il client Steam per Windows e i giochi che lancia. È la prova d'insieme di tutto il resto: installer, processi a 32 e 64 bit, rete, Chromium, grafica, audio e input.

Cosa serve, da verificare in ordine:
- ⬜ Installer `SteamSetup.exe` (NSIS) e primo aggiornamento del client: rete (winhttp/wininet, TLS con gnutls o lo stack di macOS), scrittura di molti file, avvio dei processi figli. Il download di Steam va chiesto all'utente.
- ⬜ `steam.exe` e i suoi processi: quali sono a 32 bit (WoW64 su FEX) e quali a 64 bit (ARM64EC su FEX); avvio e chiusura di molti processi sotto HVF (vedi il `fork()` da thread Windows già corretto e il figlio di mfplat i386 che ogni tanto si appende).
- ⬜ `steamwebhelper.exe` (Chromium/CEF, a 64 bit): la sandbox di Chromium (token ristretti, job object, desktop separati), il processo GPU (prima con `-cef-disable-gpu`, poi D3D11 sul nostro backend della fase 5 passo 7), memoria condivisa fra processi (sezioni, `NtMapViewOfSection` sotto HVF), molti thread. È il punto che di solito rompe Steam sotto Wine.
- ⬜ Login e interfaccia (libreria, negozio): rendering della finestra, input da tastiera e mouse, font (CJK compresi), DPI su Retina.
- ⬜ Download e installazione di un gioco libero (da scegliere con l'utente), Steam overlay (`gameoverlayrenderer`, hook di D3D/Vulkan), controller (fase 5, winebus).
- ⬜ Avvio di giochi: uno D3D11 a 64 bit, uno D3D9 a 32 bit, uno Vulkan, quando i passi 5-7 della fase 5 li rendono possibili.
- ⬜ Integrazione con la `.app` (fase 6/6b): Steam come programma installabile dal wizard, con prefix e cartella dati propri.
- Rischi: le API anti-cheat (EAC, BattlEye) e i DRM a livello kernel non funzionano sotto Wine; i giochi che li richiedono restano fuori.

## Idee di MetalSharp che NON adottiamo (le risolve già la VM)

| Loro soluzione | Perché da noi non serve |
|---|---|
| Trucco della versione SDK per conservare x18 | nella VM x18 appartiene al guest |
| `KUSER_SHARED_DATA` spostata a `0x17ffe0000`, letture emulate, ribasamento nel JIT di FEX | nella VM è all'indirizzo vero `0x7ffe0000` |
| Pagine 16K: emulazione W^X, emulazione delle scritture, alternanza RX/RW | nella VM le pagine sono 4K e i permessi li decide la tabella del guest |
| Entitlement `cross-architecture-support` per memoria sotto 4GB e pagine 4K | la finestra a `0xE000000000` + doppia mappatura nella VM, senza account a pagamento |
| `NtFlushInstructionCache` che riporta le pagine a RX | lato host le pagine non sono mai eseguibili; il codice gira solo nella VM |

Da tenere comunque presente come ricerca: chi ha l'entitlement potrebbe usare un modo misto, processo nativo per ARM64/x86-64 e VM solo dove serve, per evitare il costo delle syscall.

# Roadmap: Wine nativo per Apple Silicon con VM Hypervisor

Obiettivo: Wine nativo arm64 su macOS, che esegue programmi Windows ARM64, x86-64 e x86 a 32 bit senza Rosetta, sfruttando Apple Silicon. Il codice Windows gira in una VM Hypervisor.framework (pagine 4K, memoria sotto 4GB, x18 libero, TSO hardware); il codice x86 è tradotto da FEX.

Legenda: **[MS]** = idea presa da [MetalSharp-Wine](https://github.com/metalsharp/MetalSharp-Wine), da reimplementare (il loro codice è AGPL-3.0, Wine è LGPL: si prendono le idee, non il codice). Stato: ✅ fatto · 🔧 scritto, da provare · ⬜ da fare.

## Stato attuale (2026-11-14)

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

## Fase 2: correttezza della traduzione x86

Dai test di conformità (2026-11-14, risultati in `tests/results/`):
- 🔧 **Timeout su i386 sotto FEX** (16 unità contro 3 su aarch64). Causa principale trovata e corretta: la ntdll a 64 bit riconosceva i thread a 32 bit dall'indirizzo di partenza sotto i 4GB, e con la finestra non lo erano più, quindi FEX non veniva inizializzato nei thread secondari (`loader.c`, `loader_init`). `thread32.exe` ora `RESULT ok`; 6 unità su 16 ora completano (`ntdll` `pipe`, `sync`; `kernel32` `fiber`, `heap`, `module`, `toolhelp`).
- ✅ **`CreateProcess` da codice a 32 bit**: `wow64` passava all'host il percorso dell'exe (`PS_ATTRIBUTE_IMAGE_NAME`) senza convertirlo; funzionava solo per i programmi di `system32`, reindirizzati. `proc32.exe` ora `RESULT ok`; niente più errori 998 né `CreateRemoteThread` saltato.
- ✅ **Sospensione dentro FEX**: i `brk` sconosciuti diventano `EXCEPTION_ILLEGAL_INSTRUCTION` come su Linux, così FEX riconosce la sua trappola `brk #0xCAFE`. `kernel32` `thread` i386: 1034 test, 4 fallimenti (prima crash senza output).
- ✅ **Memoria nella finestra bassa**: `zero_bits` (ricerca top-down sotto il limite, `NO_MEMORY` invece di `INVALID_PARAMETER`), mappature tra processi (controllo `zero_bits` sull'indirizzo a 32 bit, limite 4GB per figli wow64), limiti di `MEM_ADDRESS_REQUIREMENTS` passati come numeri. `ntdll` `virtual` i386: da timeout a 2169 test, 31 fallimenti, tutti dovuti alle pagine host da 16K.
- ✅ **Mappature di file scrivibili e condivise** (prima rifiutate per non dare pagine di file alla VM): vista anonima riempita dal file e pagine modificate riscritte su flush, unmap e uscita del processo (`hvf_shadow`). Test `mapfile` ok su i386, x86-64, arm64. Limite: due viste dello stesso file non si vedono finché non si riscrive.
- ✅ **vCPU**: venivano persi alla fine di ogni thread (limite 64 per processo); ora distrutti, e un thread che si blocca in attesa rilascia il suo quando ne sono in uso più di metà. Test `threads100`: 100 thread vivi insieme ok in 81–154 ms su tutte e tre le architetture.
- ✅ Scatter/gather (`NtReadFileScatter`/`NtWriteFileGather`) da codice a 32 bit. `kernel32` `file` i386: 571.424 test, 8 fallimenti (16K).
- ✅ **FEX**: un unmap su un indirizzo non mappato toglieva i permessi di esecuzione a tutta la memoria da 0 in su (anche in FEX upstream); scansione DEP e ricerca della sezione ora convertono gli indirizzi della finestra bassa.
- 🔧 i386 ancora aperti: `kernel32` `virtual` arriva alla riga 4650 e muore in `test_stack_commit` (stack personalizzato con guard page, "Exception frame is not in stack limits"); `debugger` in timeout; `loader` (test di `Wow64Transition`, legato a FEX); `process` 40 fallimenti (lettura degli import di un modulo remoto).
- ⬜ Limiti delle pagine host da 16K (anche su aarch64): liberare o proteggere 4K dentro una pagina da 16K, guard page dello stack, write-watch per pagina host.
- ⬜ `ntdll` `exception`: crash su entrambe le architetture (unwind annidato, `c0000027`).
- ⬜ i386 `ntdll` `info`: `ProcessorLevel` 0 e limiti di memoria sbagliati. i386 `kernel32` `virtual`: `CreateProcess` con errore 998.
- ⬜ aarch64 `kernel32` `debugger`: scrittura a puntatore nullo in codice a 64 bit.
- ⬜ Fallimenti uguali sulle due architetture, quindi probabilmente non dovuti a FEX: `ntdll` `wow64` (~140), `kernel32` `resource` (95), `version`, `actctx`, `console` (45 su aarch64), `ntdll` `virtual` (115 su aarch64).
- ⬜ `change` (notifiche di modifica delle cartelle): limite noto di Wine su macOS, uguale in MetalSharp.
- ⬜ Test di conformità x86-64 veri: serve una build dei test come codice x64.


- ⬜ **[MS]** Registri che descrivono la CPU (ID registers). FEX li usa per scegliere i percorsi veloci (LSE, LRCPC, AFP); oggi `get_core_id_regs_arm64` è uno stub. Da noi due strade: leggere i valori veri della vCPU tramite Hypervisor (emulando il `mrs` che va in trap nel vettore EL1), oppure ricostruirli dalle `sysctl hw.optional.arm.FEAT_*` come fa MetalSharp.
- ⬜ **[MS]** Correzioni a FEX da rifare se incontriamo lo stesso errore:
  - flag di eccezione x87 persi in FLDENV, FRSTOR, FXRSTOR e nella conversione di contesto WoW64 (`fetestexcept` a 32 bit leggeva 0);
  - `POP` verso la memoria: RSP aggiornato prima di una scrittura che va in fault, quindi l'istruzione non riparte pulita;
  - flag DF/AF persi nei contesti di eccezione ARM64EC;
  - terminazione di thread di altri processi.
- ⬜ **TSO hardware**: collegare `SetHardwareTSOControl` di FEX al bit `ACTLR_EL1` della vCPU (il prototipo ha già mostrato 0 riordinamenti con TSO attivo). È il nostro vantaggio su MetalSharp, che emula l'ordine della memoria in software.

## Fase 3: prestazioni e avvio

- ⬜ **[MS]** `dnsapi`: togliere la ricerca DNS multicast del nome `.local` del Mac all'avvio di ogni processo (5 secondi di timeout, pagati due volte a ogni avvio a freddo).
- ⬜ **[MS]** Suite di benchmark sul modello di `run-bench.sh` (int, fp, mem, atomic, order, jit, CPython), con checksum confrontati col nativo e le stesse colonne (ARM64 / x86-64 / i386), così il confronto con i loro numeri su M4 è diretto.
- ⬜ Misurare quanto guadagna davvero il TSO hardware. Con LRCPC su M4 l'emulazione software costa poco (il loro test "order" va a 0,94 volte il nativo): il guadagno va misurato, non dato per scontato.
- ⬜ Ridurre il costo delle uscite dalla VM (circa 0,7µs a syscall): gestire alcune syscall frequenti direttamente nella VM, senza uscire, e misurare l'avvio a freddo e a caldo.

## Fase 4: validazione

- ⬜ **[MS]** Test di conformità di Wine per tutte e tre le architetture, sul modello di `run-winetest.sh` (`ntdll`, `kernel32`, poi il resto), con un elenco delle cause di ogni fallimento.
- ⬜ **[MS]** Programmi reali come test: CPython (pacchetti embeddable x64 e x86), Java (Temurin x64 e i386), MSI (7-Zip), i VC++ redistributable.

## Fase 5: grafica, audio e input

- ⬜ **[MS]** Direct3D su Metal in versione ARM64X, così un'unica build serve sia ai programmi ARM64 sia a quelli x86-64 (ARM64EC). Componenti da valutare:
  - D3D10/11/12 con un fork di DXMT (Mullion);
  - D3D9/10/11 con DXVK su MoltenVK;
  - D3D12 con VKD3D-Proton.
- ⬜ **[MS]** OpenGL 4.6 su Metal (WineMetalGL) al posto del GL 4.1 di Apple.
- ⬜ **[MS]** MoltenVK con cache delle pipeline.
- ⬜ **[MS]** FFmpeg (VideoToolbox/AudioToolbox) per `winedmo` / Media Foundation.
- ⬜ **[MS]** Controller: winebus con SDL2 + IOHID (XInput e DirectInput).
- ⬜ **[MS]** Mouse raw con GCMouse e schermo intero nativo di macOS.
- ⬜ **[MS]** Font:
  - Liberation rinominati come Arial, Times New Roman e Courier New in `C:\windows\Fonts`;
  - famiglie CJK di Windows associate ai font di sistema di macOS.
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

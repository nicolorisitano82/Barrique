# msync in Barrique: studio di fattibilità

Data: 2026-11-17. Base: Wine 11.22 con le patch Barrique (`~/Work/Personale/wine/src`, `VERSION` = "Wine version 11.22").

## Sintesi

- **Che cos'è.** msync è una patch per Wine su macOS di Marc-Aurel Zent (CodeWeavers), con licenza LGPL 2.1. Le attese su eventi, mutex e semafori restano nel processo: lo stato sta in memoria condivisa, le attese su un solo oggetto usano `__ulock_wait2` e quelle su più oggetti un semaforo Mach registrato presso un thread di wineserver. CrossOver la include dalla 23.7. Secondo CodeWeavers la include ancora la 26, basata su Wine 11.0, al posto di esync.
- **Wine 11.22 oggi.** Wine ha già lo strato generico per la sincronizzazione nel processo ("inproc sync"), ma l'unico backend è il driver Linux `/dev/ntsync`. Su macOS ogni attesa e ogni segnalazione su un oggetto kernel passa ancora da wineserver. Le primitive leggere (critical section, SRW, `RtlWaitOnAddress`) invece usano già un futex macOS (`os_sync_wait_on_address`) senza passare dal server.
- **Nella VM di Barrique.** L'attesa gira sul lato host della syscall, dentro `hvf_dispatch_syscall`. Non aggiunge uscite dalla VM oltre a quella della syscall (circa 1,7–2,0 µs) ed elimina il giro con wineserver. Il percorso di sospensione e di segnali di Barrique (`hvf_in_guest`/`hvf_pending`) copre già un thread fermo dentro una syscall. WoW64 a 32 bit non richiede modifiche.
- **Guadagno atteso.** Il README di msync riporta, su un M2 Max, attese contese 45 volte più veloci del server e FFXIV da 93 a 219 FPS. In Barrique ogni chiamata paga comunque 1,7 µs di uscita dalla VM, quindi il guadagno relativo è un po' minore ma resta ampio: il giro con wineserver costa decine di µs sotto contesa.
- **Raccomandazione.** Conviene non applicare la patch di marzent del 2024, che è scritta per l'interfaccia di wineserver precedente a Wine 10/11. Conviene invece scrivere un **backend macOS dello strato inproc di Wine 11**: memoria condivisa e `os_sync_wait_on_address`, prendendo il meccanismo delle attese multiple da msync, che è LGPL. Prima di scrivere codice servono due passi:
  1. misurare in Barrique con un microbenchmark;
  2. leggere i sorgenti LGPL di CrossOver 26.3, dove CodeWeavers ha già portato msync su Wine 11. Scaricarli richiede l'approvazione dell'utente.

  Stima: 1–2 settimane per un prototipo con attese su un solo oggetto, 5–8 settimane per una versione completa e stabile.

---

## 1. Che cos'è msync

### Origine, autore, licenza, dove si trova

| Voce | Dato | Fonte |
|---|---|---|
| Repository | `github.com/marzent/wine-msync`, solo il branch `main` (46 commit) | https://github.com/marzent/wine-msync |
| Descrizione | "A Mach semaphore-based synchronization mechanism for Wine on macOS… inspired by fsync" | https://github.com/marzent/wine-msync |
| Autore | Marc-Aurel Zent (CodeWeavers). Le intestazioni di `msync.c` dicono "Copyright (C) 2018 Zebediah Figura" e "Copyright (C) 2023 Marc-Aurel Zent" | patch `msync-devel.patch`: https://raw.githubusercontent.com/marzent/wine-msync/main/msync-devel.patch |
| Licenza | LGPL 2.1 o successiva, compatibile con Wine. L'autore chiede di essere citato, ma non lo impone | README, https://github.com/marzent/wine-msync |
| File di patch | `msync-cx22.patch`, `msync-cx23.patch` (CrossOver), `msync-devel.patch` (Wine devel), `msync-staging.patch` (Wine Staging) | https://github.com/marzent/wine-msync |
| Versione bersaglio | `msync-devel.patch` è del 12 agosto 2024 e porta `SERVER_PROTOCOL_VERSION` da 837 a 841. Si tratta quindi di un Wine 9.x, precedente al rifacimento della sincronizzazione in Wine 10/11 | patch, intestazione e `protocol.def` |
| Diffusione | CrossOver 23.7.0 (27 novembre 2023): "MSync included". CrossOver 25.1.0: "Fix for Steam downloads with msync enabled" | https://codeweavers.com/crossover/changelog |
| CrossOver 26 | Basato su Wine 11.0. Secondo l'annuncio nel forum di CodeWeavers, che ho visto solo tramite i risultati di ricerca perché la pagina risponde 403: "Since CrossOver now includes NTSync support for Linux and MSync support for Mac, we no longer include ESync" | https://www.codeweavers.com/support/forums/announce?msg=345881&t=24 ; Wine 11.0: https://www.omgubuntu.co.uk/2026/02/crossover-26-released |
| Sorgenti CrossOver | `crossover-sources-26.3.0.tar.gz` (LGPL per la parte Wine) | https://www.codeweavers.com/crossover/source |
| Whisky | Interruttore "Enhanced Sync: msync" in Whisky 2.2.4 e successive. Il progetto è stato archiviato nel 2025 e i fork usano Wine 11 | https://github.com/Whisky-App/Whisky (dato visto nei risultati di ricerca) ; https://github.com/frankea/Whisky |
| Stato upstream | Non integrato in Wine. L'issue #9 di wine-msync ("Performance comparison with new NTSync (v7)?", aprile 2025) non ha risposta dell'autore | https://github.com/marzent/wine-msync/issues/9 |

### Come funziona

I dati che seguono vengono da `msync-devel.patch`: 52 file, +3428 righe. I file nuovi sono `dlls/ntdll/unix/msync.c` (1689 righe), `server/msync.c` (991), `msync.h` lato client e lato server.

**Memoria condivisa.**
- wineserver crea un oggetto POSIX shm chiamato `/wine-<inode della config dir>-msync` con `shm_open(O_CREAT|O_EXCL)`.
- La dimensione è `MAX_INDEX * 16`, cioè 0x100000 slot da 16 byte, 16 MiB in tutto. `ftruncate` si chiama una volta sola, perché XNU non permette di ridimensionare.
- Il client mappa le pagine solo quando servono (`get_shm(idx)`).
- Ogni slot contiene quattro `int`:
  - `[0]` e `[1]`: lo stato dell'oggetto. Per un semaforo `count, max`, per un evento `signaled`, per un mutex `tid, count`.
  - `[2]`: flag "in uso/distrutto".
  - `[3]`: contatore dei thread in attesa.
- Il server assegna gli slot con `msync_alloc_shm()`.

**Oggetti.**
- `enum msync_type` comprende `SEMAPHORE`, `AUTO_EVENT`, `MANUAL_EVENT`, `MUTEX`, `AUTO_SERVER`, `MANUAL_SERVER` e `QUEUE`.
- Gli oggetti `*_SERVER` coprono processi, thread e timer: lo stato lo scrive wineserver nello slot.
- `QUEUE` copre la coda dei messaggi, con la nuova richiesta `msync_msgwait`.
- Le APC usano un evento per thread (`get_msync_apc_idx`).

**Attese.**
- *Percorso veloce.* Il client prova a prendere l'oggetto direttamente nella memoria condivisa (decremento del semaforo, CAS sul mutex, CAS sull'evento automatico). Se non è disponibile, aspetta:
  - con un solo oggetto, chiama `__ulock_wait2(UL_COMPARE_AND_WAIT_SHARED | ULF_NO_ERRNO, addr, valore, timeout_ns)`, una funzione privata caricata con `dlsym`;
  - con più oggetti, prende un semaforo Mach da un pool lato client (fino a 1024, `SYNC_POLICY_FIFO`) e lo registra presso wineserver. Il messaggio Mach contiene il semaforo come port descriptor e gli indici degli slot, con `msgh_id = (tid << 8) | count`. Poi blocca in `semaphore_wait` o `semaphore_timedwait`.
- *Segnalazione.* Il client scrive lo slot, chiama `__ulock_wake(UL_COMPARE_AND_WAIT_SHARED | ULF_WAKE_ALL)` e, solo se il contatore dei thread in attesa non è zero, invia un messaggio Mach con `msgh_id = shm_idx`.
- *WaitAll.* Il client aspetta ogni oggetto, ricontrolla che siano tutti segnalati e poi li prende uno dopo l'altro. Se una presa fallisce, restituisce quelli già presi e ricomincia (`goto tooslow`). L'operazione non è atomica come in ntsync.

**Modifiche a wineserver.**
- Un pthread dedicato (`mach_message_pump`) riceve su una porta registrata con `bootstrap_register2`. La coda è limitata a 50 messaggi (`WINEMSYNC_QLIMIT`).
- Il thread gestisce tre messaggi:
  - *registrazione*: ricontrolla la condizione per evitare la corsa tra controllo e attesa, poi chiama `add_sem()`;
  - *rimozione*;
  - *segnalazione o distruzione*: esegue `semaphore_signal` su tutti i thread in attesa dello slot (`mach_semaphore_map[MAX_INDEX]`).
- Ogni `object_ops` riceve un nuovo campo `get_msync_idx`. Per questo la patch tocca circa 50 file `server/*.c`.
- Nuove richieste: `create_msync`, `open_msync`, `get_msync_idx`, `msync_msgwait`, `get_msync_apc_idx`.
- Si attiva con `WINEMSYNC=1`, che deve essere uguale per il server e per i client.

Fonti per tutta la sezione: la patch citata sopra e il README (https://github.com/marzent/wine-msync).

**Nota.** Il README dice che il codice di memoria condivisa viene da fsync e l'idea da esync, entrambi di Zebediah/Elizabeth Figura (https://github.com/marzent/wine-msync).

---

## 2. Stato di Wine upstream (11.x) per macOS

### Lo strato inproc esiste, ma solo per Linux

- **ntsync è il primo backend.** Wine 11.0 (gennaio 2026) usa il driver `ntsync` del kernel Linux quando c'è; altrimenti ricade sul server (https://linuxiac.com/wine-11-0-brings-fully-supported-wow64-mode/ ; https://www.theregister.com/2026/01/15/wine_11_arrives_faster_and/). La MR finale "ntdll: Use in-process synchronization objects" è la !9091 di Rémi Bernon, aperta il 3 ottobre 2025. Ho visto il titolo e l'autore nei risultati di ricerca: la pagina GitLab blocca i bot (https://gitlab.winehq.org/wine/wine/-/merge_requests/9091).
- **Server.** `server/inproc_sync.c` (Copyright 2021-2022 Elizabeth Figura for CodeWeavers, riga 4):
  - tutto il codice reale è dentro `#ifdef NTSYNC_IOC_EVENT_READ` (righe 36-40). Apre `/dev/ntsync` (righe 47-52) e crea eventi, mutex e semafori come fd ntsync (righe 83-157);
  - senza ntsync, cioè su macOS, ogni funzione è un segnaposto che restituisce -1 o NULL (righe 226-275), e `get_inproc_sync_fd` risponde `STATUS_NOT_IMPLEMENTED` (righe 277-290).
- **Ogni oggetto attendibile ha già un sync interno.**
  - Wine 11 ha aggiunto `get_sync` a `object_ops` (`server/object.h:106`, `server/object.c:647`).
  - Processi, thread, code, timer, fd, completion, console, debugger e device creano un "internal sync" con `create_internal_sync()`: `process.c:666`, `thread.c:517`, `queue.c:295`, `timer.c:186`, `fd.c:1696`, `completion.c:196`, `console.c:419`, `debugger.c:314`, `device.c:806`.
  - `create_internal_sync` diventa un oggetto inproc solo se c'è il device (`server/event.c:100-104`). Lo stesso vale per eventi (`event.c:80`), mutex (`mutex.c:146`) e semafori (`semaphore.c:122`).
  - In pratica Wine 11 ha già fatto in modo generico il lavoro che msync faceva aggiungendo `get_msync_idx` a 50 file.
- **Client.**
  - `dlls/ntdll/unix/sync.c`: l'unico backend è `linux_*` via ioctl ntsync (righe 63-64, 311-450). Senza ntsync, tutte le funzioni restituiscono `STATUS_NOT_IMPLEMENTED` (righe 452-500).
  - `inproc_wait()` (righe 899-924) e le funzioni `inproc_set_event`, `inproc_release_semaphore` ecc. (righe 778 e seguenti) escono subito se `inproc_device_fd < 0` (riga 78 e righe 766-906).
  - Così `NtWaitForMultipleObjects`, `NtWaitForSingleObject` e `NtSetEvent` ricadono su `server_wait()` o `SERVER_START_REQ(event_op)` (`sync.c:2329-2338`, `2355-2364`, `1160-1170`; `server.c:811-834`).
- **Protocollo.**
  - `protocol.def:4215-4237`: `enum inproc_sync_type` (INTERNAL, EVENT, MUTEX, SEMAPHORE), le richieste `get_inproc_sync_fd` e `get_inproc_alert_fd`, e il campo `inproc_device` in `init_first_thread` (`protocol.def:1215`).
  - Il device viene passato al client come fd all'avvio (`server/thread.c:1762-1766` → `dlls/ntdll/unix/server.c:1714-1718`).
- **Cache degli handle lato client.** `struct inproc_sync` contiene `refcount`, `fd`, `access`, `type` e `closed` (`sync.c:536-543`). La cache è senza lock (righe 551-660) e chiede l'fd al server una volta per handle (righe 667-689). È indipendente da Linux e si può riusare.

### Cosa esiste già per macOS

- **Futex macOS per le primitive leggere.** `sync.c:137-196` usa `os_sync_wait_on_address(_with_timeout)` (API pubblica da macOS 14.4) e ricade su `__ulock_wait`/`__ulock_wake` sui sistemi più vecchi. Lo usa `NtWaitForAlertByThreadId` (righe 3585-3621), cioè la base di `RtlWaitOnAddress`, delle critical section, di SRW e delle condition variable. Queste quindi **non** passano da wineserver.
  - Il flag è `OS_SYNC_WAIT_ON_ADDRESS_NONE`, valido solo dentro un processo. L'header dell'SDK documenta `OS_SYNC_WAIT_ON_ADDRESS_SHARED` "when synchronizing among multiple processes by placing the @addr … in a shared memory region" (`/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/os/os_sync_wait_on_address.h:48-64`, disponibilità `macos(14.4)` alla riga 39). Si veda anche https://developer.apple.com/documentation/os/os_sync_wait_on_address.
- **Porta Mach di wineserver.** `server/mach.c:100-116` registra già una porta Mach con `bootstrap_register2`. Un thread di wineserver che riceve messaggi Mach, come fa msync, non introduce un meccanismo nuovo.
- **Memoria condivisa di sessione** (`protocol.def:1006-1130`, `session_shm_t`). Serve a finestre, code e input di win32u, non alla sincronizzazione.

**Conclusione.** Su macOS con Wine 11.22, eventi, mutex, semafori, processi, thread, timer e code passano tutti da wineserver: una richiesta per ogni `SetEvent` e un `server_select` per ogni attesa. Lo strato inproc però offre il punto di aggancio per un backend macOS:
- lato server, `create_inproc_*`, `signal_inproc_sync`, `reset_inproc_sync`, `abandon_inproc_mutexes` e `get_obj_inproc_sync`;
- lato client, le funzioni `linux_*` e `linux_wait_objs`.

---

## 3. Interazione con la VM di Barrique

**Dove gira l'attesa.**
- Ogni syscall Windows è un'uscita dalla VM (`HVF_EXIT_SYSCALL`). `hvf_loop()` salva il frame, mette `hvf_in_guest = FALSE` e chiama `hvf_dispatch_syscall()` sul thread host (`signal_arm64.c:2422-2466`).
- `NtWaitForSingleObject` e le altre funzioni di sincronizzazione girano lì. Con un backend inproc l'operazione diventa qualche atomico sulla memoria condivisa, più eventualmente `os_sync_wait_on_address` o `semaphore_wait` sul thread host.
- Non serve nessuna uscita in più: quella della syscall avviene comunque. Sparisce invece il giro sulla pipe verso wineserver e il cambio di contesto del server.

**vCPU mentre il thread è bloccato.**
- `is_wait_syscall()` riconosce le syscall di attesa (`syscall.c:253-264`). Sopra la soglia (`vcpu_release_threshold`, metà del massimo dei vCPU, `hvf.c:134-137` e `hvf.c:555-557`), `hvf_dispatch_syscall` rilascia il vCPU prima di bloccare (`signal_arm64.c:2266-2270`, `hvf.c:737-750`).
- Questo meccanismo è indipendente dal backend e resta valido. Con msync le attese brevi e non contese tornano subito, quindi il rilascio del vCPU, che costa leggere i registri FP con `hvf_sync_fp`, avviene meno spesso o inutilmente. Conviene rilasciare il vCPU solo quando l'attesa blocca davvero: per esempio spostando il rilascio dentro il backend, subito prima di `os_sync_wait_on_address`. È un'ottimizzazione, non un requisito.

**Sospensione, segnali, terminazione.**
- Mentre il thread host è dentro la syscall, il frame è valido. `usr1_handler` (SIGUSR1, sospensione) vede `hvf_in_guest == FALSE`, quindi non rimanda il segnale, e chiama `wait_suspend()` direttamente (`signal_arm64.c:1579-1615`, `1135-1170`).
- È lo stesso caso di oggi, con il thread bloccato in `server_select`. Il backend deve solo:
  - ripetere l'attesa quando `os_sync_wait_on_address` o `semaphore_wait` vengono interrotti da un segnale (`EINTR`, `KERN_ABORTED`), ricalcolando il timeout assoluto. Il backend ntsync fa lo stesso: `do { ioctl } while (errno == EINTR)`, `sync.c:431-432`;
  - non tenere lock tra un'attesa e il punto in cui può arrivare un segnale. `fd_cache_mutex` si prende con `server_enter_uninterrupted_section`, come per ntsync (`sync.c:877-889`).
- `hvf_kick()` (`hvf.c:825-830`) serve solo quando il thread è nel guest. Un thread bloccato in un'attesa inproc non è nel guest.

**APC e attese alertable.**
- Con ntsync il server segnala un "alert" interno per thread (`server/thread.c:518`, `1453`, `1488`, `1504`, `2394-2398`). Il client lo aggiunge alla lista degli oggetti attesi e poi chiede le APC con un `server_wait` a timeout zero (`sync.c:436-443`).
- Un backend macOS può fare lo stesso: l'alert diventa uno slot di tipo INTERNAL in memoria condivisa, scritto da wineserver, che sveglia i thread in attesa con `os_sync_wake_by_address_all(…SHARED)`. Non serve niente di specifico per la VM.

**Callback utente durante un'attesa.**
- Un'attesa può invocare `KeUserModeCallback` solo tramite le APC. Le APC passano già da `server_wait` → `invoke_user_apc`, quindi resta il percorso attuale, già testato in Barrique.

**WoW64 a 32 bit (i386 con FEX).**
- Il codice x86 chiama i thunk di `wow64.dll` dentro il guest (`dlls/wow64/sync.c:1405`, `1806`, `1825`), che convertono gli argomenti e fanno **una** syscall a 64 bit. Questa segue lo stesso percorso host: costa 1,99 µs per syscall i386 contro 1,76 su ARM64 (`Barrique/ROADMAP.md:90`).
- Gli oggetti e la memoria condivisa vivono solo sull'host, quindi WoW64 non richiede modifiche. Lo stesso vale per ARM64EC/x86-64.

**Memoria condivisa e VM.**
- La memoria condivisa di msync/inproc la mappa solo il lato host (ntdll unix e wineserver). Il guest non la vede. Non serve mapparla nello stage-2: niente `hv_vm_map` e niente impatto sulla finestra sotto i 4GB.
- Possibilità futura (fase 7 della ROADMAP, `ROADMAP.md:92`): mappare gli slot in sola lettura nel guest, o leggerli e scriverli da un gestore EL1. Così le operazioni non contese (`WaitForSingleObject` su un evento già segnalato, `SetEvent` senza thread in attesa) si farebbero senza uscire dalla VM: circa 22 ns per una syscall EL1 nel guest contro 720 ns per un'uscita nel prototipo hvproto (memoria di progetto). Richiede una ntdll PE con un percorso veloce e ha implicazioni di sicurezza, perché il codice Windows potrebbe corrompere la sincronizzazione di altri processi. Da valutare dopo.

---

## 4. Guadagno atteso

### Numeri pubblicati

**msync** (README, M2 Max, build basata su CX23; https://github.com/marzent/wine-msync):

| Prova | Server (Wine attuale su macOS) | esync | msync | msync + ulock |
|---|---|---|---|---|
| Attesa contesa, 10M iterazioni, 2 thread | > 170 s (≥ 17 µs/iter) | 7,42 s | 5,89 s | **3,79 s** (0,38 µs/iter) |
| Zigzag, 2 s, 2 thread (iterazioni) | 60.309 (~33 µs/iter) | 222.675 | 270.545 | **401.605** (~5 µs/iter) |
| FFXIV, interno, limitato dalla CPU | 93 FPS | 145 | 170 | **219** (2,35× il server) |

**ntsync contro il solo server su Linux** (cover letter della patch kernel: https://lore.kernel.org/lkml/20240124004028.16826-1-zfigura@codeweavers.com/ ; tabella riportata da https://gamingonlinux.com/2024/01/proposed-windows-nt-driver-for-linux-brings-big-wine-proton-performance-improvements):

| Gioco | Server | ntsync |
|---|---|---|
| Dirt 3 | 110,6 | 860,7 |
| Resident Evil 2 | 26 | 77 |
| Call of Juarez | 99,8 | 224,1 |
| Tiny Tina's Wonderlands | 130 | 360 |
| Forza Horizon 5 | 108 | 160 |
| Metro 2033 | 164,4 | 199,2 |

Contro fsync/esync il vantaggio di ntsync è piccolo, a parte la correttezza: Valve parla di stabilità più che di velocità (https://gamingonlinux.com/2024/01/proposed-windows-nt-driver-for-linux-brings-big-wine-proton-performance-improvements ; https://korben.info/en/ntsync-wine-11-linux-gaming-fps-boost.html). Quello che conta è uscire dal server, non il meccanismo scelto.

### Riportati a Barrique

Costi misurati in Barrique:
- una syscall, cioè un'uscita dalla VM, costa 1,76 µs su ARM64, 1,85 su x86-64 e 1,99 su i386 (`ROADMAP.md:90`). Un milione di syscall richiede 1917–2050 ms contro 104–114 nativi (`ROADMAP.md:83`).

Stima, da verificare con il microbenchmark del passo 0:

| Operazione | Oggi (server) | Con backend inproc |
|---|---|---|
| `SetEvent` senza thread in attesa | 1,8 µs (uscita) + giro con il server (stimato 5–15 µs su macOS, non misurato in Barrique) | ~1,8 µs (uscita) + ~50 ns di atomici |
| `WaitForSingleObject` su un oggetto già segnalato | come sopra | ~1,8 µs |
| Scambio tra 2 thread (zigzag) | ~33 µs/iter (dato msync) + 4 × 1,8 µs ≈ 40 µs | ~5 µs/iter + 4 × 1,8 µs ≈ 12 µs |

- **Stima.** Le operazioni di sincronizzazione su oggetti kernel costerebbero circa 3–8 volte meno, invece delle 6–9 volte di msync su Wine nativo: l'uscita dalla VM resta e diventa il costo principale.
- **Programmi.** Il guadagno conta per quelli che usano molto eventi e semafori: giochi con thread di rendering e lavoro (FFXIV: da 93 a 219 FPS senza VM), DXMT con i thread di presentazione, audio, Steam, Unity. Non conta per i programmi che usano solo critical section e SRW, che non toccano già il server, né per i carichi limitati da I/O o GPU.
- **wineserver.** Il carico sul server, che ha un solo thread, cala molto: un vantaggio per i processi multipli (Steam con i suoi helper, launcher).
- **Uscita dalla VM.** Diventa il costo dominante. Questo rafforza la fase 7: un percorso veloce nel guest per gli oggetti non contesi (sezione 3).

---

## 5. Lavoro, rischi, strada consigliata

### Strade possibili

| Strada | Descrizione | Stima | Pro | Contro |
|---|---|---|---|---|
| **A. Patch marzent 2024** | Applicare `msync-devel.patch` su 11.22 | 4–6 settimane | Codice già usato in produzione | Scritta per protocollo 837 e Wine 9.x. Tocca circa 50 `object_ops` con `get_msync_idx`, mentre Wine 11 ha `get_sync`/`create_internal_sync`. Non si applica: in pratica va riscritta. Duplica lo strato inproc di Wine 11 |
| **B. msync di CrossOver 26.3** | Leggere `crossover-sources-26.3.0.tar.gz` (Wine 11.0 + msync, LGPL) e portare la differenza da 11.0 a 11.22 | 1–3 settimane per la porta, più i test HVF | CodeWeavers ha già fatto il lavoro su Wine 11 e lo mantiene | Richiede di scaricare circa centinaia di MB (serve l'approvazione dell'utente). Non so se si appoggia allo strato inproc o gli corre in parallelo |
| **C. Backend macOS dello strato inproc** | Implementare le funzioni server di `inproc_sync.c` e le `linux_*` di `sync.c` con memoria condivisa + `os_sync_wait_on_address(SHARED)`. Per WaitAny/WaitAll su più oggetti, riprendere l'idea di msync: semaforo Mach registrato presso un thread di wineserver | 1–2 settimane per il prototipo con un oggetto, 5–8 per la versione completa | Patch piccola e isolata (2 file principali + protocollo). Riusa la cache degli handle, l'alert per le APC e gli internal sync di tutti gli oggetti già presenti. API pubblica (macOS 14.4+, Barrique richiede 26+), niente `__ulock_wait2` privato | Da scrivere e collaudare. WaitAll multiprocesso non atomico, come in msync |

### Rischi tecnici

1. **Durata degli oggetti.** Con ntsync il client tiene un fd, quindi l'oggetto sopravvive alla chiusura dell'handle mentre qualcuno aspetta (`sync.c:503-534`). Con gli slot in memoria condivisa, il server non deve riusare uno slot mentre un client ci aspetta. Serve un contatore di generazione nello slot, oppure un riferimento lato client restituito al server alla chiusura, come `close_inproc_sync` (`sync.c:762-775`). msync usa il flag "distrutto" più la sveglia di tutti i thread in attesa.
2. **Semantica NT.** Sono tutti punti noti in esync/msync, e sono il motivo per cui è nato ntsync:
   - WaitAll non atomico tra processi;
   - `PulseEvent`;
   - mutex abbandonati alla morte di un thread (`abandon_inproc_mutexes`, `server/inproc_sync.c:201-207`);
   - attese su un handle chiuso.

   Conviene tenere i test `ntdll/sync` e `kernel32/sync` come criterio di accettazione.
3. **Segnali e timeout.** Dopo `EINTR` o `KERN_ABORTED` bisogna ricalcolare il timeout. Una sospensione durante un'attesa deve lasciare lo stato coerente (sezione 3).
4. **Regressioni viste in msync.** Download di Steam bloccati con msync (corretti in CrossOver 25.1: https://codeweavers.com/crossover/changelog). Code dei messaggi e "hung app" (`msync_msgwait`). Coda Mach piena (`WINEMSYNC_QLIMIT`).
5. **Thread aggiuntivo in wineserver.** Il message pump di msync è un pthread dentro un server che finora ha un solo thread. Deve toccare solo la memoria condivisa e le sue strutture, mai lo stato del server.
6. **API private.** `__ulock_wait2` e `bootstrap_register2` sono private. La prima si evita con `os_sync_wait_on_address`, pubblica. La seconda Wine la usa già (`server/mach.c:100-116`). La `.app` non è in sandbox: l'entitlement è solo `com.apple.security.hypervisor`, `Barrique/entitlements.plist:5`.
7. **Licenze.**
   - msync è LGPL 2.1+ e CrossOver/Wine è LGPL: si possono riusare mantenendo le intestazioni di copyright.
   - Non guardare né copiare il codice di sincronizzazione di MetalSharp-Wine (AGPL).
   - Le restrizioni sul codice scritto con l'AI riguardano FEX e DXMT, che qui non vengono toccati. Se un giorno si volesse proporre il backend a Wine upstream, bisognerà prima verificare la politica di Wine sui contributi.
8. **Rapporto costo/beneficio.** Se il microbenchmark mostra che il giro con wineserver su macOS costa poco rispetto a 1,8 µs, il guadagno si riduce. Per questo il passo 0 viene prima di tutto.

### Strada consigliata e primi passi

Strada **C**, usando **B** come riferimento di lettura.

0. **Misurare (1–2 giorni).**
   - Aggiungere a `tests/pe/bench.c` tre prove, confrontando i quattro casi: nativo, ARM64, x86-64, i386.
     - (a) `SetEvent`/`ResetEvent` non contesi;
     - (b) ping-pong a 2 thread con due eventi automatici, come lo zigzag di msync;
     - (c) `WaitForMultipleObjects` su 4 oggetti.
   - Aggiungere a `WINE_HVF_STATS` il conteggio delle syscall di sincronizzazione, e dei `server_select`, nei programmi reali di `tests/apps`. Così si capisce quanto pesano davvero.
1. **Leggere CrossOver 26.3 (2–3 giorni, dopo l'approvazione a scaricare il tarball).** Fare il diff di `dlls/ntdll/unix/sync.c`, `server/inproc_sync.c` ed eventuali `msync.c` contro Wine 11.0, per capire come CodeWeavers ha integrato msync con lo strato inproc.
2. **Prototipo con un solo oggetto (1–2 settimane).**
   - Il server crea la memoria condivisa e la passa come "inproc device" nella risposta di `init_first_thread`: c'è già il campo, `server/thread.c:1762-1766`.
   - `get_inproc_sync_fd` restituisce un indice di slot invece di un fd.
   - Lato client, le funzioni `mac_*` gemelle delle `linux_*` usano atomici + `os_sync_wait_on_address(SHARED)`.
   - Le attese su più oggetti restano sul server, che a quel punto deve diventare la fonte autorevole dello stato leggendo gli slot.
   - Attivazione con una variabile d'ambiente (per esempio `WINEMSYNC=1`), spenta per default.
3. **Attese multiple e alert (2–3 settimane).** Registrazione di un semaforo Mach presso un thread di wineserver come in msync, ricontrollo della condizione al momento della registrazione, alert per le APC, mutex abbandonati.
4. **Collaudo in Barrique (1–2 settimane).**
   - Test di conformità `ntdll` `sync`/`om` e `kernel32` `sync`/`thread` sulle tre architetture.
   - Sospensione durante un'attesa (`kernel32` `thread`, debugger).
   - Programmi reali di `tests/apps`, Steam, un gioco DXMT.
   - Prove con molti thread oltre la soglia dei vCPU.
   - Se tutto passa, accenderlo per default nella `.app`.

**Stima totale: 5–8 settimane** per una versione stabile e accesa per default. Si può fermare dopo il passo 2 se il guadagno misurato è basso.

---

## Fonti

- msync: https://github.com/marzent/wine-msync ; patch https://raw.githubusercontent.com/marzent/wine-msync/main/msync-devel.patch ; issue #9 https://github.com/marzent/wine-msync/issues/9
- CrossOver changelog: https://codeweavers.com/crossover/changelog ; sorgenti https://www.codeweavers.com/crossover/source ; annuncio CX26 (visto nei risultati di ricerca, la pagina risponde 403) https://www.codeweavers.com/support/forums/announce?msg=345881&t=24 ; CX26 = Wine 11.0 https://www.omgubuntu.co.uk/2026/02/crossover-26-released
- Wine 11.0 e NTSync: https://linuxiac.com/wine-11-0-brings-fully-supported-wow64-mode/ ; https://www.theregister.com/2026/01/15/wine_11_arrives_faster_and/ ; MR !9091 https://gitlab.winehq.org/wine/wine/-/merge_requests/9091
- Benchmark ntsync: https://lore.kernel.org/lkml/20240124004028.16826-1-zfigura@codeweavers.com/ ; https://gamingonlinux.com/2024/01/proposed-windows-nt-driver-for-linux-brings-big-wine-proton-performance-improvements
- API Apple: https://developer.apple.com/documentation/os/os_sync_wait_on_address ; header SDK `os/os_sync_wait_on_address.h`
- Whisky: https://github.com/Whisky-App/Whisky ; https://github.com/frankea/Whisky
- Codice locale: `~/Work/Personale/wine/src/server/inproc_sync.c`, `server/event.c`, `server/thread.c`, `server/object.h`, `server/protocol.def`, `server/mach.c`, `dlls/ntdll/unix/sync.c`, `dlls/ntdll/unix/server.c`, `dlls/ntdll/unix/syscall.c`, `dlls/ntdll/unix/signal_arm64.c`, `dlls/ntdll/unix/hvf.c`, `dlls/wow64/sync.c`; `~/Work/Personale/wine/Barrique/ROADMAP.md`

## Port fatto (2026-11-18/19)

Invece del backend scritto da zero è stato portato il msync di **CrossOver 26.3.0** (sorgenti LGPL, base Wine 11.0): `server/msync.{c,h}` e `dlls/ntdll/unix/msync.{c,h}` con le intestazioni di copyright originali, agganciati allo strato `inproc_sync` di Wine 11.22 (ramo `__APPLE__` accanto a quello ntsync di Linux in `dlls/ntdll/unix/sync.c` e `server/inproc_sync.c`; campo `shm_idx` nella risposta di `get_inproc_sync_fd`). Spento di default, `WINEMSYNC=1` lo accende.

| `syncbench`, µs per operazione | senza | `WINEMSYNC=1` |
|---|---|---|
| mutex wait+release | 27,5-28,2 | 3,6-4,0 |
| semaphore release+wait | 27,1-28,0 | 3,6-4,0 |
| SetEvent + WaitForMultipleObjects(4) | 27,8-28,1 | 3,6-4,0 |
| WaitForSingleObject, timeout 0 | 20,0 | 1,5-1,7 |
| ping-pong di eventi | 31-32 | 13-16 |

Uguale su ARM64, x64 e x86. I circa 3,5 µs rimasti sono l'uscita dalla VM a ogni syscall, non più il viaggio al wineserver.

Correzioni rispetto a CrossOver, trovate con i test di conformità:
- `NtReleaseMutant`: msync restituisce il numero di acquisizioni, Wine 11 vuole il conteggio precedente del mutante (`1 - count`, come il ramo ntsync);
- mutex abbandonato dopo la chiusura dell'ultimo handle mentre un altro thread aspetta (kernel32 `sync.c:416`): il server distruggeva l'oggetto msync e lo toglieva da `mutex_list`, quindi alla morte del proprietario nessuno svegliava chi aspettava. Ora un mutex ancora posseduto e ancora usato resta in `orphan_mutex_list` finché il proprietario lo rilascia o termina.

Dopo le correzioni `ntdll` sync/om e `kernel32` sync/thread danno gli stessi risultati con e senza msync sulle tre architetture. Da fare prima di accenderlo di default: un giro con programmi veri; poi valutare `os_sync_wait_on_address` al posto delle API private `__ulock_wait`/`__ulock_wait2` (`make-dist.sh` ammette `___ulock_wait2` debole, esiste da macOS 11).

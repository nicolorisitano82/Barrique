# Studio: driver kernel Windows (.sys) per periferiche USB in Barrique (2026-11-20)

Domanda: cosa serve perché un driver `.sys` per periferiche USB (scanner, stampanti, aggeggi con solo un driver Windows) funzioni in Barrique, cioè Wine 11.22 + HVF + FEX (ARM64EC per x64, WoW64 per i386).

Riferimenti `file:riga` nell'albero `~/Work/Personale/wine/src` (Wine 11.22, `git diff` = patch di Barrique; nessuna patch di Barrique tocca `ntoskrnl.exe`, `wineusb.sys`, `winedevice`, `services`). La storia git locale è un solo commit (`9f8c4ae Release 11.22`), quindi niente cronologia upstream da consultare.

## In breve

- **Wine sa già caricare ed eseguire driver WDM** in un processo utente (`winedevice.exe`): IRP, PnP di base, interfacce, `\\.\` dai programmi. Funziona per driver "semplici" (anti-cheat leggeri, driver software, HID virtuali, il bus USB di Wine).
- **Oggi in Barrique un `.sys` x64 non si carica affatto**: `services.exe` avvia `winedevice.exe` come processo ARM64 nativo, e un'immagine AMD64 non è valida in un processo ARM64 (serve un processo ARM64EC). Anche il riconoscimento dell'INF (`.NTamd64`) dipende dall'architettura del processo. È la prima cosa da sistemare, ed è piccola (1-2 giorni).
- **Lo stack USB di Wine è minimo**: `wineusb.sys` gestisce solo `IOCTL_INTERNAL_USB_SUBMIT_URB` con 7 funzioni URB (niente isocrono, niente `SELECT_INTERFACE`, niente `CONTROL_TRANSFER`, niente `QUERY_INTERFACE`), e **su macOS non reclama le interfacce**, quindi bulk/interrupt falliscono con libusb darwin. `winusb.dll` è tutta stub, `winusb.sys` e `usbscan.sys` non esistono, **KMDF (`wdfldr.sys`/`wdf01000.sys`) e UMDF non esistono**.
- **La strada che rende di più non è eseguire `.sys` di terzi**: quasi tutti gli scanner USB usano `usbscan.sys` (driver Microsoft) + DLL utente del produttore (TWAIN/WIA), e molte periferiche moderne usano WinUSB. Emulare `usbscan.sys` e WinUSB sopra `wineusb.sys` sblocca più dispositivi di qualsiasi lavoro su KMDF, e tutto il codice del produttore resta in modalità utente.
- **Stampanti**: i driver dei produttori (v3 GDI/UNIDRV, v4 XPS) non sono ospitabili in Wine; la strada giusta è CUPS (già presente) + `wineps.drv`.
- **Impossibile**: PCI/MMIO/IRQ/DMA, filtri di classe/storage/rete, anti-cheat kernel, tutto ciò che vuole memoria fisica o altri processi.

## 1. Come Wine esegue i driver kernel oggi

### Caricamento

| Passo | Dove |
|---|---|
| Servizio `SERVICE_KERNEL_DRIVER` → `services.exe` non lancia il `.sys` ma `winedevice.exe`, un processo per gruppo di caricamento | `programs/services/services.c:980-1020`, percorso `:854-874`, riuso processo `:876-900` |
| 32/64 bit: solo `SCS_32BIT_BINARY` → winedevice WoW64; **nessuna distinzione ARM64/AMD64** | `services.c:861-862` |
| `CreateProcessW` senza `PROC_THREAD_ATTRIBUTE_MACHINE_TYPE` | `services.c:1090` |
| `winedevice.exe` = servizio che chiama `wine_ntoskrnl_main_loop` | `programs/winedevice/device.c:130-158` |
| `ZwLoadDriver` → `IoCreateDriver(init_driver)` → `wine_enumerate_root_devices` | `dlls/ntoskrnl.exe/ntoskrnl.c:4487-4535` |
| `load_driver`: legge `ImagePath`, `LoadLibraryExW` del `.sys` (immagine nativa, import di `ntoskrnl.exe`/`hal.dll` risolti come DLL) | `ntoskrnl.c:4326-4424` |
| `init_driver`: chiama `DriverEntry` | `ntoskrnl.c:4427-4475` |
| Rilocazione a mano dei `.sys` non allineati a pagina, cookie di sicurezza | `ntoskrnl.c:4247-4311` |
| Gestore vettoriale che emula istruzioni privilegiate (x86/x64) | registrato in `ntoskrnl.c:5154-5156`, codice `instr.c:459-947` |

Il driver gira **in modalità utente**, nel processo `winedevice.exe`, con `PsGetCurrentProcess()` = winedevice e non il chiamante.

### Dai programmi al driver

`CreateFile("\\.\Nome")` → link simbolico (`IoCreateSymbolicLink`, `ntoskrnl.c:1821`) → oggetto device nel wineserver → le richieste diventano "IRP" in coda (`server/device.c:559` `queue_irp`, `:618` read, `:652` ioctl, `:916` `get_next_device_request`) → `wine_ntoskrnl_main_loop` (`ntoskrnl.c:994-1110`) le prende e le spedisce (`dispatch_funcs`, `ntoskrnl.c:927`): create/close/read/write/ioctl/flush/set_info/volume/cancel. `dispatch_ioctl` (`ntoskrnl.c:780-843`) copia i buffer nel processo del driver: METHOD_BUFFERED e DIRECT funzionano; METHOD_NEITHER funziona solo con buffer piatti (puntatori annidati nella memoria del chiamante non valgono: processi diversi).

### PnP e alimentazione

| Funzione | Stato | Dove |
|---|---|---|
| Enumerazione bus (`IRP_MN_QUERY_DEVICE_RELATIONS`), ID, testo, capacità | ok | `pnp.c:73-222`, `:737` |
| Scelta del driver: `SetupDiBuildDriverInfoList` sugli INF in `C:\windows\inf` + class installer | ok | `pnp.c:284-347` |
| `AddDevice` + `IRP_MN_START_DEVICE` | ok, **solo il driver funzione**: `UpperFilters`/`LowerFilters` ignorati | `pnp.c:224-272`, `:389-408` |
| Il driver funzione viene caricato **nello stesso processo del driver di bus** (`ZwLoadDriver` in-process) | importante per l'architettura, vedi §5 | `pnp.c:244` |
| Rimozione (`SURPRISE_REMOVAL`, `REMOVE_DEVICE`) | ok | `pnp.c:711-724` |
| Mai inviati: `QUERY_RESOURCE_REQUIREMENTS`, `QUERY_PNP_DEVICE_STATE`, `QUERY_BUS_INFORMATION`, `QUERY_REMOVE/STOP`, IRP di sistema S0 | assenti | |
| `IoRegisterDeviceInterface`, `IoSetDeviceInterfaceState` (+ `WM_DEVICECHANGE`), proprietà, `IoOpenDeviceRegistryKey`, `IoGetDeviceProperty` | ok | `pnp.c:1460`, `:1299`, `:1601`, `:954` |
| `IoGetDeviceInterfaces` | stub `NOT_IMPLEMENTED` | `ntoskrnl.c:1891` |
| `IoRegisterPlugPlayNotification` | stub che non notifica mai | `ntoskrnl.c:3964` |
| `PoRequestPowerIrp` | **stub `NOT_IMPLEMENTED`** | `pnp.c:1636` |
| `PoSetPowerState`, `PoStartNextPowerIrp` | stub innocui | `pnp.c:1647`, `:1656` |
| `PoCallDriver` | = `IoCallDriver` | `pnp.c:1664` |

### Copertura delle API (`ntoskrnl.exe.spec`, 1720 voci, 726 `stub`)

| Prefisso | voci | stub | Note |
|---|---|---|---|
| Io | 205 | 118 | IRP, device, MDL, work item, remove lock ok |
| Ke | 161 | 71 | eventi, mutex, semafori, spinlock, timer (con DPC via threadpool, `sync.c:525`) ok |
| Mm | 77 | 50 | `MmProbeAndLockPages` è solo un flag (`ntoskrnl.c:3284`); `MmMapLockedPagesSpecifyCache` ok |
| Ex | 87 | 43 | pool, lookaside, fast mutex, resource ok |
| Ob / Ps | 29 / 92 | 18 / 50 | riferimenti, thread di sistema ok |
| Rtl / Zw | 345 / 148 | 59 / 10 | quasi tutto da ntdll |
| Po | 14 | 10 | vedi sopra |
| Se, Fs, Cc, Wmi, Hal (in ntoskrnl), Inbv | | quasi tutto stub | file system e sicurezza kernel assenti |

Mancanze che colpiscono un driver USB tipico (verificato sullo spec):

| API | Stato |
|---|---|
| `KeInsertQueueDpc` | **stub che non esegue mai il DPC** (`ntoskrnl.c:2841`) |
| `PoRequestPowerIrp` | `NOT_IMPLEMENTED`: KMDF, se padrone della policy di alimentazione, ci passa all'avvio |
| `IoSetCompletionRoutineEx`, `IoAttachDeviceToDeviceStackSafe`, `IoBuildPartialMdl`, `IoCsqInsertIrp`, `IoStartPacket`, `IoValidateDeviceIoControlAccess`, `IoOpenDeviceInterfaceRegistryKey`, `IoInvalidateDeviceState`, `KeRemoveQueueDpc`, `KeSaveFloatingPointState`, `ExQueueWorkItem`, `ExInitializeRundownProtection`, `ExAcquireRundownProtection`, `WmiTraceMessage` | stub |
| `IoQueueWorkItemEx`, `ExInitializeLookasideListEx`, `ExAllocatePool3`, `ExAllocateTimer`, `KeQuerySystemTimePrecise`, `EtwRegister`/`EtwWrite` (presenti solo `EtwRegisterClassicProvider`, `EtwUnregister`) | assenti |
| `hal.dll`: `KeStallExecutionProcessor` | **stub** (`dlls/hal/hal.spec:80`), usato da moltissimi driver x64 |

## 2. Lo stack USB

### `wineusb.sys` (bus USB di Wine su libusb)

- Installato come dispositivo `root\wineusb` (`programs/wineboot/wineboot.c:1598`, `dlls/wineusb.sys/wineusb.inf`), gruppo `WinePlugPlay`. In Barrique libusb 1.0.30 è compilato da noi (`Barrique/scripts/build-libusb.sh`); l'abilitazione è in corso da parte di altri.
- Crea un PDO per dispositivo e, per i composti, uno per interfaccia (sostituto di `usbccgp`, senza IAD: `unixlib.c:170-200`); ID `USB\VID_xxxx&PID_xxxx[&REV][&MI_xx]` e `USB\Class_..` (`wineusb.c:360-405`).
- PnP del PDO (`wineusb.c:457-516`): `QUERY_ID`, `QUERY_CAPABILITIES` (solo `RawDeviceOK`), `START`, rimozione. **Nessun `IRP_MN_QUERY_INTERFACE`** (quindi niente `USB_BUS_INTERFACE_USBDI_GUID`: `USBD_CreateHandle`/`USBD_QueryUsbCapability` di `usbdex.lib` non lo trovano), niente `QUERY_DEVICE_TEXT`.
- IOCTL interni: **solo `IOCTL_INTERNAL_USB_SUBMIT_URB`** (`wineusb.c:656`). Mancano `GET_PORT_STATUS`, `RESET_PORT`, `CYCLE_PORT`, `SUBMIT_IDLE_NOTIFICATION`, `GET_HUB_NAME`, `GET_TOPOLOGY_ADDRESS`…

URB gestiti (`wineusb.c:537-627`, `unixlib.c:427-597`):

| URB | Stato |
|---|---|
| `BULK_OR_INTERRUPT_TRANSFER` | ok (asincrono) |
| `GET_DESCRIPTOR_FROM_DEVICE` | ok |
| `VENDOR_DEVICE/INTERFACE/ENDPOINT` | ok (flag oltre la direzione ignorati, `unixlib.c:566`) |
| `SELECT_CONFIGURATION` | finta: crea gli handle delle pipe, non chiama `libusb_set_configuration` (`unixlib.c:523-539`) |
| `SYNC_RESET_PIPE_AND_CLEAR_STALL`, `ABORT_PIPE` | ok |
| `CLASS_*`, `CONTROL_TRANSFER(_EX)`, `SELECT_INTERFACE`, `GET_CONFIGURATION`, `GET_INTERFACE`, `GET_STATUS_*`, `SET/CLEAR_FEATURE_*`, `GET_DESCRIPTOR_FROM_INTERFACE/ENDPOINT`, `GET_CURRENT_FRAME_NUMBER` | **assenti** |
| `ISOCH_TRANSFER` | **assente** (webcam, audio USB, alcuni scanner/acquisitori: impossibili oggi) |
| alternate setting | sempre la prima (`unixlib.c:159`, `:190`) |

**Problema specifico macOS** (corretto il 2026-11-20: `wineusb` ora reclama le interfacce su `SELECT_CONFIGURATION`, vedi ROADMAP, Dispositivi): `wineusb` apriva il dispositivo (`unixlib.c:124`) ma non chiamava mai `libusb_claim_interface`. Con il backend darwin le pipe esistono solo per le interfacce reclamate (`deps-src/libusb-1.0.30/libusb/os/darwin_usb.c:468-508`, `LIBUSB_ERROR_NOT_FOUND`), quindi **ogni bulk/interrupt fallisce**. Da segnalare a chi abilita USB: reclamare l'interfaccia nel `START_DEVICE` del PDO (o al primo trasferimento).

### Altri pezzi

| Componente | Stato |
|---|---|
| `usbd.sys` | solo helper di parsing (`USBD_ParseConfigurationDescriptorEx`, `USBD_CreateConfigurationRequestEx`, `USBD_GetUSBDIVersion`…); il resto stub (`dlls/usbd.sys/usbd.sys.spec`) |
| `usbport.sys`, `usbhub.sys`, `usbccgp.sys` | assenti (non servono a un driver client se `wineusb` fa da bus) |
| `hidclass.sys`, `hidparse.sys`, `winehid.sys`, `mouhid.sys` | presenti; i dispositivi HID passano da `winebus.sys` (IOHIDManager), non da `wineusb` |
| `wdfldr.sys`, `wdf01000.sys` (KMDF) | **assenti**. Bug WineHQ 49193, aperto dal 2020: stub di `WdfVersionBind` non basta; Zeb Figura valutava un port del codice MIT di Microsoft ([bug 49193](https://bugs.winehq.org/show_bug.cgi?id=49193)) |
| UMDF (`WUDFHost.exe`, `WUDFx02000.dll`, riflettore) | **assente** |
| `winusb.dll` | 22 export, tutti stub tranne `WinUsb_Free` (`dlls/winusb/winusb.spec`, `main.c`) |
| `winusb.sys`, `usbscan.sys` | **assenti** |
| `windows.devices.usb` | solo stub WinRT |
| Installazione driver: `pnputil.exe` | stub che non fa nulla (`programs/pnputil/main.c`) |
| `difxapi` (`DriverPackageInstall`…) | stub (`dlls/difxapi/main.c:30-56`) |
| `newdev`: `UpdateDriverForPlugAndPlayDevices` | implementata (`dlls/newdev/main.c:83`); `DiInstallDriver` stub (`:154-165`) |
| `SetupCopyOEMInf` | implementata (`dlls/setupapi/devinst.c:5944`) |

### Cosa fallisce oggi per un driver client USB tipico (nell'ordine in cui lo incontra)

1. **Barrique**: `.sys` x64 rifiutato dal loader (§5).
2. Installer del produttore: `DPInst`/`difxapi`/`pnputil` non installano; l'INF deve finire in `C:\windows\inf` con `SetupCopyOEMInf` o a mano.
3. **KMDF** (la maggioranza dei driver USB dopo il 2010): import da `WDFLDR.SYS` non risolto → il driver non si carica.
4. WDM puro: `AddDevice`/`START` ok, poi `IRP_MN_QUERY_INTERFACE` senza risposta, `SELECT_INTERFACE`/`CONTROL_TRANSFER`/`CLASS_*` non gestiti, DPC mai eseguiti, `KeStallExecutionProcessor` stub, `PoRequestPowerIrp` fallisce.
5. macOS: bulk senza interfaccia reclamata (sopra).

## 3. Scanner

Come funzionano su Windows: il produttore fornisce quasi sempre **solo codice utente** (un TWAIN Data Source `.ds` e/o un minidriver WIA) sopra un driver kernel Microsoft: `usbscan.sys` (classico, ancora usato da Canon, Epson, Brother, Fujitsu…) o `winusb.sys` (più recenti). Pochi scanner hanno un `.sys` proprio.

| Pezzo | In Wine | Cosa servirebbe |
|---|---|---|
| `usbscan.sys` | assente | **Nuovo driver Wine** (WDM, nostro codice, è glue lato Wine) che si aggancia ai PDO di `wineusb` per `USB\Class_06`/ID del produttore e crea `\\.\Usbscan0..N`. API utente: `ReadFile`/`WriteFile` sulla pipe bulk, `DeviceIoControl` con `IOCTL_SEND_USB_REQUEST` (vendor/class control), `IOCTL_GET_PIPE_CONFIGURATION`, `IOCTL_GET_USB_DESCRIPTOR`, `IOCTL_RESET_PIPE`, `IOCTL_CANCEL_IO`, `IOCTL_GET_DEVICE_DESCRIPTOR`, `IOCTL_GET_VERSION`, `IOCTL_WAIT_ON_DEVICE_EVENT` (interrupt), `IOCTL_SET_TIMEOUT`. Tutte si mappano su URB che `wineusb` sa già fare o quasi (manca `CLASS_*`). Stima: **1-2 settimane** + `wineusb` completato |
| Registrazione STI/WIA dei dispositivi immagine | `sti.dll` quasi tutta stub (`dlls/sti/sti.c:77-116`, `GetDeviceList`/`CreateDevice` = `E_NOTIMPL`) | Molti `.ds` trovano il dispositivo via `SetupDiGetClassDevs(GUID_DEVCLASS_IMAGE)` e la chiave `CreateFileName` del dispositivo (funziona già con setupapi), altri via `IStillImage::GetDeviceList` (da implementare, 3-5 giorni) |
| TWAIN DSM | `twaindsm.dll` cerca `*.ds` in `C:\windows\twain_32` o `twain_64` secondo il bit del processo (`dlls/twaindsm/dsm_ctrl.c:171-172`) e li carica con `LoadLibrary` (`:468`); `twain_32.dll` solo i386, inoltra a `twaindsm` | Già pronto: un `.ds` del produttore funziona se il suo trasporto (usbscan/WinUSB) esiste |
| `sane.ds` (scanner tramite SANE dell'host) | presente in Wine, **disattivato in Barrique** (`--without-sane`, `build-arm64ec/config.log:7`) | Alternativa senza driver Windows: compilare `sane-backends` per macOS (molti backend: Epson, Canon pixma, HP via hplip…) e includerli nella `.app`. Stima 2-4 giorni; copre gli scanner supportati da SANE, non quelli del produttore |
| WIA | `wiaservc` minimale (`dlls/wiaservc/wiadevmgr.c:200`, `CreateDevice` = `E_NOTIMPL`) | Ospitare minidriver WIA (`IWiaMiniDrv`) = rifare il servizio WIA: settimane, priorità bassa (quasi tutti i produttori danno anche TWAIN) |
| `winusb` | stub | vedi §6, serve anche agli scanner recenti |

## 4. Stampanti (breve)

- Wine: `spoolsv` + `spoolss` + `localspl` (porte, con monitor CUPS `localspl/cups.c`) + `winspool.drv`; le stampanti CUPS di macOS compaiono come stampanti Windows con driver **`wineps.drv`** (PostScript). CUPS è già collegato in Barrique (`CUPS_LIBS = -lcups`).
- `gdi32` accetta solo driver in stile Wine: carica la DLL e cerca `wine_driver_open_dc` (`dlls/gdi32/dc.c:184-197`, `:226-233`). Un driver v3 del produttore (DDI `DrvEnableDriver`/`DrvEnablePDEV`, UNIDRV/PSCRIPT5 con GPD/PPD) **non può fare da backend GDI**. `winspool.drv` sa caricare la parte UI/config (`DrvDeviceCapabilities`, `dlls/winspool.drv/info.c:461-472`), non il rendering.
- Ospitare v3 richiederebbe un motore DDI (Eng*/Drv*, simile a win32k) dentro gdi32: mesi. v4 (XPS) richiederebbe la pipeline di filtri XPS (`PrintFilterPipelineSvc`, `prntvpt` e `xpsprint` sono stub): mesi.
- Pratico: stampare tramite CUPS (driver macOS/IPP Everywhere/AirPrint, che oggi coprono la gran parte delle stampanti). Le utilità del produttore (livelli inchiostro, pulizia testine) che parlano via USB con la stampante ricadono nel caso WinUSB/`usbprint.sys` (non presente): stessa strategia di `usbscan`.

## 5. Vincoli specifici di Barrique

### Architettura del processo che ospita il driver (bloccante)

- Il loader accetta un'immagine solo se la sua macchina coincide con quella del processo (`dlls/ntdll/loader.c:2501-2511`); `current_machine` è ARM64 nella parte nativa e AMD64 nella parte ARM64EC (`dlls/ntdll/ntdll_misc.h:40-47`: con il compilatore ARM64EC di Barrique `__x86_64__` è definito, verificato: l'`instr.o` arm64ec contiene `vectored_handler` e `KeGetCurrentIrql`).
- `services.exe` (nativo) lancia `winedevice.exe` senza indicare la macchina (`services.c:1090`); un exe ibrido ARM64X parte come ARM64 nativo se il padre è nativo (`dlls/ntdll/unix/process.c:923-927`). Quindi **winedevice è sempre ARM64 nativo e un `.sys` AMD64 dà `STATUS_INVALID_IMAGE_FORMAT`**.
- Inoltre l'INF viene scelto con l'estensione della piattaforma del processo: `.NTarm64` in nativo, `.NTamd64` in ARM64EC (`dlls/setupapi/devinst.c:55-62`). Un INF del produttore ha solo `NTamd64`, quindi in nativo non trova nemmeno il driver.
- I driver PnP vengono caricati **nel processo di `wineusb.sys`** (`pnp.c:244`), quindi è quel winedevice a dover essere ARM64EC.

Correzione (piccola, lato Wine): in `services.c` leggere la macchina del `.sys` (non solo 32/64 bit), tenere i processi winedevice separati per macchina (`get_winedevice_process`, `:876-900`) e passare `PROC_THREAD_ATTRIBUTE_MACHINE_TYPE = AMD64` (supportato: `dlls/kernelbase/process.c:347`, `dlls/ntdll/unix/process.c:894-896`); lanciare come AMD64 anche il winedevice del gruppo che contiene `wineusb` (o spostare `wineusb` in un gruppo suo). Le DLL di Wine sono ARM64X (`ntoskrnl.exe` è `ARM64X` con 3341 export, verificato in `build-arm64ec`), quindi `wineusb.sys`, `ntoskrnl`, `hal`, `usbd` funzionano anche nel processo EC. Nota: Windows 11 su ARM rifiuta i driver kernel x64; qui andremmo oltre Windows, e questo è un punto a favore.

I driver i386 (raro: solo Windows a 32 bit) passano già da winedevice WoW64 (`services.c:861-862`).

### Istruzioni privilegiate sotto FEX

- FEX (solo letto, non modificato) traduce `in`/`out`/`ins`/`outs`, `cli`/`sti`, `mov cr`/`mov dr`, `rdmsr`/`wrmsr`/`rdpmc`, `clts`, `lgdt`/`lidt` ecc. in un #GP (`FEXCore/Source/Interface/Core/OpcodeDispatcher/BaseTables.h:38,75,79,85`, `SecondaryTables.h:9,15,17,19`, `OpcodeDispatcher.cpp:5299`), che diventa `EXCEPTION_PRIV_INSTRUCTION` con contesto x64 (`Source/Windows/Common/Exception.h:72`).
- Il gestore di `ntoskrnl` (`instr.c:900-945`, compilato anche per ARM64EC) le emula come su Wine x64: `mov cr0/2/3/4/8` (lettura 0, scrittura ignorata, `instr.c:726-783`), `mov dr`, `rdmsr` (solo `MSR_LSTAR`, il resto 0, `:804-823`), `cli`/`sti` ignorate, letture di `KUSER_SHARED_DATA` all'indirizzo kernel `0xfffff78000000000` rimappate su `0x7ffe0000` (`:493-494`, `:826-845`). `in`/`out` **non** sono emulate (correttamente: non c'è hardware).
- **Costo**: su x64 `KeGetCurrentIrql`/`KeRaiseIrql`/`KeLowerIrql` sono inline (`mov cr8`), quindi ogni cambio di IRQL è un'eccezione completa: FEX → trap EL1 → host → dispatch dell'eccezione nella VM → gestore vettoriale. Accettabile per un driver USB (decine di µs), da misurare. Accesso a `0xfffff78000000000`: con `EPD1` (`dlls/ntdll/unix/hvf.c:803-805`) la metà alta non è mappata, il fault diventa un access violation normale, gestito dallo stesso codice.
- Rischio da verificare: il gestore vettoriale EC vede anche le eccezioni del codice ARM64EC e decodificherebbe byte ARM come x86; agisce solo su istruzioni privilegiate e indirizzi KUSD, quindi il rischio è basso, ma conviene filtrare con `RtlIsEcCode(Rip)`.

### HVF

Nulla in `hvf.c` blocca codice "da driver": winedevice è un processo normale a EL0, con thread, APC (`irp_completion`) e attese come gli altri. Già corretto il deadlock `hvf_mutex`/`virtual_mutex` con APC di completamento IRP (ROADMAP, 2026-11-19), che toccava proprio questo percorso. Limite noto: tutta la memoria Windows sotto `IPA_LIMIT` = 1 TB (`hvf.c:57`), irrilevante per i driver USB.

### macOS

- Nessuna entitlement serve per aprire un dispositivo USB da un'app non sandboxed (Barrique ha solo `com.apple.security.hypervisor`, `Barrique/entitlements.plist`).
- Un dispositivo già preso da un driver macOS (HID, mass storage, audio, CDC/seriale, fotocamere PTP prese da `ptpcamerad`) non si può reclamare: `libusb` lo "cattura" solo come root o con l'entitlement **`com.apple.vm.device-access`** (`darwin_usb.c:2879`, `:2932`), riservata da Apple (va richiesta). Scanner e stampanti di classe vendor (0xFF) o 0x06/0x07 di solito non hanno driver macOS e sono liberi; una stampante usata da CUPS va condivisa con il backend USB di CUPS (prende il dispositivo solo durante la stampa).
- Interfacce da reclamare (vedi §2).

## 6. Piano consigliato

| # | Lavoro | Stima | Sblocca |
|---|---|---|---|
| 0 | **winedevice ARM64EC per i `.sys` x64** (`services.c`: macchina dell'immagine, processi per macchina, `MACHINE_TYPE` AMD64; gruppo di `wineusb` in EC). Verifica con i test già esistenti `dlls/ntoskrnl.exe/tests` (`driver.c`, `driver_pnp.c`…), mai girati in Barrique (assenti in `tests/results`), su aarch64 e x86_64 | 1-2 giorni | qualunque `.sys` x64 WDM; base per tutto il resto |
| 1 | `wineusb`: ~~`claim_interface` su macOS~~ (fatto), `SELECT_INTERFACE`, `CONTROL_TRANSFER(_EX)`, `CLASS_*`, `GET_STATUS`, `GET_CONFIGURATION`, `GET_DESCRIPTOR_FROM_INTERFACE`, `QUERY_INTERFACE` USBDI, `RESET/CYCLE_PORT`, alternate setting | 1 settimana | trasferimenti affidabili per tutto ciò che sta sopra |
| 2 | **`usbscan.sys` emulato** (driver Wine nuovo sopra `wineusb`) + `sti` `GetDeviceList`/`CreateDevice` minimi | 1-2 settimane | gran parte degli scanner USB con driver TWAIN del produttore |
| 3 | **WinUSB**: `winusb.sys` (driver Wine sopra `wineusb`, interfaccia `\\.\`) + `winusb.dll` con le 22 funzioni via `DeviceIoControl`; INF `Include=winusb.inf` / descrittori MS OS 2.0 (compatible ID `WINUSB`) | 1-2 settimane | scanner recenti, programmatori/debugger (Arduino, STM32, FTDI D2XX no: ha un suo `.sys`), utilità di stampanti, molti gadget |
| 4 | API ntoskrnl mancanti per WDM: DPC veri (`KeInsertQueueDpc` su un thread "DPC"), `KeStallExecutionProcessor` in `hal`, `PoRequestPowerIrp` reale, `IoSetCompletionRoutineEx`, rundown protection, `IoQueueWorkItemEx`, `ExInitializeLookasideListEx`, ETW minimo (`EtwRegister`/`EtwWrite` che non fanno nulla), PnP: `UpperFilters`/`LowerFilters`, `IoGetDeviceInterfaces`, `IoRegisterPlugPlayNotification` | 1-2 settimane | driver WDM USB di terze parti (vecchi, pre-2010) |
| 5 | Installazione: `pnputil /add-driver /install`, `DriverPackageInstall` (difxapi), `DiInstallDriver` sopra `SetupCopyOEMInf` + `UpdateDriverForPlugAndPlayDevices` | 3-5 giorni | installer dei produttori senza intervento manuale |
| 6 | **KMDF**: `wdfldr.sys` (`WdfVersionBind`, tabella delle funzioni) + `wdf01000.sys` compilato dal codice MIT di Microsoft ([Windows-Driver-Frameworks](https://github.com/microsoft/Windows-Driver-Frameworks); il loader non è incluso; ReactOS lo compila già, collegandolo staticamente, [ros-diffs](https://lists.reactos.org/hyperkitty/list/ros-diffs@reactos.org/thread/CIQALF6D7LINSLKOIZR4HHTOSKDRMTFA)). È C++ che si appoggia a molte API kernel (punto 4 è prerequisito) e a una macchina a stati PnP/power che si aspetta IRP che Wine non manda | 1-2 mesi | la maggioranza dei driver USB moderni con `.sys` proprio |
| 7 | UMDF 2 (`WUDFHost` + riflettore) dallo stesso codice | 1 mese dopo il 6 | sensori, alcuni lettori di carte/biometrici, driver MTP |
| — | `sane.ds` con `sane-backends` per macOS (alternativa senza driver Windows) | 2-4 giorni | scanner supportati da SANE |

Ordine: **0 → 1 → 2/3** danno il massimo con il minimo: il codice del produttore resta in modalità utente (TWAIN, WIA lato DLL, programmi WinUSB), il kernel lo scriviamo noi. 4-5 aprono i driver WDM. 6 (KMDF) è il vero salto per i `.sys` moderni, ma è il lavoro più grosso e va fatto solo se c'è un dispositivo concreto da sbloccare.

### Classi di dispositivi

| Possibile | Dopo il passo |
|---|---|
| Scanner con TWAIN + usbscan | 2 |
| Dispositivi WinUSB (scanner recenti, schede di sviluppo, utilità stampanti, gadget) | 3 |
| Driver USB WDM di terze parti (bulk/interrupt/control) | 0+1+4 |
| Driver USB KMDF | 6 |
| Dispositivi isocroni (webcam UVC proprietarie, audio USB proprietario, acquisitori video) | serve l'isocrono in `wineusb` (+1-2 settimane) e prestazioni da verificare sotto FEX |
| Stampa | CUPS + `wineps.drv`, già oggi |

### Impossibile (o fuori scopo)

- PCI/PCIe, MMIO, porte I/O, interrupt, DMA, memoria fisica (`MmMapIoSpace`, `IoConnectInterrupt`, `IoGetDmaAdapter`, `HalGetBusData` sono stub e devono restarlo): schede di acquisizione, dongle LPT, schede PCI.
- Driver filtro (classe, disco, file system, rete NDIS/WFP), minifilter, driver di bus non USB, driver video/audio kernel (WDDM, PortCls).
- Anti-cheat e DRM kernel (vogliono memoria di altri processi, callback di processo reali, `SystemCodeIntegrityInformation`; cf. [bug 49193](https://bugs.winehq.org/show_bug.cgi?id=49193)).
- Dispositivi già presi da un driver macOS senza l'entitlement `com.apple.vm.device-access`.
- Driver che passano puntatori della memoria del chiamante dentro buffer IOCTL (METHOD_NEITHER annidato) o usano `KeStackAttachProcess`: il driver vive in un altro processo.

## Primo passo consigliato

Passo 0: far girare i test `ntoskrnl` (aarch64 e x86_64, `tests/run-conformance.sh`) per avere la base, poi la modifica di `services.c` per lanciare winedevice come ARM64EC quando il `.sys` è AMD64 (e per il gruppo di `wineusb`), e rifare i test x86_64: il driver di test x64 deve caricarsi, rispondere a IOCTL ed eseguire `mov cr8` e letture KUSD attraverso FEX.

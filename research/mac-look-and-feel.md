# Barrique: dare ai programmi Windows l'aspetto e il comportamento di un'app Mac

Studio di fattibilità, 17 novembre 2026. Basato su Wine 11.22 in `~/Work/Personale/wine/src` (percorsi relativi a quella cartella). Nessun codice è stato modificato e Wine non è stato eseguito: quello che segue viene dalla lettura dei sorgenti, non da prove.

## Sintesi

- **La cornice della finestra è già nativa.** winemac.drv toglie la barra del titolo e il bordo disegnati da Windows e li sostituisce con quelli di una `NSWindow` vera (semafori, ombra, ridimensionamento, schermo intero, Stage Manager gestito). Mancano solo rifiniture: titolo e barra degli strumenti unificati, schede (disattivate apposta), e le app con barra del titolo personalizzata, che restano senza semafori.
- **Liquid Glass (macOS 26+) sulla cornice arriva gratis.** Wine in Barrique è compilato con l'SDK macOS 27 (`otool -l build-arm64/loader/wine` → `sdk 27.0`), e AppKit dà il nuovo aspetto alle app compilate con SDK 26 o successivo: barra del titolo, semafori e angoli della `NSWindow` dovrebbero essere già Liquid Glass. Da confermare a occhio.
- **I controlli sono la parte più "Windows".** Wine usa il tema `aero.msstyles` (pulsanti blu stile Windows 7). Un tema nostro con forme e colori dei controlli di macOS 26/27 (pulsanti a capsula, angoli grandi) è fattibile con gli stessi strumenti con cui Wine costruisce `aero.msstyles` (file `.rc` + immagini disegnate da noi), senza copiare risorse Apple. **Il vetro vero (trasparenza, sfocatura, rifrazione) non si può fare con un msstyles**: le immagini sono statiche. Si può avere solo con AppKit lato host, dietro le finestre che lo chiedono (vedi §2, "Liquid Glass"). Il tema scuro in Wine oggi è solo un valore di registro: non esiste un tema scuro da disegnare.
- **Colori e aspetto chiaro/scuro si possono sincronizzare con poco**: leggere `NSApp.effectiveAppearance` e `NSColor.controlAccentColor` lato host, scrivere `AppsUseLightTheme` e i colori di sistema nel registro del prefisso, mandare `WM_SETTINGCHANGE`.
- **Font**: SF Pro non si può distribuire, ma Wine su macOS vede già i font del sistema via CoreText. Si può sostituire "Segoe UI" con un font di sistema del Mac dell'utente al momento dell'avvio, senza metterlo nella `.app`. SF ha un nome nascosto (`.SF NS`) e va verificato se CoreText lo espone.
- **Integrazione col sistema**: appunti, trascinamento file, cursori, scorrimento preciso, icona del Dock e icone nella barra dei menu ci sono già. Mancano: menu dell'app nella barra dei menu macOS (Wine mette solo un menu fisso "Wine/Finestra"), finestre Apri/Salva native, notifiche native, badge e barra di avanzamento nel Dock.
- **Piano consigliato**: prima i ritocchi di registro (giorni), poi sincronizzazione dell'aspetto e un tema msstyles chiaro/scuro (settimane), infine dialoghi Apri/Salva nativi e menu globali (mesi, alto rischio di rotture). Sempre attivabile/disattivabile per singola app.

---

## 0. Dove gira cosa in Barrique

Due metà, e conta per ogni proposta qui sotto:

- **Lato host (macOS, Cocoa)**: `dlls/winemac.drv/*.m` e i file `.c` della parte Unix di winemac. Qui si parla con AppKit: finestre, menu, appunti, Dock, aspetto chiaro/scuro.
- **Lato Windows (PE nella VM)**: `uxtheme`, `comctl32`, `comdlg32`, `user32`, `explorer`. Qui si disegnano i controlli e si leggono registro e colori di sistema.

Tutto ciò che deve sapere "com'è configurato il Mac" (aspetto, colore d'accento, font) va letto sul lato host e passato al lato Windows tramite registro o messaggi. Questo è il punto da progettare con cura.

---

## 1. Cornice della finestra (barra del titolo, semafori)

### Cosa fa già Wine

- **Barra del titolo nativa.** Per ogni finestra di primo livello winemac decide quali parti della cornice Windows "assorbire": se lo stile ha `WS_CAPTION` attiva barra del titolo e ombra; `WS_SYSMENU` → pulsante chiudi, `WS_MINIMIZEBOX` → riduci, `WS_MAXIMIZEBOX`/`WS_THICKFRAME` → ridimensionabile, `WS_EX_TOOLWINDOW` → finestra utility (`dlls/winemac.drv/window.c:50-79`). Questo diventa lo `styleMask` della `NSWindow` (`dlls/winemac.drv/cocoa_window.m:70-84`).
- **Taglio della cornice Windows.** win32u chiede al driver quali stili "gestisce lui" (`macdrv_GetWindowStyleMasks`, `dlls/winemac.drv/window.c:1629-1645`) e calcola un "rettangolo visibile" senza quella cornice (`get_visible_rect`, `dlls/win32u/window.c:2149-2176`). Risultato: l'app non disegna la sua barra del titolo, la disegna macOS.
- **Interruttore globale.** La chiave `Decorated` (`dlls/win32u/sysparams.c:169`, `:6089-6090`) spegne tutto questo: con `N` le finestre sono senza cornice macOS e Windows disegna la sua.
- **Schermo intero nativo.** Le finestre ridimensionabili ricevono `NSWindowCollectionBehaviorFullScreenPrimary` e quindi il pulsante verde e la voce "Entra in schermo intero" (`dlls/winemac.drv/cocoa_window.m:991-1013`).
- **Stage Manager**: rilevato leggendo `com.apple.WindowManager` (`dlls/winemac.drv/cocoa_window.m:177-190`) e usato per gestire la riduzione a icona (`:1182-1199`).
- **Schede di finestra disattivate apposta**: `[NSWindow setAllowsAutomaticWindowTabbing:NO]` (`dlls/winemac.drv/cocoa_app.m:137`).

### Cosa manca

1. **Barra menu Windows dentro la finestra.** Il menu `HMENU` dell'app (File, Modifica…) fa parte dell'area non-client ma winemac assorbe solo titolo e bordo: il menu resta disegnato da Windows sotto la barra del titolo macOS. È la differenza più visibile da un'app Mac. Vedi §4.1.
2. **Titolo e barra strumenti unificati** (`NSWindowStyleMaskFullSizeContentView`, `titlebarAppearsTransparent`). Non usati. Si potrebbero attivare per app che hanno una barra strumenti in alto, ma Wine non sa dov'è la barra strumenti dell'app: il contenuto finirebbe sotto i semafori. Utile solo con regole per singola app; non come default.
3. **App con barra del titolo personalizzata** (Electron, Chrome, Office, Steam, molte app moderne). Gestiscono `WM_NCCALCSIZE` e disegnano da sé titolo e pulsanti. In quel caso l'area client copre tutta la finestra e `get_visible_rect` ripiega sull'intera finestra (`dlls/win32u/window.c:2166-2169`): `NSWindow` senza bordo, **niente semafori**, pulsanti Windows in alto a destra. Rimedio possibile: per queste finestre mostrare comunque i semafori sopra il contenuto (`NSWindowStyleMaskTitled | FullSizeContentView` con barra trasparente) e tradurre il clic sulle zone che l'app dichiara `HTCAPTION`/`HTCLOSE` via `WM_NCHITTEST`. È lavoro delicato: l'app continua a disegnare i suoi pulsanti, quindi si avrebbero due set di pulsanti. Da valutare app per app.
4. **Colore/scuro della barra del titolo chiesto dall'app.** `DwmSetWindowAttribute` è uno stub (`dlls/dwmapi/dwmapi_main.c:101-107`): un'app che chiede `DWMWA_USE_IMMERSIVE_DARK_MODE` non ottiene nulla. Con la cornice macOS il titolo segue già l'aspetto del Mac, quindi il danno è piccolo; ma si potrebbe usare la richiesta per forzare `NSAppearance` scura su quella finestra.
5. **Liquid Glass sulla cornice.** Da macOS 26 AppKit applica Liquid Glass alle app compilate con SDK 26 o successivo; quelle compilate con SDK vecchi restano con l'aspetto precedente ([Apple Developer Forums](https://developer.apple.com/forums/thread/788573), [CPython #139404](https://github.com/python/cpython/issues/139404)). Il nostro `loader/wine` e `winemac.so` hanno `minos 27.0, sdk 27.0` (`otool -l`), quindi la cornice dovrebbe già essere quella nuova. Due cose da guardare con una prova: (a) gli angoli della finestra in macOS 26+ sono più arrotondati e possono tagliare i pixel negli angoli in basso del contenuto Windows, che è disegnato squadrato; (b) senza `NSToolbar` la barra del titolo resta una striscia semplice: il vetro "che galleggia sul contenuto" si vede solo con toolbar e sidebar native, che le app Windows non hanno. Non attivare `UIDesignRequiresCompatibility` (spegnerebbe Liquid Glass) salvo bug gravi di AppKit con le nostre finestre.
6. **Schede di finestra**: lasciarle spente. Le app Windows non sanno di condividere una finestra e i loro `SetWindowPos` litigherebbero con AppKit.

### Cosa fanno gli altri

CrossOver usa lo stesso winemac (CodeWeavers ne è l'autore) e quindi la stessa cornice nativa; un utente riporta barre del titolo scure con CrossOver 21 ([forum CodeWeavers](https://www.codeweavers.com/support/forums/general?msg=239945&t=27)), e CrossOver 21 ha aggiunto il modo scuro alla propria interfaccia ([ifun.de](https://www.ifun.de/crossover-21-unterstuezt-dark-mode-und-neue-game-controller-173755/)). Non ho trovato documentazione su un tema dei controlli in stile Mac. Whisky usava Wine/winemac senza modifiche all'aspetto ed è archiviato dal 2025 ([GitHub](https://github.com/Whisky-App/whisky), [TechSpot](https://www.techspot.com/news/107499-whisky-popular-wine-frontend-mac-gamers-no-more.html)). Game Porting Toolkit riguarda la grafica (D3DMetal), non l'interfaccia ([Apple](https://developer.apple.com/games/game-porting-toolkit/)). Porting Kit è un gestore di "wrapper" Wine e non risulta toccare l'aspetto. **In sintesi: nessuno dei progetti noti va oltre la cornice nativa di winemac.** Il resto di questo documento è terreno nuovo.

---

## 2. Controlli: un tema che somigli a macOS

### Come funziona il tema in Wine

- Tema attivo e file scelti nel registro: `HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager`, valori `ThemeActive`, `DllName`, `ColorName`, `SizeName` (`loader/wine.inf.in:1067-1071`; letti in `dlls/uxtheme/system.c:44`, `:147-170`). Di serie: `aero.msstyles`, colore `Blue`.
- `aero.msstyles` è una DLL di sole risorse costruita da `dlls/aero.msstyles/aero.rc` (≈4600 righe): un file INI incorporato (`BLUE_INI`, `aero.rc:84-85`) che per ogni classe/parte/stato indica immagine, margini, colori e font, più ~480 immagini BMP generate da sorgenti SVG presenti nel tree (`dlls/aero.msstyles/blue_*.svg`). Un solo schema di colore (`COLORNAMES` = "Blue", `aero.rc:55-57`).
- comctl32 v6 e user32 chiedono a uxtheme di disegnare pulsanti, caselle, combo, schede, barre di scorrimento, progressi, ecc. Le app che usano controlli standard cambiano aspetto cambiando file msstyles, senza toccare codice.

### Fattibilità di un tema in stile macOS 26/27 ("Barrique")

Il riferimento è l'aspetto attuale di macOS (Liquid Glass, introdotto con macOS 26 Tahoe), non il vecchio Aqua. Nota: "Aqua" resta il nome tecnico dell'aspetto chiaro in AppKit (`NSAppearanceNameAqua` / `NSAppearanceNameDarkAqua`), e in quel senso compare ancora nelle API usate in questo documento.

- **Tecnicamente semplice da avviare**: copiare la struttura di `dlls/aero.msstyles` in un nuovo modulo (es. `barrique.msstyles`) e ridisegnare gli SVG. La toolchain c'è già (svg → bmp nel build di Wine). Si può anche costruirlo fuori dal tree Wine, nel repo Barrique, come DLL di risorse e installarlo in `C:\windows\resources\themes\barrique\`.
- **Cosa rende "Mac" oggi**: pulsanti a capsula o con angoli molto arrotondati, controlli un po' più alti, pulsante predefinito pieno col colore d'accento, caselle e radio col colore d'accento, combo con freccia doppia, barre di scorrimento sottili (solo l'aspetto: il comportamento "a scomparsa" non c'è in uxtheme), schede in stile "segmented control" a capsula, cursori (slider) con manopola a capsula, progressi sottili. Le misure esatte vanno prese guardando macOS, non copiando file Apple.
- **Cosa non si può fare con un msstyles**: il materiale Liquid Glass (trasparenza che sfoca e rifrange ciò che sta dietro, riflessi che cambiano). uxtheme sa solo disegnare bitmap e riempimenti, con al massimo la trasparenza alfa sopra lo sfondo della stessa finestra. Il tema imita quindi forme e colori, "piatto", senza vetro. È la scelta giusta per i controlli dentro dialoghi Windows: anche in macOS 26 il vetro è riservato a toolbar, sidebar e controlli che galleggiano sul contenuto, non ai pulsanti dentro un modulo.
- **Limiti**: il tema cambia l'aspetto ma non le misure dei dialoghi. I dialoghi Windows sono disegnati in "dialog units" su Segoe UI/MS Shell Dlg: pulsanti alti 14 DLU, spaziature Windows. Un pulsante "macOS" dentro un layout Windows resta un layout Windows. Lo scopo realistico è "coerente e pulito accanto alle app Mac", non "indistinguibile".
- **Varianti**: servono almeno due schemi di colore nello stesso file (`Light`, `Dark`) e possibilmente la DPI alta: le immagini vanno fornite per 192 DPI (Barrique usa 192 DPI di default, `Barrique/scripts/prefix-defaults.reg`), come fa già `aero` con le serie `_13px … _52px`.

### Liquid Glass vero: materiali Windows 11 → materiali AppKit

C'è un ponte naturale. Le app Windows 11 chiedono uno sfondo traslucido per la finestra con `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)` (Mica, Acrylic) o con `DwmExtendFrameIntoClientArea`, e lasciano trasparenti (alfa 0) i pixel dove vogliono vedere il materiale. In Wine oggi `DwmSetWindowAttribute` è uno stub (`dlls/dwmapi/dwmapi_main.c:101-107`) e quei pixel diventano neri o opachi.

Proposta: inoltrare la richiesta a winemac e mettere dietro al contenuto della finestra una view di materiale AppKit: `NSGlassEffectView` (macOS 26+, [documentazione AppKit](https://developer.apple.com/documentation/updates/appkit), [`NSGlassEffectContainerView`](https://developer.apple.com/documentation/appkit/nsglasseffectcontainerview)) o, più prudente, `NSVisualEffectView` con materiale `.windowBackground`/`.sidebar`. La finestra deve essere non opaca e con contenuto a alfa per pixel, cosa che winemac già fa per altri casi (`setBackgroundColor:[NSColor clearColor]`, `dlls/winemac.drv/cocoa_window.m:886`, `:1722`, `:1907`). Così un'app Windows 11 con Mica avrebbe dietro il vetro di macOS, e per le app vecchie non cambia nulla.

Limiti: funziona solo per le app che chiedono un materiale (poche, e quasi tutte WinUI/WPF moderne); va verificato che la superficie di Barrique (che copia i pixel dalla VM) mantenga l'alfa; costo GPU del vetro su finestre grandi. Lavoro medio-grande, da fare dopo il tema.

### Licenze

- Il tema va **disegnato da noi**. Non copiare immagini, PDF o file Sketch/Figma da [Apple Design Resources](https://developer.apple.com/design/resources/): sono concessi per progettare app per piattaforme Apple, e la licenza dei font Apple vieta esplicitamente i mock-up di interfacce per sistemi non Apple (vedi §3). Un tema che gira dentro Windows emulato è il caso più dubbio: niente risorse Apple, nemmeno estratte da AppKit a runtime e salvate nel prefisso.
- Ispirarsi a forme generiche (angoli arrotondati, colore d'accento) non è copia. Evitare di riprodurre in modo riconoscibile icone o glifi Apple (es. la freccia doppia della combo andrebbe ridisegnata in modo proprio).
- Partire dai sorgenti SVG di `aero.msstyles` va bene: Wine è LGPL 2.1 e il tema derivato resta LGPL.

### Chiaro/scuro che segue macOS

Stato in Wine 11.22:

- `ShouldAppsUseDarkMode` / `ShouldSystemUseDarkMode` leggono `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme` / `SystemUsesLightTheme` (`dlls/uxtheme/system.c:1271-1297`). winecfg li scrive entrambi (`programs/winecfg/theme.c:499-502`).
- **Ma nessun tema scuro esiste**: `aero.rc` non ha voci `DarkMode_*` e né comctl32 né user32 cambiano disegno in base a quel valore (nessun riferimento in `dlls/comctl32`, `dlls/user32`, `dlls/win32u`). `RefreshImmersiveColorPolicyState` è uno stub (`dlls/uxtheme/system.c:1260`). Quindi oggi "scuro" in Wine significa: le app che leggono il valore (es. Electron, app WinUI/WPF che decidono da sole) diventano scure, i controlli standard restano chiari.

Proposta:

1. **Lato host**: in winemac osservare `NSApp.effectiveAppearance` (KVO) e la notifica `AppleInterfaceThemeChangedNotification`; al cambio, mandare un evento al lato Windows (nuovo tipo di evento macdrv, come gli altri in `cocoa_event.m`).
2. **Lato Windows**: alla ricezione scrivere `AppsUseLightTheme`/`SystemUsesLightTheme`, scegliere `ColorName` = `Light`/`Dark` del nostro msstyles, aggiornare i colori di sistema (`SetSysColors`) e mandare `WM_SETTINGCHANGE` con `lParam = "ImmersiveColorSet"` (oggi Wine non lo manda mai: nessuna occorrenza nel tree) e `WM_THEMECHANGED`.
3. Variante povera senza patch a winemac: il launcher della `.app` legge l'aspetto all'avvio (`defaults read -g AppleInterfaceStyle`) e scrive il registro prima di avviare Wine. Nessun cambio a caldo, ma costo quasi zero.

### Colore d'accento → colori di sistema

I colori di sistema sono in `HKCU\Control Panel\Colors` con i valori predefiniti in `dlls/win32u/sysparams.c:5826-5860` (es. `COLOR_HIGHLIGHT` = RGB(10,36,106), `Hilight`, riga 5840). Mappa proposta (lettura lato host, scrittura nel registro o `SetSysColors`):

| Windows | macOS |
|---|---|
| `COLOR_HIGHLIGHT`, `COLOR_MENUHILIGHT`, `COLOR_HOTLIGHT` | `NSColor.selectedContentBackgroundColor` / `controlAccentColor` |
| `COLOR_HIGHLIGHTTEXT` | `NSColor.alternateSelectedControlTextColor` |
| `COLOR_WINDOW`, `COLOR_WINDOWTEXT` | `textBackgroundColor`, `textColor` |
| `COLOR_BTNFACE`, `COLOR_3DFACE` | `windowBackgroundColor` |
| `COLOR_GRAYTEXT` | `disabledControlTextColor` |
| `COLOR_ACTIVECAPTION` ecc. | irrilevanti con cornice nativa |

Attenzione: i colori `NSColor` dinamici vanno convertiti in sRGB *dentro* l'aspetto corrente (`performAsCurrentDrawingAppearance`), lo stesso problema già notato in `dlls/winemac.drv/cocoa_window.m:3836`. Il nostro msstyles può anche dichiarare `ACCENTCOLORHINT` (proprietà già mappata in `dlls/uxtheme/stylemap.c:64`), ma le immagini BMP hanno colori fissi: per seguire l'accento le parti colorate vanno descritte come riempimenti (`FillColor`) e non come bitmap, oppure si generano varianti per gli 8 accenti macOS.

---

## 3. Font e testo

### Stato attuale in Barrique

- 192 DPI, `RetinaMode=y`, antialias in scala di grigi (`FontSmoothing=2`, `FontSmoothingType=1`) in `Barrique/scripts/prefix-defaults.reg`. Wine legge questi valori in `dlls/win32u/font.c:4783-4806`.
- Segoe UI → Selawik (`loader/wine.inf.in:510-514`), `MS Shell Dlg 2` → Tahoma (`:555`); Barrique aggiunge Selawik/Carlito/Caladea (ROADMAP passo 1b).
- Wine su macOS enumera i font installati nel sistema tramite CoreText (`dlls/win32u/freetype.c:1358-1430`), quindi i font del Mac dell'utente sono già visibili alle app Windows **senza copiarli nella `.app`**.

### Opzioni per un testo "da Mac"

1. **Non distribuire SF Pro.** La licenza è per sviluppatori Apple registrati e per app su piattaforme Apple, e vieta i mock-up di interfacce per sistemi non Apple ([Apple Fonts](https://developer.apple.com/fonts/), [Apple Developer Forums](https://developer.apple.com/forums/thread/727961), [Wikipedia](https://en.wikipedia.org/wiki/San_Francisco_(sans-serif_typeface))). Metterlo nella `.app` è escluso.
2. **Sostituzione a runtime con un font del sistema dell'utente**: impostare `HKCU\Software\Wine\Fonts\Replacements` (`dlls/win32u/font.c:955-1000`) per far puntare `Segoe UI` e `MS Shell Dlg 2` a un font di sistema macOS. Il font non viene copiato: lo usa il Mac che lo possiede già. Resta una zona grigia per SF (uso del font di sistema dentro Windows emulato); più tranquilli:
   - **Helvetica Neue** o **Lucida Grande**: font di sistema macOS con nome pubblico, sicuramente visibili a CoreText.
   - **SF**: è nel sistema come `/System/Library/Fonts/SFNS.ttf` con nome nascosto `.SF NS`; i font con nome che inizia per punto di norma non escono dalle enumerazioni pubbliche. Da verificare con una prova, non presumere.
   - Font aperti di stile simile da mettere nella `.app`: **Inter** (OFL) è la scelta più vicina a SF per metriche e aspetto ed è redistribuibile.
3. **Metriche**: SF/Inter sono più larghi di Segoe UI/Tahoma. I dialoghi Windows dimensionati su Segoe UI rischiano testi tagliati. Raccomandazione: tenere Selawik (metriche compatibili con Segoe) come default e offrire "font Mac" come opzione per singola app, oppure sostituire solo i font di menu/titoli (`NONCLIENTMETRICS`) e lasciare i dialoghi.
4. **Smussatura**: già giusta (grigio, niente subpixel su Retina). Eventuale ritocco: `FontSmoothingGamma` per avvicinare il peso a quello di Core Text, che rende più "pieno" di FreeType; da valutare a occhio.

---

## 4. Integrazione col sistema

### 4.1 Barra dei menu

- **Oggi**: winemac crea un menu globale fisso: menu applicazione (Nascondi, Nascondi altre, Mostra tutte, Esci con Cmd-Q) e menu Finestra (Riduci, Zoom, Schermo intero, Porta tutto in primo piano) (`dlls/winemac.drv/cocoa_app.m:228-300`). **I menu dell'app Windows non vengono mai portati nella barra macOS**: restano dentro la finestra (§1).
- **Proposta**: leggere l'`HMENU` della finestra attiva (`GetMenu`, `GetMenuItemInfo`) e ricostruirlo come `NSMenu`; al clic mandare `WM_COMMAND` con l'ID; prima di aprire un menu mandare `WM_INITMENUPOPUP` perché l'app abiliti/disabiliti le voci. Poi nascondere il menu dentro la finestra (togliere l'altezza della barra menu dall'area non-client, come si fa col titolo in `get_visible_rect`).
- **Problemi**: menu owner-draw (`MFT_OWNERDRAW`) e bitmap; app che ricostruiscono il menu a ogni `WM_INITMENUPOPUP` (serve un aggiornamento sincrono mentre il `NSMenu` è aperto, con la VM di mezzo → latenza); app con più finestre top-level ciascuna col suo menu; Alt e tasti di accesso (`&File`); app con menu non-`HMENU` (ribbon, menu disegnati a mano, Qt/Electron) — per loro non cambia nulla. È il lavoro più grande dell'elenco.

### 4.2 Dialoghi standard (Apri/Salva)

- **Oggi**: comdlg32 disegna i dialoghi Windows: vecchio stile `GetOpenFileNameW`/`GetSaveFileNameW` (`dlls/comdlg32/filedlg.c:4196`, `:4261`, implementazione `GetFileName95` a `:334`) e nuovo stile `IFileOpenDialog`/`IFileSaveDialog` (`dlls/comdlg32/itemdlg.c`).
- **Proposta**: un hook in comdlg32 che, quando il dialogo non è personalizzato, chiede al lato host di mostrare `NSOpenPanel`/`NSSavePanel` come foglio della finestra padre e converte il percorso Unix ottenuto in percorso Windows (`wine_get_dos_file_name`). Va trasmesso: filtri (`lpstrFilter` → `allowedContentTypes` tramite estensioni), cartella iniziale, nome proposto, selezione multipla, "seleziona cartella" (`FOS_PICKFOLDERS`).
- **Quando NON farlo** (ricadere sul dialogo Wine): `OFN_ENABLEHOOK`, `OFN_ENABLETEMPLATE*`, `IFileDialogCustomize` usato (caselle, combo aggiunte dall'app), percorsi fuori dai drive mappati. Molte app professionali personalizzano i dialoghi: la copertura reale va misurata sulle app che ci interessano.
- **Sandbox/permessi**: `NSOpenPanel` dà accesso ai file scelti anche fuori dal prefisso, ma il file deve essere raggiungibile da un drive Wine (es. `Z:` o un drive per la cartella home). Va deciso insieme alla politica dei drive della `.app`.
- Stesso schema possibile in seguito per scelta colore (`NSColorPanel`) e font; valore basso.

### 4.3 Notifiche

Le "balloon" di `Shell_NotifyIcon` (`NIF_INFO`) sono gestite da explorer (`programs/explorer/systray.c:58-89`) e disegnate come tooltip Windows. Le icone nell'area di notifica diventano già `NSStatusItem` nella barra dei menu (`dlls/winemac.drv/cocoa_status_item.m`, `systray.c`). Proposta: inoltrare le balloon a `UNUserNotificationCenter` lato host. Richiede che la `.app` sia firmata e chieda il permesso notifiche; lavoro medio-piccolo. Le notifiche "toast" moderne (WinRT `ToastNotificationManager`) sono un capitolo a parte e meno usate dalle app che ci interessano.

### 4.4 Dock: icona, badge, avanzamento

- **Oggi**: icona dell'app dal programma Windows (`dlls/winemac.drv/cocoa_app.m:305`) e miniatura nel Dock delle finestre ridotte (`dlls/winemac.drv/cocoa_window.m:2089-2190`).
- `ITaskbarList3::SetProgressValue` e `SetOverlayIcon` sono stub (`dlls/explorerframe/taskbarlist.c:142-150`, `:226-232`). Proposta: implementarli lato host con `NSDockTile` (barra di avanzamento disegnata in una view del Dock tile, `badgeLabel` per i contatori). Piccolo e utile (download, installer, esportazioni).
- `FlashWindowEx` → `requestUserAttention`: da verificare se winemac lo fa già (non controllato in questo studio).

### 4.5 Appunti e trascinamento

Già buoni: testo UTF-8/UTF-16, RTF, HTML, PNG, JPEG, TIFF, file (`CF_HDROP` ↔ `NSFilenamesPboardType`) (`dlls/winemac.drv/clipboard.c:139-181`); le finestre accettano trascinamenti di dati e file (`dlls/winemac.drv/cocoa_window.m:894`). Mancano: trascinamento *da* app Windows *verso* Finder (OLE `DoDragDrop` → `NSDraggingSource`), e `public.file-url` moderno al posto di `NSFilenamesPboardType` (deprecato). Basso impatto visivo, medio impatto d'uso.

### 4.6 Tastiera: Cmd vs Ctrl

- **Oggi**: Opzione può fare da Alt (`LeftOptionIsAlt`, `RightOptionIsAlt`) e Cmd può fare da Ctrl (`LeftCommandIsCtrl`, `RightCommandIsCtrl`) (`dlls/winemac.drv/macdrv_main.c:319-327`). Cmd-Q/H/M vanno al menu nativo.
- **Problema**: con `CommandIsCtrl=y` si perde Cmd-Q/Cmd-H nativi (diventano Ctrl-Q/Ctrl-H). Senza, l'utente Mac preme Cmd-C e non succede nulla.
- **Proposta (media)**: modalità ibrida in `keyboard.c`: Cmd+lettera → Ctrl+lettera, *eccetto* le scorciatoie di sistema macOS (Cmd-Q, Cmd-H, Cmd-Opzione-H, Cmd-M, Cmd-Tab, Cmd-`, Cmd-Spazio) che restano ad AppKit; Ctrl fisico resta Ctrl. È la combinazione che un utente Mac si aspetta. Opzionale: Cmd-W → `WM_CLOSE` invece di Ctrl-W, Cmd-, → nessuna mappatura. Cmd-frecce (inizio/fine riga) → Home/End: da aggiungere solo nei campi di testo, difficile da sapere dal driver; meglio lasciar perdere al primo giro.

### 4.7 Scorrimento

`UsePreciseScrolling` (`dlls/winemac.drv/macdrv_main.c:346`, uso in `dlls/winemac.drv/cocoa_app.m:1703`) passa al programma i delta fini del trackpad. Lo scorrimento "naturale" viene già dal sistema perché Wine usa i delta di Cocoa. Inerzia e "rimbalzo" non esistono in Windows: non si possono aggiungere senza riscrivere le app. Raccomandazione: tenere `UsePreciseScrolling=y` (default) e non andare oltre.

### 4.8 Cursori

I cursori standard di Windows sono già mappati sui cursori `NSCursor` nativi (freccia, I-beam, mano, ridimensionamenti…) (`dlls/winemac.drv/mouse.c:46-111`, controllo dei nomi a `:197-199`). I cursori personalizzati delle app restano i loro. Nulla da fare.

---

## 5. Per singola app o globale, cosa si rompe, costo

### Per app o globale

winemac legge già le opzioni sia globali sia per eseguibile (`HKCU\Software\Wine\Mac Driver` e `HKCU\Software\Wine\AppDefaults\<app.exe>\Mac Driver`, funzione `get_config_key`, `dlls/winemac.drv/macdrv_main.c:234`); `Decorated` vale anche per app (`dlls/win32u/sysparams.c:6089`). Il tema uxtheme invece è per utente (quindi per prefisso). Proposta per Barrique:

- **Globali, sempre**: sincronizzazione chiaro/scuro e accento, cursori, appunti, badge Dock, notifiche.
- **Globale ma disattivabile per app**: tema msstyles "Barrique" in stile macOS 26/27 (un'app che lo rende male → `SetWindowTheme`/disattivazione; in Wine il modo più semplice è un prefisso o una chiave per app che il lato Windows legge prima di caricare il tema — oggi non esiste, va aggiunta in uxtheme), mappatura Cmd ibrida, dialoghi nativi.
- **Solo su richiesta per app**: menu globali, barra del titolo unificata, sostituzione font Mac.

### Cosa si rompe

- **Giochi**: nessun beneficio (schermo intero, tutto disegnato in D3D/Vulkan). Il tema non li tocca; la mappatura Cmd→Ctrl può dare fastidio (Cmd usato come tasto). I giochi vanno esclusi da tutto tranne che dalla cornice nativa che c'è già.
- **App che disegnano tutto da sé** (Electron, Qt con stile proprio, Delphi/VCL con skin, Java Swing, WPF, Office): il tema msstyles non le cambia o le cambia a metà (WPF legge alcuni colori di sistema → effetti imprevisti con colori scuri). Qt e Electron leggono `AppsUseLightTheme`: diventano scure correttamente, è un vantaggio.
- **Colori di sistema scuri con controlli chiari**: se si scrivono colori scuri (`COLOR_WINDOW` nero) ma il tema resta chiaro, si ottiene testo nero su nero in vecchie app che mescolano. Per questo il modo scuro va acceso **solo** quando esiste il tema scuro coerente (§6, passo M2), non prima.
- **Dialoghi nativi**: app che si aspettano messaggi dal dialogo (`CDN_*`, hook) o che leggono il risultato in modi strani (`lpstrFileTitle`, buffer multi-selezione). Il fallback automatico deve essere prudente.
- **Menu globali**: owner-draw, aggiornamenti dinamici, scorciatoie Alt, app MDI. Rischio alto.

### Costo in prestazioni

- Tema msstyles: costo uguale ad `aero` (stesso motore, immagini simili). Immagini a 192 DPI più grandi in memoria, trascurabili.
- Sincronizzazione aspetto: solo su evento, nulla.
- Dialoghi/menu nativi: un giro host↔VM per apertura; il punto critico è `WM_INITMENUPOPUP` sincrono mentre AppKit aspetta, che con la VM di mezzo potrebbe rendere lenta l'apertura dei menu. Da misurare.

---

## 6. Piano in ordine di convenienza

Stime per una persona che conosce già Barrique; incerte, servono a ordinare non a promettere.

### Vittorie rapide (solo registro/configurazione, 1–3 giorni)

| # | Cosa | Dove | Rischio |
|---|---|---|---|
| R1 | `AppsUseLightTheme`/`SystemUsesLightTheme` scritti dal launcher in base a `AppleInterfaceStyle` all'avvio | launcher `.app` + `prefix-defaults.reg` | Basso: oggi cambia solo le app che leggono il valore (Electron, Qt, WinUI) |
| R2 | Colori di selezione dal colore d'accento macOS (`Hilight`, `MenuHilight`, `HotTrackingColor`) scritti all'avvio | launcher, `HKCU\Control Panel\Colors` | Basso (solo colori chiari di selezione) |
| R3 | `LeftCommandIsCtrl=y` come opzione proposta (non default) finché non c'è la modalità ibrida | `Mac Driver` | Medio: si perde Cmd-Q |
| R4 | Font menu/titoli → Helvetica Neue o Inter via `Replacements` solo per `NONCLIENTMETRICS`, come prova | registro | Basso-medio (testi tagliati) |
| R5 | Verificare che `UsePreciseScrolling`, `Decorated`, `RetinaMode` siano come vogliamo nel prefisso della `.app` | `prefix-defaults.reg` | Nullo |
| R6 | Confermare con una prova che la cornice è Liquid Glass (build con SDK 27) e controllare gli angoli arrotondati sul contenuto | prova manuale | Nullo |

### Lavori medi (1–4 settimane ciascuno)

| # | Cosa | Stima | Rischio |
|---|---|---|---|
| M1 | Tema `barrique.msstyles` chiaro: pulsanti, caselle, radio, combo, campi, schede, barre di scorrimento, progressi; SVG nostri, 192 DPI | 2–4 settimane (soprattutto disegno) | Basso tecnico, medio legale se si copia troppo da vicino |
| M2 | Variante scura nello stesso msstyles + colori di sistema scuri coerenti | +1–2 settimane | Medio (app vecchie che mescolano colori) |
| M3 | Sincronizzazione a caldo: evento winemac su cambio aspetto/accento → registro + `WM_SETTINGCHANGE "ImmersiveColorSet"` + `WM_THEMECHANGED` | 1 settimana | Basso-medio |
| M4 | Dock: `SetProgressValue`/`SetOverlayIcon` → `NSDockTile`; balloon → `UNUserNotificationCenter` | 1 settimana | Basso |
| M5 | Tastiera Cmd ibrida (Cmd+lettera → Ctrl, scorciatoie di sistema lasciate a macOS) | 1 settimana | Medio (giochi e app che usano il tasto Windows) |
| M6 | `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` → `NSAppearance` della finestra | 2–3 giorni | Basso |

### Lavori grandi (1–3 mesi ciascuno)

| # | Cosa | Stima | Rischio |
|---|---|---|---|
| G1 | `NSOpenPanel`/`NSSavePanel` per i dialoghi comdlg32 non personalizzati, con fallback | 4–6 settimane | Medio-alto (copertura, percorsi, drive) |
| G2 | Menu `HMENU` nella barra dei menu macOS, per app su richiesta | 6–10 settimane | Alto (owner-draw, aggiornamenti sincroni, latenza VM) |
| G3 | Semafori per finestre con barra del titolo personalizzata (Electron/Chrome/Office) | 3–5 settimane | Alto (doppi pulsanti, hit-test) |
| G4 | Trascinamento da app Windows verso Finder | 2–3 settimane | Medio |
| G5 | Materiali Windows 11 (Mica/Acrylic via `DwmSetWindowAttribute`) → `NSGlassEffectView`/`NSVisualEffectView` dietro la finestra | 3–5 settimane | Medio-alto (alfa nella superficie dalla VM, poche app lo usano) |

### Ordine consigliato

R1, R2, R5 subito (pochi giorni, nessun rischio). Poi M1 + M2 + M3 insieme, perché il modo scuro ha senso solo con un tema scuro coerente. In parallelo M4 e M6 (piccoli, visibili). M5 dopo aver deciso il comportamento da tastiera con l'utente. G1 è il grande lavoro con più valore percepito; G2 e G3 solo se un'app importante per il prodotto lo richiede.

### Rischi trasversali

- **Mantenimento delle patch Wine**: M3, M5, M6, G1, G2 toccano winemac/comdlg32/uxtheme. Vanno tenute come patch in `Barrique/patches` e ribasate a ogni versione di Wine; più sono piccole e isolate, meglio è. Il tema msstyles (M1/M2) invece può stare tutto fuori dal tree Wine.
- **Legale**: niente risorse Apple nella `.app` né copiate nel prefisso; niente SF Pro distribuito.
- **Aspettative**: il risultato è "app Windows ben integrata", non "app Mac". I layout dei dialoghi restano Windows.

---

## Fonti

Codice (Wine 11.22, `~/Work/Personale/wine/src`): citato riga per riga nel testo. Principali: `dlls/winemac.drv/window.c`, `cocoa_window.m`, `cocoa_app.m`, `macdrv_main.c`, `clipboard.c`, `mouse.c`; `dlls/win32u/window.c`, `sysparams.c`, `font.c`, `freetype.c`; `dlls/uxtheme/system.c`; `dlls/aero.msstyles/aero.rc`; `dlls/comdlg32/filedlg.c`, `itemdlg.c`; `dlls/explorerframe/taskbarlist.c`; `dlls/dwmapi/dwmapi_main.c`; `programs/winecfg/theme.c`; `programs/explorer/systray.c`; `loader/wine.inf.in`. Barrique: `scripts/prefix-defaults.reg`, `ROADMAP.md`.

Esterne:
- Whisky archiviato: https://github.com/Whisky-App/whisky , https://www.techspot.com/news/107499-whisky-popular-wine-frontend-mac-gamers-no-more.html
- CrossOver, modo scuro: https://www.ifun.de/crossover-21-unterstuezt-dark-mode-und-neue-game-controller-173755/ , https://www.codeweavers.com/support/forums/general?msg=239945&t=27
- Game Porting Toolkit: https://developer.apple.com/games/game-porting-toolkit/
- Licenza font Apple / SF Pro: https://developer.apple.com/fonts/ , https://developer.apple.com/forums/thread/727961 , https://en.wikipedia.org/wiki/San_Francisco_(sans-serif_typeface)
- Apple Design Resources: https://developer.apple.com/design/resources/
- Liquid Glass e SDK: https://developer.apple.com/forums/thread/788573 , https://github.com/python/cpython/issues/139404 , https://developer.apple.com/documentation/updates/appkit , https://developer.apple.com/documentation/appkit/nsglasseffectcontainerview

Non verificato in questo studio (da controllare prima di lavorarci): se CoreText espone `.SF NS` a Wine; se winemac gestisce `FlashWindowEx` col Dock; cosa fanno esattamente CrossOver 26 e Porting Kit sull'aspetto dei controlli (nessuna documentazione trovata).

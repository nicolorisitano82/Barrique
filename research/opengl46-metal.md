# OpenGL 4.6 per i programmi Windows sotto Barrique: studio di fattibilità

Data: 2026-11-17. Ambito: sostituire, per i programmi Windows, l'OpenGL di Apple (2.1 legacy / 4.1 core via CGL) che oggi usa `winemac.drv`. Nessun download, build o esecuzione: solo lettura di sorgenti locali e pagine web. Ogni affermazione ha una fonte (URL o `file:riga`); dove le fonti si contraddicono lo dico.

## Sintesi

- **Raccomandazione: Mesa Zink sopra KosmicKrisp (Vulkan su Metal di LunarG, in Mesa), agganciato al percorso EGL già presente in win32u, dietro un interruttore, con CGL che resta il default.** È l'unica strada tutta MIT/LGPL-compatibile, mantenuta a monte, e con un driver Vulkan conforme 1.4. **Però oggi non dà GL 4.6 "vero":** KosmicKrisp a monte non espone `geometryShader` né `VK_EXT_transform_feedback`, quindi Zink si ferma a GL 2.1/3.1 e arriva a 4.6 solo con `MESA_GL_VERSION_OVERRIDE` (shader che falliscono). Il primo passo è quindi una **sonda di 1-2 giorni** che misura cosa espone oggi KosmicKrisp; il resto (2-4 settimane in Wine) si fa solo se la sonda passa o se si accetta di partire da un fork di KosmicKrisp che aggiunge geometry shader e transform feedback.
- **Zink su MoltenVK 1.4.2 (il nostro) è da scartare:** nel sorgente mancano geometry shader, transform feedback, custom border color, conditional rendering, `draw_indirect_count`; il risultato non supera ciò che Apple già dà (4.1 core).
- **WineMetalGL (MetalSharp)** è oggi il prodotto più completo (dichiara GL 4.6 core e compatibility, CTS quasi completo), ma è un'implementazione propria su Metal: rifarla è un lavoro di mesi. Si usano solo le idee. Nota: il suo `LICENSE` oggi dice **LGPL-3.0-or-later**, non AGPL (AGPL è il materiale di MetalSharp-Wine). Resta valida la regola "solo idee".
- **MGL** (Apache-2.0) è un prototipo incompleto senza geometry shader; **ANGLE** su Metal è solo GLES: non servono a dare GL desktop 4.6 a `opengl32`.
- **Il vincolo HVF è il punto tecnico più delicato:** sotto HVF i buffer mappati devono stare in memoria Windows. Oggi `opengl32` li copia e **non supporta `GL_MAP_PERSISTENT_BIT`** (`dlls/opengl32/unix_wgl.c:1691-1694`), che è il cuore dei programmi GL 4.4+. Con Zink la soluzione naturale è `GL_MESA_map_buffer_client_pointer` (già usato da Wine), ma l'implementazione di Zink si basa su `VK_EXT_map_memory_placed`, che MoltenVK non ha e che sotto HVF non servirebbe (la memoria Metal non si può mappare nella VM). Va scritta una variante che importa memoria Windows con `VK_EXT_external_memory_host`, come fa già il nostro win32u per Vulkan.

## 0. Situazione attuale in Barrique

- `winemac.drv` prova in ordine i profili CGL Legacy, GL3 Core, GL4 Core e si ferma al massimo che Apple offre (`dlls/winemac.drv/opengl.c:1089`, `:1111`, `:1119-1125`). Apple non va oltre 4.1 core; il profilo legacy (quello dei programmi che non chiedono un contesto core) è 2.1.
- Il driver si aggancia a win32u tramite `macdrv_OpenGLInit`, che carica `OpenGL.framework` e restituisce `macdrv_driver_funcs` (`dlls/winemac.drv/opengl.c:2080-2097`, `:2266-2277`).
- Conseguenze già misurate: il test opengl32 si ferma su `glGetFramebufferParameteriv` (GL 4.3), che nel profilo legacy è NULL, e wined3d crea solo device di livello 9.x (`Barrique/FASE5-AV.md:43`). D3D10 BGRA non si raggiunge né con GL 2.1 né con il renderer Vulkan su MoltenVK (`Barrique/FASE5-AV.md:267`). D3D10/11 a 32 bit è fuori perché DXMT non funziona a 32 bit sotto HVF (`Barrique/ROADMAP.md:145`). **Un GL 4.x completo darebbe a wined3d i livelli 10/11 anche ai programmi a 32 bit:** è un beneficio collaterale importante.
- In roadmap la voce è già prevista come "[MS] OpenGL 4.6 su Metal (WineMetalGL)" (`Barrique/ROADMAP.md:151`; `Barrique/FASE5-AV.md:304`).

## 1. Mesa Zink sopra MoltenVK

**Requisiti di Zink per versione GL** (da [docs.mesa3d.org/drivers/zink.html](https://docs.mesa3d.org/drivers/zink.html)):
- GL 2.1: `logicOp`, `fillModeNonSolid`, `alphaToOne`, `shaderClipDistance`, `independentBlend`, più `VK_EXT_custom_border_color`, `VK_EXT_provoking_vertex`, `VK_EXT_line_rasterization` (con linee rettangolari, Bresenham, smooth e stippled), `VK_EXT_border_color_swizzle`, `VK_KHR_dynamic_rendering`, `VK_KHR_maintenance5` e altre.
- GL 3.0: `VK_EXT_transform_feedback`, `VK_EXT_conditional_rendering`.
- GL 3.2: `geometryShader`, `depthClamp`, `VK_EXT_depth_clip_enable`.
- GL 3.3: `dualSrcBlend`, divisori degli attributi.
- GL 4.0: `tessellationShader`, `sampleRateShading`, `imageCubeArray`.
- GL 4.1: `multiViewport` e limiti delle texture.
- GL 4.2: `VK_EXT_image_2d_view_of_3d`, store e atomici nei vertex/fragment shader.
- GL 4.3: `robustBufferAccess`, `robustImageAccess2`.
- GL 4.5: `shaderCullDistance`.
- GL 4.6: `samplerAnisotropy`, `depthBiasClamp`, `VK_KHR_draw_indirect_count`, voto dei subgroup.
- La stessa pagina dice che Zink su macOS è "experimental with very limited capabilities" e che si compila con `-Dgallium-drivers=zink` e `-Dmoltenvk-dir`.

**Cosa ha il nostro MoltenVK 1.4.2** (sorgente in `~/Work/Personale/wine/deps-src/moltenvk-1.4.2`, compilato da `Barrique/scripts/build-moltenvk.sh`):
- `geometryShader`: non viene mai impostato in `MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm` (0 occorrenze), quindi resta falso.
- `tessellationShader`: vero (`MVKDevice.mm:2803`, `:2823`); `multiViewport` vero (`:2813`); `dualSrcBlend`, `depthClamp` veri (`:2790-2791`).
- `logicOp`: solo con le API private di Metal (`MVKDevice.mm:2765`).
- `shaderCullDistance`: assente.
- Linee: solo Bresenham, niente rettangolari, smooth o stippled (`MVKDevice.mm:255-260`).
- Estensioni assenti da `MoltenVK/MoltenVK/Layers/MVKExtensions.def`: `VK_EXT_transform_feedback`, `VK_EXT_custom_border_color`, `VK_EXT_border_color_swizzle`, `VK_EXT_conditional_rendering`, `VK_EXT_depth_clip_enable` (arriva in 1.4.3 secondo [Whats_New](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/Whats_New.md)), `VK_EXT_map_memory_placed`.
- `VK_KHR_draw_indirect_count` è marcata `MVK_NA` (non disponibile) (`MVKExtensions.def:57`).
- Presenti: `VK_EXT_external_memory_host` (`:143`), `VK_EXT_external_memory_metal` (`:144`), `VK_KHR_map_memory2` (`:88`).
- Transform feedback è chiesto dal 2022 e mai fatto ([MoltenVK#1588](https://github.com/KhronosGroup/MoltenVK/issues/1588)).

**GL raggiungibile:** in teoria 2.1 con difetti (mancano custom border color, logicOp e linee stippled), senza 3.0 perché manca transform feedback. Le prove pratiche concordano:
- La decisione 0001 di WineMetalGL registra che con Zink su MoltenVK, sotto Wine, `glcts.exe` non riusciva a creare contesti GL 3.0-3.3. Forzando 3.3 la prima query dava `GL_OUT_OF_MEMORY`, e Zink segnalava l'assenza di `logicOp` e di `EXT_custom_border_color` ([0001-gl-implementation-strategy.md](https://github.com/metalsharp/WineMetalGL/blob/main/docs/decisions/0001-gl-implementation-strategy.md)). Quella prova usava il MoltenVK di MetalSharp, che dicono esporre transform feedback: non è il nostro.
- Un rapporto UTM (M3 Pro, macOS 26.7.1, MoltenVK 1.4.2, via Venus) indica Zink desktop GL 3.1 su MoltenVK ([joshan-kana/UTM#22](https://github.com/joshan-kana/UTM/issues/22)). Lo stesso rapporto elenca `geometryShader` e transform feedback come presenti su MoltenVK, il che contraddice il sorgente 1.4.2 qui sopra. Probabilmente usano un MoltenVK modificato o lo strato Venus riporta le funzioni a modo suo: non verificato.

**Mesa su macOS:** compila "without modifications" e su macOS funzionano solo i driver software e quelli che inoltrano ad altre API ([docs.mesa3d.org/macos.html](https://docs.mesa3d.org/macos.html)). EGL con la piattaforma surfaceless si compila su macOS (`-Degl=enabled -Degl-native-platform=surfaceless -Dplatforms=macos`, [gist di lucamignatti](https://gist.github.com/lucamignatti/5312f5e937de2ba44256ecba6de54cc2)). Il supporto alle finestre però non c'è a monte: l'autore del gist l'ha aggiunto a mano. Una MR per abilitare DRI su Darwin per Zink+EGL (MR 41359) era in corso a maggio 2026 ([mesa-dev](http://www.mail-archive.com/mesa-dev@lists.freedesktop.org/msg225151.html)); non ho potuto verificarne lo stato perché gitlab.freedesktop.org blocca i fetch automatici.

**Verdetto:** no. Darebbe meno di quello che Apple dà già con 4.1 core.

## 2. Mesa KosmicKrisp + Zink

**Stato di KosmicKrisp:**
- È un driver Vulkan di Mesa su Metal 4. Richiede Apple Silicon e macOS 26+ (va bene per Barrique). Shader NIR → MSL, niente SPIRV-Cross. Licenza MIT. ([docs.mesa3d.org/drivers/kosmickrisp.html](https://docs.mesa3d.org/drivers/kosmickrisp.html), [LunarG](https://www.lunarg.com/kosmickrisp-achieves-vulkan-1-4-conformance-on-apple-silicon/))
- **Conforme Vulkan 1.4** (in precedenza 1.3). Prestazioni circa triplicate dalla conformità 1.3. Tessellation, Robustness2 e multi-draw aggiunti ([LunarG](https://www.lunarg.com/kosmickrisp-achieves-vulkan-1-4-conformance-on-apple-silicon/)). Le date nelle fonti sono incoerenti (settembre 2025 o settembre 2026, [Neoteo](https://www.neoteo.com/en/rpcs3-adds-kosmickrisp-to-mac-builds-with-vulkan-14)).
- Il build documentato disattiva OpenGL (`-Dopengl=false -Dgallium-drivers=`): Zink non è un obiettivo dichiarato di LunarG ([docs Mesa](https://docs.mesa3d.org/drivers/kosmickrisp.html), [XDC 2025](https://www.lunarg.com/lunarg-at-xdc-2025-kosmickrisp-overview/)).

**Funzioni che contano per Zink (fonti indirette, da verificare con la sonda del passo 0):**
- La decisione 0001 di WineMetalGL: "KosmicKrisp also lacks `geometryShader`", quindi né MoltenVK né KosmicKrisp possono dare GL 3.2/3.3 via Zink ([0001](https://github.com/metalsharp/WineMetalGL/blob/main/docs/decisions/0001-gl-implementation-strategy.md)).
- Rapporto UTM (novembre 2026, Mesa 26.2.6): su KosmicKrisp `geometryShader` falso, `VK_EXT_transform_feedback` assente, `fillModeNonSolid` falso. Ci sono invece `logicOp`, `VK_KHR_draw_indirect_count` e `VK_EXT_depth_clip_enable`. Zink desktop GL: **2.1** ([UTM#22](https://github.com/joshan-kana/UTM/issues/22)).
- Esperienze reali: Minecraft via Zink+KosmicKrisp riporta 4.6 **solo forzando `MESA_GL_VERSION_OVERRIDE=4.6`**. Non tutti gli shader funzionano ("kosmickrisp doesnt expose all needed features"), manca `GL_MAX_TRANSFORM_FEEDBACK_BUFFERS`, e le prestazioni sono inferiori (25 contro 100 fps in un caso) ([gist](https://gist.github.com/lucamignatti/5312f5e937de2ba44256ecba6de54cc2)).
- Uno sviluppatore che usa Zink+KosmicKrisp come backend GL (M4 Max) dice che i contesti 4.6 sono "usable end to end". Segnala però un bug di KosmicKrisp: una pipeline con una sola topologia dà errori grafici quando si mescolano punti, linee e triangoli. Propone una patch di circa 80 righe ([mesa-dev, 2026-05-06](http://www.mail-archive.com/mesa-dev@lists.freedesktop.org/msg225151.html)).
- Un fork MIT, [procrastinine/maclauncher-kosmickrisp](https://github.com/procrastinine/maclauncher-kosmickrisp), aggiunge per emulazione transform feedback con stream, geometry, tessellation e mesh shader, fp64 e altro. Non è conforme di suo, non ha release, ha 0 stelle. Una patch su un altro fork implementa transform feedback in Zink/KosmicKrisp emulandolo con geometry shader ([athei/mesa#1](https://github.com/athei/mesa/pull/1)). È un segnale che il lavoro esiste, ma non è a monte.

**GL raggiungibile:** oggi, a monte, 2.1-3.1 dichiarati. 4.6 si ottiene con l'override ma non è affidabile (shader con geometry shader o transform feedback falliscono). Se KosmicKrisp a monte (o il fork) espone `geometryShader` e `VK_EXT_transform_feedback`, l'elenco di Zink si chiude: degli altri requisiti 4.x, KosmicKrisp ha già `draw_indirect_count` e `depth_clip_enable`. Il resto (cull distance, linee stippled, custom border color) è da verificare con la sonda. Custom border color è dietro `MESA_KK_EXPERIMENTAL` ([docs Mesa](https://docs.mesa3d.org/drivers/kosmickrisp.html)).

**Verdetto:** è la base giusta, ma l'esito dipende da geometry shader e transform feedback in KosmicKrisp: da misurare subito.

## 3. Altri strati GL-su-Metal

| Progetto | Cosa dà | Licenza | Utilità per noi |
|---|---|---|---|
| **MGL** ([openglonmetal/MGL](https://github.com/openglonmetal/MGL)) | Punta a GL 4.6 core (più una libreria ES). GLSL → SPIR-V → MSL. Stato GL tracciato e mappato su oggetti Metal a ogni draw. **Niente geometry shader**, molte funzioni sono stub con `assert(0)`, la conformità non era un obiettivo | Apache-2.0 | Solo riferimento. Non è una base per un prodotto |
| **ANGLE, backend Metal** ([README](https://chromium.googlesource.com/angle/angle/+/HEAD/README.md)) | Solo **OpenGL ES** (2.0 completo; 3.0 secondo la decisione 0001 di WineMetalGL). Il backend GL desktop di ANGLE su Mac è deprecato e comunque fa il contrario (ES sopra GL) | BSD-3-Clause | Non dà GL desktop a `opengl32`. Si possono studiare soluzioni puntuali (provoking vertex, conversione degli indici, emulazione di transform feedback), come fa WineMetalGL ([0001](https://github.com/metalsharp/WineMetalGL/blob/main/docs/decisions/0001-gl-implementation-strategy.md)) |
| **WineMetalGL** ([metalsharp/WineMetalGL](https://github.com/metalsharp/WineMetalGL)) | Implementazione GL propria su Metal: dichiara GL 1.0-4.6 e GLES 2-3.2, con percentuali CTS dal 99,88% al 100% per versione | `LICENSE`: LGPL-3.0-or-later (fino alla 2.1.0 MIT). MetalSharp-Wine è AGPL-3.0 per il proprio materiale | Solo idee (vincolo dell'utente) |

**Come funziona WineMetalGL, ad alto livello (solo idee, nessun codice copiato):**
- Un core `libwinemetalgl.dylib` senza dipendenza da OpenGL.framework. Compila il GLSL con glslang + SPIRV-Cross verso MSL e implementa da sé lo stato GL e le risorse. Un modulo Wine (`winemetalgl.dll`/`.so`) riceve le chiamate WGL/GL da un `opengl32.dll` di inoltro messo nel prefisso, attraverso una ABI propria (`include/winemetalgl/driver.h`). **Wine non viene modificato** ([README](https://github.com/metalsharp/WineMetalGL)).
- **Geometry shader** emulati con kernel compute. **Tessellation:** la fase di controllo gira in kernel, i punti del dominio sono generati dalla CPU. **Transform feedback:** un passaggio vertex separato prima del draw, con clamp allo spazio residuo e segnalazione di overflow. Il SPIR-V viene riportato a GLSL per avere un solo percorso di compilazione. Varianti degli shader per chiave di pipeline, compilate al primo uso. DSA ottenuto scambiando temporaneamente il binding. `draw_indirect_count` legge il conteggio sulla CPU ([0002-gl4-architecture.md](https://github.com/metalsharp/WineMetalGL/blob/main/docs/decisions/0002-gl4-architecture.md)).
- fp64 è emulato in software con interi a 64 bit, perché Metal non ha double. Mancano parti del profilo compatibility: accumulation buffer, `glRenderMode`, evaluator, line stipple ignorato. Nota contraddittoria: la stessa pagina dice anche che i contesti si fermano a 4.3 ([unsupported.md](https://github.com/metalsharp/WineMetalGL/blob/main/docs/unsupported.md)).
- Hanno scartato Zink su MoltenVK e Zink su KosmicKrisp per l'assenza di geometry shader, con l'impegno di ripetere la sonda a ogni tappa ([0001](https://github.com/metalsharp/WineMetalGL/blob/main/docs/decisions/0001-gl-implementation-strategy.md)). È la stessa sonda che propongo al passo 0.
- Gira in processi arm64 nativi senza VM ([MetalSharp-Wine](https://github.com/metalsharp/MetalSharp-Wine)). Le loro scelte sui buffer mappati non tengono conto del vincolo HVF descritto al punto 4.

**Altre strade considerate:**
- llvmpipe (software, Mesa, compila su macOS secondo [docs.mesa3d.org/macos.html](https://docs.mesa3d.org/macos.html)) può servire come ripiego di correttezza o per i test. La versione GL esatta che espone non è verificata qui.
- Una nostra implementazione GL su Metal scritta da zero (nello stile di WineMetalGL) è possibile per licenza, ma è un lavoro di molti mesi.

## 4. Come si aggancia a Wine su macOS

### 4.1 EGL o CGL

- win32u ha già un backend EGL generico: carica `SONAME_LIBEGL` con `dlopen` (`dlls/win32u/opengl.c:1668-1680`). Richiede `EGL_KHR_client_get_all_proc_addresses` ed `EGL_EXT_platform_base` (`:1707-1708`) e, sul display, `EGL_KHR_create_context`, `_no_error` e `EGL_KHR_no_config_context` (`:1752-1754`). Mesa li fornisce tutti.
- `display_funcs_init` prova prima EGL se `use_egl` è attivo (`dlls/win32u/sysparams.c:170`, `:6092`), poi chiama `pOpenGLInit` del driver, che può sostituire le funzioni con le sue (`dlls/win32u/opengl.c:3505-3510`).
- Il backend EGL di default usa `EGL_PLATFORM_SURFACELESS_MESA` (`:1257-1260`). Le sue superfici di finestra sono `framebuffer_surface` fuori schermo (`:1473-1477`). Lo swap copia nel `target` solo se il driver ne ha fornito uno (`:1146-1201`). **Senza un driver di finestra non si vede niente.**
- Il modello da seguire è winewayland: il driver fornisce solo `p_init_egl_platform`, `p_surface_create` e `p_pbuffer_create`, mentre win32u fa tutto il resto: pixel format, contesti, estensioni (`dlls/winewayland.drv/opengl.c:121-127`, `:207-212`). Lo stesso fanno winex11 (`dlls/winex11.drv/opengl.c:460`, `:572`) e wineandroid (`dlls/wineandroid.drv/opengl.c:121`).
- Per winemac quindi: in `macdrv_OpenGLInit` (`dlls/winemac.drv/opengl.c:2080`) scegliere tra CGL (oggi) e EGL/Zink (nuovo) con un interruttore, per esempio `BARRIQUE_GL=zink`. Nel ramo EGL lasciare a win32u le `egldrv_funcs` e sovrascrivere solo piattaforma, superfici e pbuffer.
- Oggi `configure.ac:1257-1265` cerca `libEGL` con pkg-config. Per macOS bisogna puntarlo al `libEGL.dylib` di Mesa messo nel bundle (`@rpath`), come già si fa con MoltenVK (`Barrique/ROADMAP.md:169`, librerie in `lib/deps` con `@rpath`).

### 4.2 Superfici di finestra (CAMetalLayer)

winemac sa già creare un `CAMetalLayer` per finestra: `macdrv_view_create_metal_view` (`dlls/winemac.drv/cocoa_window.m:3583`) e la swapchain Metal del client surface (`dlls/winemac.drv/window.c:1176`), che il driver Vulkan passa a `vkCreateMetalSurfaceEXT` (`dlls/winemac.drv/vulkan.c:44-62`). Tre modi di presentare il GL di Zink, dal più semplice al migliore:

1. **Prototipo senza patch a Mesa:** surfaceless + `framebuffer_surface` di win32u, con lettura dei pixel e copia nella finestra. È lento (una lettura per frame), ma serve per misurare la versione GL e i test di correttezza. Il percorso di lettura per i pbuffer esiste già (`dlls/win32u/opengl.c:2449`).
2. **Patch a Mesa: superficie EGL su `CAMetalLayer`.** Kopper, la gestione della swapchain di Zink, creerebbe la superficie con `VK_EXT_metal_surface` ([spec](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_metal_surface.html)) sul layer che gli passa winemac. È ciò che ha fatto l'autore del gist ("added a macOS-only path… when Kopper detects Metal support", [gist](https://gist.github.com/lucamignatti/5312f5e937de2ba44256ecba6de54cc2)). È la strada più veloce e anche quella da proporre a monte.
3. **Senza patch a Mesa ma veloce:** l'immagine di colore di Zink viene esportata in Metal (`VK_EXT_external_memory_metal`) e winemac la copia nel proprio layer. Più codice in Wine, meno in Mesa.

### 4.3 Vincolo di memoria HVF

- Sotto HVF il codice Windows vede solo memoria normale del processo. La memoria mappata di MoltenVK è memoria Metal (`VM_MEMORY_IOACCELERATOR`) e `hvf.c` la rifiuta, per questo il nostro win32u importa memoria Windows con `VK_EXT_external_memory_host` (`dlls/win32u/vulkan.c:62-67`, `:549-557`).
- Con `WINE_HVF=1` anche i processi a 64 bit prendono la strada di copia di WoW64 (`dlls/opengl32/unix_wgl.c:1203-1204`). `glMap*` passa da `wow64_map_buffer` (thunk in `dlls/opengl32/unix_thunks.c:13913-13994`) e le stringhe vengono copiate in un pool di memoria Windows (`unix_wgl.c:1824`). Cronologia in `Barrique/FASE5-AV.md:37`.
- In `wow64_map_buffer` (`unix_wgl.c:1615`) le strade sono, in ordine:
  - memoria Vulkan piazzata (`vk_memory`, `:1626-1653`). Richiede `GL_EXT_memory_object_fd` più `VK_KHR_external_memory_fd` e `VK_EXT_map_memory_placed` (`:294`, `:381-386`): su macOS non c'è;
  - memoria pinned (`GL_AMD_pinned_memory`, `:1655-1660`, `:1446-1456`);
  - copia (`:1684-1711`).
  
  **La copia rifiuta `GL_MAP_PERSISTENT_BIT`** (`:1691-1694`), quindi i buffer persistenti di `ARB_buffer_storage` (GL 4.4) falliscono sotto HVF. Per WoW64 Wine limita addirittura il contesto a 4.3 e spegne `ARB_buffer_storage` quando il driver non ha né client pointer, né Vulkan, né memoria pinned (`:479-495`).
- Wine ha già la soluzione di principio: **`GL_MESA_map_buffer_client_pointer`**. Il driver mappa i buffer dentro intervalli forniti dall'applicazione ([spec Khronos](https://registry.khronos.org/OpenGL/extensions/MESA/MESA_map_buffer_client_pointer.txt)). opengl32 li alloca con `NtAllocateVirtualMemory`, quindi in memoria Windows (`unix_wgl.c:1547-1612`), e passa il flag anche a `glBufferStorage` (`:1757`). Secondo Phoronix (ottobre 2026, citato via ricerca perché la pagina restituisce 403: [phoronix.com/news/Wine-Mesa-Map-Buffer-Client-Ptr](https://phoronix.com/news/Wine-Mesa-Map-Buffer-Client-Ptr)) Zink ha già unito il supporto all'estensione.
- **Il problema:** la bozza di Zink si basa su `VK_EXT_map_memory_placed` ([mesa-dev, ottobre 2024](https://lists.freedesktop.org/archives/mesa-dev/2024-October/226328.html)). MoltenVK non l'ha (`MVKExtensions.def`), per KosmicKrisp non è noto, e sotto HVF non basterebbe comunque, perché collocherebbe memoria Metal in un indirizzo Windows e la VM non la vede.
- **Soluzione proposta:** in Zink, per i buffer con `MAP_CLIENT_POINTER_BIT_MESA`, creare la memoria Vulkan importando le pagine dell'intervallo client con `VK_EXT_external_memory_host` (MoltenVK lo ha, `MVKExtensions.def:143`; in Metal diventa un `MTLBuffer` senza copia). È la stessa strategia che win32u usa per Vulkan (passo 4 della fase 5). In alternativa si implementa `GL_AMD_pinned_memory` in Zink con lo stesso meccanismo: opengl32 lo usa già (`unix_wgl.c:1446-1456`).
- Il vincolo vale anche per i puntatori GL → Windows (`glGetString`, `glGetBufferPointerv`): sono già gestiti da `hvf_string` e `hvf_glGet*BufferPointerv` (`unix_wgl.c:1824`, `:1868-1890`) e non dipendono dal backend.

## 5. Classifica, stima, rischi, licenze

### Classifica

1. **Zink + KosmicKrisp a monte, via EGL in winemac.** Licenze tutte MIT e compatibili con LGPL, e mantenuto da altri. Bloccante: geometry shader e transform feedback in KosmicKrisp.
2. **Zink + un fork di KosmicKrisp con geometry shader e transform feedback emulati** ([maclauncher-kosmickrisp](https://github.com/procrastinine/maclauncher-kosmickrisp), MIT). Stessa integrazione in Wine; il rischio si sposta su un fork piccolo e non conforme. Da usare come ripiego se il punto 1 resta a 2.1/3.1.
3. **Implementazione GL propria su Metal**, scritta da zero con le idee di WineMetalGL e MGL. Controllo totale, ma richiede mesi o anni-persona.
4. **Zink + MoltenVK:** scartato (GL ≤ 2.1, peggio di CGL 4.1).
5. **MGL, ANGLE:** scartati come base.

### Stima (opzione 1 o 2)

| Passo | Lavoro | Stima |
|---|---|---|
| 0. Sonda | Build di Mesa (Zink, KosmicKrisp, EGL surfaceless) fuori da Wine. `vulkaninfo` su KosmicKrisp per `geometryShader`, transform feedback, `external_memory_host`, `map_memory_placed`, custom border color, cull distance e linee stippled. `eglinfo`/`glxinfo`-like per la versione GL che Zink espone senza override | 1-2 giorni |
| 1. winemac EGL | Interruttore, `p_init_egl_platform`, `p_surface_create`/`p_pbuffer_create` con presentazione 4.2/1, `configure` per `libEGL` nel bundle | 3-5 giorni |
| 2. Presentazione vera | 4.2/2 (Kopper + `VK_EXT_metal_surface`, patch a Mesa) oppure 4.2/3 | 1-2 settimane |
| 3. Memoria HVF | Client pointer o memoria pinned in Zink con `VK_EXT_external_memory_host`; buffer persistenti; prova a 64 e 32 bit | 1-2 settimane |
| 4. Bundle e test | `libEGL`, `libgallium`, ICD di KosmicKrisp in `.app`; test opengl32 e wined3d (livelli D3D10/11), gears GL, un gioco GL 4.x | 1 settimana |
| Geometry shader e transform feedback in KosmicKrisp | Solo se non arrivano a monte né dal fork: lavoro in Mesa, non in Wine | settimane o mesi, fuori dal nostro controllo |

Totale lato Wine e Mesa-integrazione: **circa 4-6 settimane**, se la sonda è positiva.

### Rischi

- **Funzioni di KosmicKrisp:** senza geometry shader e transform feedback, niente GL ≥ 3.2 reale ([UTM#22](https://github.com/joshan-kana/UTM/issues/22), [0001](https://github.com/metalsharp/WineMetalGL/blob/main/docs/decisions/0001-gl-implementation-strategy.md)).
- **Prestazioni:** Zink su un driver Vulkan che traduce in Metal significa due livelli. Un caso riportato va a 25 fps contro 100 ([gist](https://gist.github.com/lucamignatti/5312f5e937de2ba44256ecba6de54cc2)). Va confrontato con CGL 4.1 sui nostri gears (`Barrique/FASE5-AV.md:283`).
- **Maturità:** Zink su macOS è "experimental" ([docs Zink](https://docs.mesa3d.org/drivers/zink.html)). EGL con finestre su macOS non è a monte, e ci sono bug noti di KosmicKrisp con le topologie ([mesa-dev](http://www.mail-archive.com/mesa-dev@lists.freedesktop.org/msg225151.html)).
- **Due driver Vulkan nello stesso processo:** winevulkan usa MoltenVK senza loader (`Barrique/ROADMAP.md:142`), Zink userebbe KosmicKrisp. Zink va collegato direttamente all'ICD di KosmicKrisp (niente loader), oppure si passa tutto a KosmicKrisp: decisione separata.
- **Dimensione del bundle:** Mesa con LLVM e libclc (necessari al build di KosmicKrisp, [docs](https://docs.mesa3d.org/drivers/kosmickrisp.html)). Da misurare quanto finisce nel runtime.
- **Profilo compatibility:** Zink lo supporta (mesamatrix lo dà a 4.6, [mesamatrix.net](https://mesamatrix.net/)), ma i programmi vecchi che usano GL legacy oggi funzionano bene su CGL 2.1. L'interruttore deve restare per programma.

### Licenze e politiche

- Mesa e KosmicKrisp: MIT ([LunarG](https://www.lunarg.com/kosmickrisp-achieves-vulkan-1-4-conformance-on-apple-silicon/)), compatibili con Wine LGPL e con la distribuzione nella `.app`. MoltenVK: Apache-2.0 (`Barrique/scripts/build-moltenvk.sh:2`). MGL: Apache-2.0. ANGLE: BSD-3.
- WineMetalGL: LGPL-3.0-or-later secondo il `LICENSE` attuale ([repo](https://github.com/metalsharp/WineMetalGL)). MetalSharp-Wine: AGPL-3.0 ([repo](https://github.com/metalsharp/MetalSharp-Wine)). Indicazioni dell'utente: solo idee, niente codice.
- Contributi IA: FEX e DXMT li vietano, ma questo piano non li tocca. **Mesa** accetta contributi assistiti da IA solo con dichiarazione esplicita e un autore umano che risponde del codice; i revisori possono rifiutarli e i messaggi di commit generati sono vietati ([Phoronix](https://www.phoronix.com/news/mesa-Contributor-Guidelines), [Phoronix](https://phoronix.com/news/mesa-two-gen-ai-policies); fonti secondarie, da verificare sulle linee guida ufficiali). Conclusione: le patch a Mesa (Kopper su Metal, client pointer con `external_memory_host`) restano patch nostre in `Barrique/patches`, e si propongono a monte solo se l'utente le fa sue.

## Raccomandazione e primi passi concreti

1. **Sonda (passo 0), prima di scrivere codice in Wine:**
   - compilare Mesa con `-Dplatforms=macos -Dvulkan-drivers=kosmickrisp -Dgallium-drivers=zink -Degl=enabled -Degl-native-platform=surfaceless -Dglx=disabled -Dgbm=disabled`, un'unione dei comandi delle [docs di KosmicKrisp](https://docs.mesa3d.org/drivers/kosmickrisp.html) e del [gist](https://gist.github.com/lucamignatti/5312f5e937de2ba44256ecba6de54cc2);
   - con `vulkaninfo` su KosmicKrisp verificare `geometryShader`, `VK_EXT_transform_feedback`, `VK_EXT_external_memory_host`, `VK_EXT_map_memory_placed`, `VK_EXT_custom_border_color`, `shaderCullDistance` e le linee stippled;
   - con un piccolo programma EGL surfaceless leggere `GL_VERSION` senza override e con `ZINK_DEBUG`/`MESA_DEBUG` annotare i requisiti mancanti;
   - ripetere con il fork maclauncher-kosmickrisp.
   - **Cancello:** almeno GL 3.3 dichiarato senza override a monte, oppure GL 4.x con il fork.
2. **Se il cancello passa,** in winemac aggiungere il ramo EGL (presentazione per lettura dei pixel) dietro `BARRIQUE_GL=zink`, e far girare il test opengl32 e i gears su aarch64, x86_64 e i686. È il punto in cui si scopre davvero cosa succede ai buffer sotto HVF.
3. **Memoria HVF:** patch a Zink per client pointer o memoria pinned con `VK_EXT_external_memory_host`. Verificare `GL_MAP_PERSISTENT_BIT` e che il limite a 4.3 di WoW64 (`unix_wgl.c:479-495`) non scatti più.
4. **Presentazione con `CAMetalLayer`** (Kopper + `VK_EXT_metal_surface`), poi bundle e test di wined3d per D3D10/11 a 32 bit.
5. **Se il cancello non passa:** tenere CGL, ripetere la sonda a ogni release di Mesa (come fa WineMetalGL) e decidere solo allora tra il fork e un'implementazione propria.

## Esito della prova (2026-11-18)

Mesa **26.3.1** compilata fuori da Wine (`~/Work/Personale/wine/deps-mesa-probe`) con KosmicKrisp e Zink, EGL surfaceless; programma di prova `research/opengl-probe/glprobe.c`.

**Soglia non superata.** KosmicKrisp 26.3.1 (tabella delle funzioni in `src/kosmickrisp/vulkan/kk_physical_device.c`) non ha **transform feedback** né **geometry shader**; la documentazione di Zink (`docs/drivers/zink.rst`) li richiede rispettivamente per GL 3.0 e GL 3.2. Zink su KosmicKrisp si ferma quindi a **OpenGL 2.1**, sotto il GL 4.1 di Apple che Barrique usa oggi. Decisione: restare su CGL, riprovare a ogni release di Mesa (o valutare il fork MIT che li emula).

Ha già invece, ed è utile per HVF: `VK_EXT_external_memory_host`, `VK_EXT_map_memory_placed`, tessellation, `logicOp`, multi-viewport, conditional rendering, draw indirect count; custom border color è sperimentale.

Note di compilazione su macOS (solo opzioni, sorgenti di Mesa non modificati):
- dipendenze da Homebrew (solo per la prova): LLVM 23, `libclc`, `spirv-llvm-translator`, `spirv-tools`, `glslang`, `bison` (quello di sistema è 2.3), `vulkan-loader`, `vulkan-tools`; `mako` e `pyyaml` in `~/Work/Personale/wine/venv-mesa`;
- `-Dmoltenvk-dir` deve contenere solo `include/MoltenVK` (con gli header Vulkan di MoltenVK nasconde quelli di Mesa);
- `-Dc_args`/`-Dcpp_args=-DVK_USE_PLATFORM_METAL_EXT -DVK_USE_PLATFORM_MACOS_MVK` (ordine delle inclusioni in Zink);
- link con `-lobjc -framework Metal -framework QuartzCore -framework IOSurface`;
- il driver KosmicKrisp così collegato va in crash all'avvio: i suoi punti d'ingresso Vulkan sono riferimenti deboli e il linker di macOS non estrae dagli archivi statici i file che li definiscono (`-Wl,-all_load` globale rompe `libgallium`). Per farlo girare servirebbe forzare il caricamento solo per il driver, nel `meson.build` di Mesa. Non necessario per questa decisione.

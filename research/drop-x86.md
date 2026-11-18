# Studio: togliere il supporto x86 a 32 bit (2026-11-18)

**Decisione dell'utente (2026-11-18): strada B, x86 a 32 bit tenuto e congelato.** Controllo rapido: `tests/i386-smoke.sh`.

Domanda: cosa comporta abbattere completamente il supporto x86 a 32 bit e tenere solo ARM64 nativo e x86-64 (ARM64EC + FEX).

## In breve

**Sconsigliato.** Il guadagno è modesto (circa 330 MB di app, un terzo dei test, una parte del codice WoW64), mentre la perdita è grande e in parte invisibile: **molti installer di programmi a 64 bit sono a 32 bit**, quindi senza x86 non si installerebbero nemmeno programmi x64. Lo abbiamo visto in questa stessa sessione: l'installer di **Firefox x64** (`Firefox Setup 160.0.1.exe`) e quello di **7-Zip x64** (`7z2301-x64.exe`) sono eseguibili `i386`.

Proposta alternativa: tenere x86 ma **congelarlo** a "basta che installi e funzioni": nessun lavoro nuovo specifico a 32 bit (come già deciso per D3D10/11), test i386 ridotti a un giro di fumo.

## Cosa costa oggi x86 a 32 bit

| Voce | Peso |
|---|---|
| DLL Wine i386 nella `.app` (`lib/wine/i386-windows`) | 222 MB |
| Gecko x86 (`share/wine/gecko/wine-gecko-2.47.4-x86`) | 104 MB |
| FEX per WoW64 (`libwow64fex.dll`) | 4,8 MB |
| Mono x86 | dentro il pacchetto Mono unico, non separabile senza rifarlo |
| Totale togliibile | **circa 330 MB** su 1,5 GB (la `.app` scenderebbe a circa 1,17 GB) |
| Test di conformità | un terzo dei giri (i386 è una delle tre architetture) |
| Compilazione | la parte i386 delle DLL PE (build `--enable-archs`), più la build dei test x86 |
| Codice nostro specifico | 8 file di `wow64`/`wow64win` toccati; 107 conversioni di puntatori a 32 bit (`ULongToPtr`/`ULongToHandle`) in opengl32, winevulkan, wow64win; correzioni WoW64 sotto HVF (fase 2: thread a 32 bit, `CreateProcess`, notifiche tra processi, scatter/gather) |
| Fallimenti aperti solo i386 | kernel32 `virtual` (`test_stack_commit`), `debugger`, `loader`, ws2_32 `afd`, user32 `class`, ole32 `dragdrop` |

**Cosa non sparirebbe:** la finestra bassa (`0xE000000000` + indirizzo, `hvf_low_ptr`). Serve a ogni indirizzo Windows sotto i 4 GB, anche nei processi a 64 bit (allocazioni con `zero_bits`, programmi x64 che chiedono memoria bassa). Quindi la parte più delicata dell'HVF resta; si toglierebbero solo le conversioni dei puntatori delle strutture a 32 bit (thunk WoW64).

## Cosa si perderebbe

1. **Installer.** Gli stub più diffusi sono a 32 bit anche per programmi a 64 bit:
   - NSIS (Firefox, molti programmi open source e giochi indie): lo stub è x86 nella grande maggioranza dei casi;
   - Inno Setup: `setup.exe` è a 32 bit anche quando installa in modalità 64 bit;
   - InstallShield e molti bootstrapper `setup.exe`;
   - azioni personalizzate degli MSI spesso in DLL a 32 bit;
   - redistributable VC++ x86, installati anche da programmi x64.
   Senza x86 questi installer non partono: bisognerebbe chiedere agli utenti di estrarre i file a mano (innoextract aiuta solo con Inno).
2. **Giochi.** Gran parte del catalogo prima del 2015 è a 32 bit: classici GOG, molti titoli Steam più vecchi, molti indie con motori a 32 bit. Il client Steam stesso è ormai a 64 bit (`steamwebhelper` lo è da tempo), ma i giochi che lancia no.
3. **Programmi.** Utilità e programmi professionali più vecchi, plugin a 32 bit, programmi .NET compilati "preferisci 32 bit", molti strumenti aziendali.
4. **HTML incorporato.** Oggi Gecko funziona solo a 32 bit (x64 va in crash sotto FEX, ARM64 non esiste): togliendo x86 si perde del tutto finché Gecko x64 non è corretto.
5. **Vantaggio competitivo.** Le soluzioni concorrenti su Mac eseguono x86 tramite Rosetta, che Apple ha annunciato di ridurre nelle prossime versioni di macOS; Barrique esegue x86 a 32 bit con FEX dentro la VM, senza Rosetta. Il supporto a 32 bit è uno dei punti in cui Barrique può durare più a lungo degli altri.

## Cosa si guadagnerebbe

- circa 330 MB in meno nella `.app`;
- giri di test più corti di un terzo e una build più corta;
- niente più correzioni WoW64 future (sono state una fonte ricorrente di bug sotto HVF: puntatori a 32 bit non convertiti in opengl32, winevulkan, wow64win, DXMT);
- meno superficie per bug come il blocco raro visto in OpenTTD x86.

## Strade possibili

| Strada | Guadagno | Perdita |
|---|---|---|
| A. Togliere x86 del tutto | -330 MB, -1/3 test, meno codice WoW64 | installer a 32 bit (anche di programmi x64), giochi e programmi a 32 bit, Gecko |
| B. **Tenere x86 congelato** (consigliata) | nessun lavoro nuovo su 32 bit; test i386 ridotti a un giro di fumo (installer NSIS e Inno, OpenTTD x86, un programma .NET x86) | nulla per l'utente; i fallimenti i386 già noti restano |
| C. x86 come componente opzionale | `.app` base senza i386 (-330 MB), x86 scaricato al primo installer a 32 bit | complessità di un download a richiesta e di una seconda firma; utile solo se la dimensione della `.app` diventa un problema |

Lavoro per A: rimuovere `i386` da `--enable-archs`, togliere `syswow64` da wineboot, `libwow64fex.dll` e la chiave `Wow64\x86`, Gecko x86, il ramo i386 dei test e degli script; poi eventualmente ripulire i thunk WoW64 di Barrique (operazione da fare con calma, perché alcune conversioni valgono anche per gli indirizzi bassi dei processi a 64 bit). Stima: 1-2 giorni per toglierlo dalla build e dalla `.app`, più la pulizia del codice.

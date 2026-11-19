Barrique alpha
==============

Barrique runs Windows programs on Apple Silicon Macs without Rosetta: Wine,
with Windows code running in a Hypervisor.framework VM and x86 code translated
by FEX. This is an alpha: expect crashes and missing features.

Requirements
------------
- A Mac with Apple Silicon (M1 or later). Intel Macs are not supported.
- macOS 26 or later.
- About 2 GB of disk space, plus the programs you install.

Install
-------
1. Unzip, then move Barrique.app to /Applications.
2. Barrique is not notarized by Apple (it is signed ad hoc), so macOS will say
   it "is damaged" or "cannot be opened". To allow it, run this once in
   Terminal:

       xattr -dr com.apple.quarantine /Applications/Barrique.app

   Do this only for a copy you downloaded from the project's GitHub releases
   page, after checking the zip against SHA256SUMS.
3. Open Barrique.app. The first start creates the Windows environment
   ("prefix"), which takes a minute.

Use
---
- Open a .exe, .msi, .bat or .lnk with Barrique (right click > Open With).
- Double clicking Barrique.app opens Wine's file manager.
- From Terminal:
      /Applications/Barrique.app/Contents/MacOS/barrique program.exe
      /Applications/Barrique.app/Contents/MacOS/barrique --wine winecfg
- Your Windows files and settings are in
      ~/Library/Application Support/Barrique
  Deleting that folder resets everything.
- Direct3D 10/11 uses wined3d by default; to try DXMT, start from Terminal
  with BARRIQUE_D3D=dxmt.

Known problems
--------------
- Firefox (ARM64 and x64) crashes now and then; its GPU process restarts.
- Web pages inside x86-64 programs (Wine Gecko) crash; 32-bit ones work.
- 32-bit x86 programs get no specific work: some may fail.
- Some programs hang on their first start and work on the second.
- Steam is not supported yet.
- Only tested on macOS 27 so far; macOS 26 is untested.

Reporting problems
------------------
Open an issue at https://github.com/nicolorisitano82/Barrique/issues with the
program, what happened, and the log:
    ~/Library/Application Support/Barrique/logs/barrique.log

Licenses
--------
Barrique is free software under the LGPL 2.1 or later. The app contains
third-party software under its own licenses: see NOTICE.md and
Barrique.app/Contents/Resources/licenses/.

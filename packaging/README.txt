Odin Sphere Leifthrasir - PC port
Version @VERSION@

Odin Sphere Leifthrasir for PlayStation 4, running natively on Windows: the
game's own code runs on the PC with a replacement for the PS4 system libraries
and a Vulkan renderer.

Source code and issue tracker:
https://github.com/saulob/odin-sphere-leifthrasir-pc


NO GAME FILES ARE INCLUDED
--------------------------
You need your own legally obtained copy of the game: a decrypted Odin Sphere
Leifthrasir PS4 package (.pkg). This port runs:

  US:    Title ID CUSA05083, version 1.00 (the base package, APP_VER 01.00,
         VERSION 01.01)
  Japan: Title ID CUSA01290, base version 1.00 (APP_VER 01.00, VERSION 01.00)
         and its 1.02 update (APP_VER 01.02, VERSION 01.00). Install the base
         package first, then the update.
  Europe: Title ID CUSA05262, base version 1.00 (APP_VER 01.00, VERSION
         01.00) and its 1.01 update (APP_VER 01.01, VERSION 01.00). Install
         the base package first, then the update.

Any other version is refused with "Different version of Odin Sphere
Leifthrasir". A package still encrypted for a PS4 console cannot be unpacked on
a PC.


REQUIREMENTS
------------
- Windows 10 version 1903 or later, or Windows 11, 64-bit
- A graphics card and driver with Vulkan 1.3, VK_EXT_robustness2,
  VK_KHR_push_descriptor and VK_EXT_vertex_attribute_divisor
- Your own Odin Sphere Leifthrasir package, plus about 4 GB of free disk space
  next to the program for the installed game
- Nothing else: no Python or other tools are needed


INSTALL
-------
1. Extract the ZIP anywhere you can write to (not "Program Files").
2. Run odin_sphere_leifthrasir.exe. The launcher opens.
3. On the Play page, click "Install game...".
4. Select your Odin Sphere Leifthrasir .pkg file.
5. Wait while the launcher checks the package and unpacks it into the "game"
   folder next to the program. The .pkg file is only read and can be deleted
   afterwards.
6. For the Japanese 1.02 or European 1.01 build, select the base package
   first; once it is installed, select the update package. The update becomes
   the active build.
7. When the Play page says "Ready to play", press PLAY.

To repair the game files, use "Reinstall game..." on the Play page. Your
settings and saves are kept. If an installation fails or is cancelled, the
previous one is left as it was.


WINDOWS SMARTSCREEN
-------------------
The first time you run odin_sphere_leifthrasir.exe, Windows SmartScreen may
show a warning, because the program is not digitally signed. If you downloaded
the release from the project's GitHub releases page, choose "More info", then
"Run anyway".


PLAYING
-------
- F11 switches between Windowed and Borderless Fullscreen.
- Xbox controllers work without setup.
- The keyboard works too; the launcher's Controls page lists the keys.
- The Controls page also chooses the button prompts the game shows: PlayStation
  (the original) or Xbox.
- To open the launcher again after turning it off, hold Shift while starting.


UPDATES
-------
When the launcher opens, it asks GitHub once whether a newer release of this
port exists. If one does, the top right of the launcher shows it, with a link
to the release notes and UPDATE NOW. The update is downloaded, its SHA-256
checksum is verified, and the launcher restarts with the new version. Only the
port's own files are replaced: the game files, saves, settings, trophies and
logs are kept. If something goes wrong, the previous version is put back.
Without an internet connection the launcher simply starts as usual.


WHERE THINGS ARE
----------------
- Game files:     the "game" folder next to odin_sphere_leifthrasir.exe
- Logs:           the "logs" folder next to odin_sphere_leifthrasir.exe,
                  one numbered log per run (the newest 50 are kept)
- Settings:       user\osl.ini
- Saves:          user\savedata\
- Trophies:       user\trophy\
- Shader cache:   user\cache\

When you move or update the port by hand, keep "user" (and "game", to skip
reinstalling).


REPORTING PROBLEMS
------------------
If the game does not start or crashes, please open an issue at
https://github.com/saulob/odin-sphere-leifthrasir-pc/issues and attach the
newest odin_sphere_leifthrasir_*.log from the "logs" folder. Say what you were
doing when it happened. After a crash the log ends with a CRASH REPORT section;
no other file is needed. If the game froze without crashing, also attach the
newest hang_NNN.txt from the same folder. For a problem that is not a crash, keep Detailed
logging on (About page of the launcher), reproduce it, and send the newest log.

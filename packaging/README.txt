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

  US: Title ID CUSA05083, version 1.00 (the base package, APP_VER 01.00,
      VERSION 01.01)

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
2. Run osl-loader.exe. The launcher opens.
3. On the Play page, click "Install game...".
4. Select your Odin Sphere Leifthrasir .pkg file.
5. Wait while the launcher checks the package and unpacks it into the "game"
   folder next to the program. The .pkg file is only read and can be deleted
   afterwards.
6. When the Play page says "Ready to play", press PLAY.

To repair the game files, use "Reinstall game..." on the Play page. Your
settings and saves are kept. If an installation fails or is cancelled, the
previous one is left as it was.


WINDOWS SMARTSCREEN
-------------------
The first time you run osl-loader.exe, Windows SmartScreen may show a warning,
because the program is not digitally signed. If you downloaded the release
from the project's GitHub releases page, choose "More info", then
"Run anyway".


PLAYING
-------
- F11 switches between Windowed and Borderless Fullscreen.
- F10 (or R3 + L2 on a controller) opens the port's in-game settings menu.
- Xbox controllers work without setup.
- The keyboard works too; the launcher's Controls page lists the keys.
- To open the launcher again after turning it off, hold Shift while starting.


WHERE THINGS ARE
----------------
- Game files:     the "game" folder next to osl-loader.exe
- Logs:           the "logs" folder next to osl-loader.exe, one numbered log per run
- Settings:       user\osl.ini
- Saves:          user\savedata\
- Shader cache:   user\cache\

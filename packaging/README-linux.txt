Odin Sphere Leifthrasir - PC port (Linux)
Version @VERSION@

Odin Sphere Leifthrasir for PlayStation 4, running natively on Linux x86-64:
the game's own code runs on the PC with a replacement for the PS4 system
libraries and a Vulkan renderer. This is a native build, not Wine or Proton.

Source code and issue tracker:
https://github.com/saulob/odin-sphere-leifthrasir-pc


NO GAME FILES ARE INCLUDED
--------------------------
You need your own legally obtained copy of the game: a decrypted Odin Sphere
Leifthrasir PS4 package (.pkg). This port runs:

  US:      Title ID CUSA05083, version 1.00 (the base package)
  Japan:   Title ID CUSA01290, versions 1.00 (the base package) and 1.02
           (the base package, then the update)
  Europe:  Title ID CUSA05262, versions 1.00 (the base package) and 1.01
           (the base package, then the update)

Any other version is refused with "Different version of Odin Sphere
Leifthrasir". A package still encrypted for a PS4 console cannot be unpacked on
a PC.


REQUIREMENTS
------------
- 64-bit Linux (x86-64) with glibc 2.31 or later and a Vulkan loader for
  Vulkan 1.3 (libvulkan.so.1)
- A graphics card and Mesa or proprietary driver with Vulkan 1.3,
  VK_EXT_robustness2, VK_KHR_push_descriptor and
  VK_EXT_vertex_attribute_divisor
- FUSE is optional. The AppImage normally mounts itself with FUSE; if that is
  not possible it unpacks itself into a temporary folder and runs from there
  automatically. Setting APPIMAGE_EXTRACT_AND_RUN=1 (or starting with
  --appimage-extract-and-run) forces that mode
- Your own Odin Sphere Leifthrasir package, plus about 4 GB of free disk space
  next to the AppImage for the installed game
- Nothing else: the AppImage carries its libraries (SDL3, FreeType, libpng,
  OpenSSL and others). GPU drivers and the Vulkan loader come from your system

This Linux build is EXPERIMENTAL: it is meant to offer everything the Windows
version does, but it has not yet been tested on many Linux systems.


INSTALL
-------
1. Put the AppImage in a folder you can write to (it keeps its game files,
   saves and logs beside itself) and make it executable:

     chmod +x OdinSphereLeifthrasir-v@VERSION@-x86_64.AppImage

2. Run it (double-click, or from a terminal). The launcher opens.
3. On the Play page, click "Install game..." and select your .pkg file.
4. Wait while the launcher checks the package and unpacks it into the "game"
   folder next to the AppImage. The .pkg file is only read and can be deleted
   afterwards.
5. For the Japanese 1.02 or European 1.01 build, select the base package
   first; once it is installed, select the update package.
6. When the Play page says "Ready to play", press PLAY.

To repair the game files, use "Reinstall game..." on the Play page. Your
settings and saves are kept.


UPDATES
-------
The launcher asks GitHub once per start whether a newer release exists. When
one does, the header shows it; nothing is installed until you choose UPDATE
NOW. The update replaces the AppImage you started from, verified by its SHA-256
checksum, and restarts it. The game files, saves, settings, trophies, shader
cache and logs in the folders beside the AppImage are kept.


PLAYING
-------
- F11 switches between Windowed and Borderless Fullscreen.
- Controllers are read through SDL's gamepad support.
- The keyboard works too; the launcher's Controls page lists the keys.
- The Controls page also chooses the button prompts the game shows:
  PlayStation (the original) or Xbox.
- To open the launcher again after turning it off, hold Shift while starting
  (X11 sessions), start the AppImage with --launcher, or use the "Show
  launcher" action of its desktop entry.


WHERE THINGS ARE
----------------
- Game files:     the "game" folder next to the AppImage file
- Logs:           the "logs" folder next to the AppImage file, one numbered
                  log per run (the newest 50 are kept)
- Settings (osl.ini), saves, trophy progress, the Trophy Key (keep it
                  private) and the shader cache:
                  the "user" folder next to the AppImage file

These folders are created when they are first needed and are not part of the
download. If the AppImage's folder is read-only, they are kept in
$XDG_DATA_HOME/OdinSphereLeifthrasirPC (~/.local/share/OdinSphereLeifthrasirPC)
instead. Setting OSL_PORTABLE_DIR to a folder chooses the place explicitly.


REPORTING PROBLEMS
------------------
If the game does not start or crashes, please open an issue at
https://github.com/saulob/odin-sphere-leifthrasir-pc/issues and attach the
newest odin_sphere_leifthrasir_*.log from the "logs" folder. Say what you were
doing, and give your Linux distribution, your GPU and its driver. After a crash
the log ends with a CRASH REPORT section; no other file is needed. For a
problem that is not a crash, keep Detailed logging on (About page of the
launcher), reproduce it, and send the newest log.


LICENSE
-------
The original project code of this port is licensed under the GNU General
Public License v2.0 or later (GPL-2.0-or-later). The full text is in
usr/share/licenses/OdinSphereLeifthrasirPC/LICENSE.txt inside the AppImage
(run it with --appimage-extract to see the files).

Third-party components keep their own licenses. Their license files and
notices are in the same folder, starting with THIRD-PARTY-LICENSES.txt and
THIRD-PARTY-LICENSES-LINUX.txt.

Game files are not included and are not covered by this project's license.
This port is not affiliated with or endorsed by Vanillaware, Atlus, SEGA,
Sony Interactive Entertainment or any other rights holder of Odin Sphere
Leifthrasir.

# Odin Sphere Leifthrasir PC v0.1.0

First public release.

Odin Sphere Leifthrasir for PlayStation 4 as a native PC port: the game's own x86-64 code runs directly on the PC, with
a replacement for the PS4 system libraries and a Vulkan renderer derived from shadPS4. It runs natively on Windows, and
experimentally on Linux and Steam Deck as an AppImage. It is not an emulator, and on Linux it does not use Wine or Proton.

**No game files are included.** You need your own legally obtained Odin Sphere Leifthrasir PS4 package.

## Highlights
- **Native execution** of the Odin Sphere Leifthrasir PS4 game code on Windows and Linux.
- **Five supported builds**: USA 1.00, Japanese 1.00 and 1.02, European 1.00 and 1.01.
- **TRG launcher with a package installer**: pick your own `.pkg` and the launcher checks it and unpacks it.
- **Windowed and Borderless Fullscreen**, switched with F11, with window size and monitor selection.
- **Render resolutions** from 480p to 4K, and **aspect ratios** 16:9, 16:10, 21:9, 32:9, 4:3 and 5:4.
- **AMD FSR 1 upscaling and RCAS sharpening**, **SMAA** and **anisotropic filtering**.
- **Graphics card selection** and **language selection** (menus and subtitles, European build).
- **Xbox controller prompts** as an option to the original PlayStation ones.
- **Local trophies**, offline on your PC.
- **FPS and frame-time display**, in the window title and in the game.
- **Skip intro**, straight to the title's PRESS START.
- **Portable** saves, settings, shader cache and logs next to the program.
- **Self-updater** for future releases, SHA-256 verified.
- **Diagnostics**: one log per run, crash reports and, on Windows, freeze reports.
- **Experimental native Linux AppImage**.

## What's in it

### Launcher and installation
- **Install game...** checks the Title ID, Content ID and versions in your package before unpacking anything, and the
  game executable's SHA-256 before installing the result. **Reinstall game...** repairs the game files and keeps your
  saves and settings. Your `.pkg` file is only read.
- Japanese 1.02 and European 1.01 are installed as their base package first, then the update package.
- **Show this launcher** can be turned off to start the game directly; hold Shift while starting to bring it back.

### Display
- Windowed and Borderless Fullscreen, switched while playing with **F11**
- Window size and monitor choices; the launcher remembers its own window placement
- Aspect Ratio: 16:9 Original, 16:10, 21:9 Ultrawide, 32:9 Super Ultrawide, 4:3 and 5:4
- Graphics card selection, VSync, and Presentation (letterbox or stretch)
- AMD FidelityFX Super Resolution 1 upscaling and AMD RCAS sharpening

### Graphics
- Render resolution: 480p, 720p, native 1080p, 1440p, 1800p and 4K, separate from the window size
- SMAA anti-aliasing
- Anisotropic texture filtering: Game, 2x, 4x, 8x or 16x

### Gameplay
- **Skip intro**: skips the startup notice and the title intro with its logos, in Refined and Classic mode, once per
  start of the game. The save data and trophy checks still run.
- **Menu & Subtitles** language for the European build: English, French, German, Spanish or Italian
- Frame rate and render resolution in the window title, and an in-game frame counter with the frame time

### Controls
- Xbox controllers work without setup; the keyboard works too, and every binding can be changed
- **Controller prompts**: PlayStation (the original) or Xbox button pictures in the game. Xbox changes the port's own
  copy of the game files; PlayStation puts the originals back.

### Trophies, saves and data
- Local PS4 trophies, offline on your PC, listed on the launcher's Trophies page
- Saves, settings, trophies and the shader cache in `user`, logs in `logs`, and the installed game in `game`, all next
  to the program (next to the AppImage on Linux)

### Updates
- When the launcher opens it checks GitHub once for a newer release, shows it in the header with its release notes, and
  installs it with **UPDATE NOW**. The download is verified with its SHA-256 checksum, only the port's own files are
  replaced, and a failed update puts the previous version back. Game files, saves, settings and logs are kept.
- This is the first release, so the updater is first used for the next version.

### Diagnostics
- One `odin_sphere_leifthrasir_NNN.log` per run for the launcher and the game together; **Detailed logging** keeps every
  session's log, and the launcher's About page opens the logs folder
- After a crash the log ends with a `CRASH REPORT` section; no separate crash file or minidump is written
- On Windows, a freeze writes `logs\hang_NNN.txt` with a short stack for every thread

## Windows
**OdinSphereLeifthrasirPC-v0.1.0-windows-x64.zip**
1. Extract it anywhere you can write to (not `Program Files`). It contains one folder,
   `OdinSphereLeifthrasirPC-v0.1.0-windows-x64`.
2. Run **odin_sphere_leifthrasir.exe** from that folder. The launcher opens.
3. On the **Play** page click **Install game...** and select your own Odin Sphere Leifthrasir `.pkg`.
4. Wait for **Ready to play**, then press **PLAY**.

Requirements: Windows 10 (version 1903 or newer) or Windows 11, 64-bit, and a graphics card and driver with Vulkan 1.3,
`VK_EXT_robustness2`, `VK_KHR_push_descriptor` and `VK_EXT_vertex_attribute_divisor`. About 4 GB of free space for the
installed game.

The program is not code-signed, so Windows SmartScreen may warn the first time it starts. If you downloaded it from this
project's GitHub releases page, choose **More info**, then **Run anyway**.

## Linux / Steam Deck (Experimental)
**OdinSphereLeifthrasirPC-v0.1.0-x86_64.AppImage**
1. Put it in a folder you can write to and make it executable (right-click > **Properties** > **Permissions** >
   **Is executable**, or `chmod +x`).
2. Run it and install your own `.pkg` from the **Play** page, as on Windows. `game`, `user` and `logs` are kept next to
   the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My
   Library** and play it from **Game Mode**.

It is a native x86_64 build using Vulkan, with the same launcher and game features as Windows except the freeze reports.
It needs glibc 2.31 or newer (Ubuntu 20.04 or later), a Vulkan loader and driver, and an X11 or Wayland desktop. It
mounts itself with FUSE, and when it cannot, it unpacks itself into a temporary folder, runs from there and removes the
folder afterwards. The AppImage is built and checked by automated scripts, but it has **not yet been tested on real
Linux PCs or a Steam Deck**. Reports are welcome. See
[docs/LINUX.md](https://github.com/saulob/odin-sphere-leifthrasir-pc/blob/main/docs/LINUX.md).

## Required game package
A decrypted PS4 package of one of these builds. Any other version is refused, and a package still encrypted for a PS4
console cannot be unpacked on a PC.

| Build | Title ID | Versions | `eboot.bin` SHA-256 |
| --- | --- | --- | --- |
| USA | `CUSA05083` | base `APP_VER` 01.00, `VERSION` 01.01 | `7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6` |
| Japan | `CUSA01290` | base `APP_VER` 01.00 | `d0add7e35bc5f58d5c9017fdce0878006c32f944fa8eb3d7ce62671257f5bcc7` |
| Japan 1.02 | `CUSA01290` | update `APP_VER` 01.02, over the Japanese base | `db96b839fe31adba4b3eb3547202f761d34881325c2fea650c4c020eded5d071` |
| Europe | `CUSA05262` | base `APP_VER` 01.00 | `3e6b1741e8a404d0f8fbe10bce93c6887974566f5be09ef1bddc22a00ba29feb` |
| Europe 1.01 | `CUSA05262` | update `APP_VER` 01.01, over the European base | `572bf21e57febee415d739cded5516def9ca81e867ad3184c989367248edcd7e` |

## Known limitations
- Only **16:9 Original** is undistorted. The game's 2D scene is laid out for its 16:9 canvas, so 21:9 and 32:9 stretch
  the gameplay horizontally and 16:10, 4:3 and 5:4 squeeze it; the HUD, menus and prerendered videos are not adjusted.
- Trophies are local only: there is no PSN connection or online play.
- **Voice Language** is the game's own in-game option; the launcher's language setting changes text only, and only for
  the European build.
- The Linux AppImage is experimental and untested on real hardware. It writes no freeze reports, Wayland compositors
  choose the window position themselves, and holding Shift to bring back the launcher works in X11 sessions (start with
  `--launcher` otherwise).
- The program is not code-signed.

## Reporting problems
If the game does not start, crashes or freezes, please open an
[issue](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues). Attach the newest `odin_sphere_leifthrasir_*.log`
from the `logs` folder next to the program (next to the AppImage on Linux), and after a freeze on Windows the newest
`hang_NNN.txt`, with a line about what you were doing. On Linux, please also give your distribution, your GPU and its
driver.

<!--
Antivirus scans: add only after the uploads have been scanned, with the links and the results as reported, followed by:
VirusTotal results can change as antivirus engines update, and a clean scan is not a security guarantee. Compare your
download with the SHA-256 checksums below.
-->

## Checksums
SHA-256 files are published with the release for both downloads: `OdinSphereLeifthrasirPC-v0.1.0-windows-x64.sha256`
and `OdinSphereLeifthrasirPC-v0.1.0-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `OdinSphereLeifthrasirPC-v0.1.0-windows-x64.zip` | `56138b54ae34f05b0c30950f118d0595d2f08424878c06dcb320cfd86e06f448` |
| `OdinSphereLeifthrasirPC-v0.1.0-x86_64.AppImage` | `13fdc5ccb53b602c9f923932d7f0f72177187adc96f8645a9a137e789246a157` |

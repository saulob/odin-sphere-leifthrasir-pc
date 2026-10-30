<div align="center">

# Odin Sphere Leifthrasir - PC Port

### Odin Sphere Leifthrasir, Vanillaware's PS4 action RPG, running natively on Windows, and experimentally on Linux and Steam Deck, with the TRG launcher and modern PC options

[![Latest release](https://img.shields.io/github/v/release/saulob/odin-sphere-leifthrasir-pc?style=for-the-badge&label=release&color=d12a78&labelColor=0e152a&include_prereleases&cacheSeconds=300)](https://github.com/saulob/odin-sphere-leifthrasir-pc/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/saulob/odin-sphere-leifthrasir-pc/total?style=for-the-badge&color=d12a78&labelColor=0e152a&cacheSeconds=300)](https://github.com/saulob/odin-sphere-leifthrasir-pc/releases)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20experimental%20%7C%20Steam%20Deck%20experimental-00acc1?style=for-the-badge&labelColor=0e152a)

<sub>The original PS4 game code runs on the PC with a replacement for the PS4 system libraries and a Vulkan renderer derived from <a href="https://github.com/shadps4-emu/shadPS4">shadPS4</a>. <b>No game files included</b>: bring your own legally obtained Odin Sphere Leifthrasir PS4 package.</sub>

</div>

<img width="1437" height="855" alt="2026-10-27_19-08" src="https://github.com/user-attachments/assets/fce77149-c2cd-401f-a8ed-4aefcd698928" />

## Status

**v0.1.0 is the first public release.** The loader and runtime map and run the game's own x86-64 code, with the
shadPS4-derived Vulkan video core, the launcher and installer, local trophies, the self-updater and the diagnostics.
The USA 1.00 base package (`CUSA05083`), the Japanese release (`CUSA01290` base 1.00 plus its 1.02 update) and the
European release (`CUSA05262` base 1.00 plus its 1.01 update) are supported; any other version is refused.

Windows is the supported platform. The native Linux build, shipped as an AppImage, is **experimental**: it is built and
checked by scripts, but it has not yet been tested on real Linux PCs or a Steam Deck.

## Features

- **Native PC port**: the game's own x86-64 code runs directly on the PC. It is not an emulator, and on Linux it does
  not use Wine or Proton.
- **TRG launcher** that installs the game from your own `.pkg` file, with **Reinstall game...** to repair it

### Display
- **Windowed** and **Borderless Fullscreen**, switched with **F11** while playing
- **Window size** and **monitor** selection for the game; the launcher remembers its own window placement
- **Aspect Ratio**: 16:9 Original, 16:10, 21:9 Ultrawide, 32:9 Super Ultrawide, 4:3 and 5:4
- **Graphics card selection**, **VSync** and **Presentation** (letterbox or stretch)
- **Upscaling** (AMD FidelityFX Super Resolution 1) and **sharpening** (AMD RCAS)

### Graphics
- **Render resolution** from 480p through native 1080p, 1440p and 1800p to 4K, separate from the window size
- **SMAA** anti-aliasing and **anisotropic texture filtering** (Game, 2x, 4x, 8x, 16x)

### Gameplay
- **Skip intro**: start at the title's PRESS START, without the startup notice, logos and intro
- **Menu & Subtitles** language for the European build (English, French, German, Spanish, Italian)
- Frame rate and render resolution in the window title, and an in-game **frame counter** with the frame time

### Controls
- Xbox controllers without setup, and keyboard play with every binding changeable
- **Controller prompts**: the game's PlayStation button pictures (the default) or **Xbox** prompts

### Your data
- **Local PS4 trophies**, offline on this PC, listed on the launcher's Trophies page
- **Portable**: settings, saves, trophies, the shader cache and logs stay in `user` and `logs` next to the program
- **Updates**: a newer GitHub release is offered in the launcher and installed with one click, SHA-256 verified,
  keeping your game files, saves and settings
- **Diagnostics**: one log per run, a `CRASH REPORT` section in that log after a crash, and on Windows a
  `hang_NNN.txt` report when the game freezes

## What you need

- **Windows**: Windows 10 (version 1903 or newer) or Windows 11, 64-bit, **or**
- **Linux / Steam Deck** (experimental): 64-bit x86 Linux with glibc 2.31 or newer (such as Ubuntu 20.04 or later), a
  Vulkan loader, and an X11 or Wayland desktop
- A processor with **AVX** and POPCNT: Intel Core 2nd generation (Sandy Bridge), AMD FX or newer. MOVBE, BMI1 and LZCNT,
  which older processors lack, are handled in software
- A graphics card and driver with **Vulkan 1.3**, `VK_EXT_robustness2`, `VK_KHR_push_descriptor` and `VK_EXT_vertex_attribute_divisor`
- **Your own legally obtained Odin Sphere Leifthrasir package**: a decrypted PS4 `.pkg` of a supported build, and about 4 GB of free space

| | |
| --- | --- |
| Game | Odin Sphere Leifthrasir |
| Platform | PlayStation 4 |
| USA build | Title ID `CUSA05083`, Content ID `UP0177-CUSA05083_00-ODINSPHERELE0000`, `APP_VER` `01.00`, `VERSION` `01.01`, `eboot.bin` SHA-256 `7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6` |
| Japanese base | Title ID `CUSA01290`, Content ID `JP0005-CUSA01290_00-ODINSPHERELE0000`, `APP_VER` `01.00`, `VERSION` `01.00`, `eboot.bin` SHA-256 `d0add7e35bc5f58d5c9017fdce0878006c32f944fa8eb3d7ce62671257f5bcc7` |
| Japanese 1.02 update | Same Title and Content ID, `APP_VER` `01.02`, `VERSION` `01.00`, `eboot.bin` SHA-256 `db96b839fe31adba4b3eb3547202f761d34881325c2fea650c4c020eded5d071` |
| European base | Title ID `CUSA05262`, Content ID `EP1063-CUSA05262_00-ODINSPHERELE0000`, `APP_VER` `01.00`, `VERSION` `01.00`, `eboot.bin` SHA-256 `3e6b1741e8a404d0f8fbe10bce93c6887974566f5be09ef1bddc22a00ba29feb` |
| European 1.01 update | Same Title and Content ID, `APP_VER` `01.01`, `VERSION` `01.00`, `eboot.bin` SHA-256 `572bf21e57febee415d739cded5516def9ca81e867ad3184c989367248edcd7e` |

The launcher checks the Title ID, Content ID and versions in the package's `param.sfo` before it unpacks anything, and
the `eboot.bin` hash before it installs the result. An update is only laid over the base package of its own region,
with the same Title ID and Content ID. A package encrypted for a PS4 console cannot be unpacked on a PC.

## Installing

**No game files are included** in the release, and the port does not download the game.

### Windows

1. Download **OdinSphereLeifthrasirPC-v0.1.0-windows-x64.zip** from the
   [latest release](https://github.com/saulob/odin-sphere-leifthrasir-pc/releases/latest) and extract it anywhere you
   can write to (not `Program Files`). It contains one folder, `OdinSphereLeifthrasirPC-v0.1.0-windows-x64`.
2. Run **odin_sphere_leifthrasir.exe** from that folder. The launcher opens.
3. On the **Play** page click **Install game...** and select your own Odin Sphere Leifthrasir `.pkg` file.
4. Wait until **Ready to play** appears. The package is unpacked on your PC into the `game` folder next to the program
   (about 3.3 GB). The `.pkg` file is only read, and you can delete it afterwards.
5. Japanese 1.02 or European 1.01: install the base package of that region first, then install its update package the
   same way. The update is laid over the base and becomes the active build.
6. Press **PLAY**.

**Reinstall game...** on the Play page repairs the game files and keeps your settings and saves.

### Linux and Steam Deck (experimental)

1. Download **OdinSphereLeifthrasirPC-v0.1.0-x86_64.AppImage**, put it in a folder you can write to and make it
   executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own `.pkg` from the **Play** page, as on Windows. `game`, `user` and `logs` are kept next to
   the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My
   Library** and play it from **Game Mode**.

The AppImage mounts itself with FUSE, and when it cannot it unpacks itself into a private temporary folder, runs from
there and removes the folder afterwards. It needs glibc 2.31 or newer: it is built on Ubuntu 20.04, and no file in it
needs anything newer. Graphics drivers, the Vulkan loader, X11/Wayland and audio come from your system. The build, its
checks and the differences from Windows are in [docs/LINUX.md](docs/LINUX.md).

### Windows SmartScreen

Windows may warn the first time the program starts, because releases are not digitally signed. If you downloaded it from
this project's GitHub releases page, choose **More info**, then **Run anyway**. Each release lists the SHA-256 of its
downloads; compare yours with it.

### Saves and settings

Everything is kept next to the program (next to the AppImage on Linux):

| Folder | Contents |
| --- | --- |
| `user\osl.ini` | Launcher and game settings |
| `user\savedata\` | Your saves |
| `user\trophy\` | Your trophy progress and the trophy list and icons unpacked from your game |
| `user\keys.json` | Your Trophy Key, if you configure one |
| `user\cache\<Title ID>\` | Shader cache, so later starts load faster |
| `logs\` | One `odin_sphere_leifthrasir_NNN.log` per run, launcher and game together; the newest 50 are kept |
| `game\` | The installed game (unpacked from your own package) |

## The launcher

- **Play**: install or reinstall the game from your `.pkg`, the **Ready to play** status and **Show this launcher**
- **Display**: display mode, window size, monitor, Aspect Ratio, graphics card, VSync, Presentation, FSR 1 upscaling and RCAS sharpening
- **Graphics**: Render resolution, SMAA anti-aliasing and anisotropic texture filtering
- **Gameplay**: Menu & Subtitles language (the European build only), **Skip intro**, frame rate and render resolution in the title, and the in-game frame counter
- **Controls**: **Controller prompts** (PlayStation or Xbox), every keyboard binding and the port hotkeys, with **Reset to defaults**. Xbox changes the button pictures the game shows (A/B/X/Y, LB/RB, LT/RT, MENU and VIEW) in the port's own copy of the game; the touchpad prompt becomes VIEW. PlayStation puts the originals back. Your `.pkg` file is never changed.
- **Trophies**: your local PS4 trophy progress, the trophy list and the Trophy Key setup
- **About**: the port version, the game, the renderer and the library versions, the game, saves and logs folders, the settings file, **Reset all settings**, and **Diagnostics** with Detailed logging and a button that opens the logs folder

Hold **Shift** while starting to bring the launcher back after turning **Show this launcher** off. On Linux, Shift works
in X11 sessions; anywhere, starting with `--launcher` (or the desktop entry's **Show launcher** action) does the same.

**Alt+F4** (or the window close button) quits the launcher. **Esc** cancels the keyboard-binding capture and pop-ups; it does not quit. **Ctrl+S** saves the launcher settings without starting the game.

The Render resolution and Aspect Ratio options change the game's verified internal render-target sites. Only
**16:9 Original** is undistorted: the game's 2D scene is still laid out for its original 16:9 canvas, so
21:9 and 32:9 stretch the gameplay horizontally instead of showing more of the world, and 16:10, 4:3 and
5:4 squeeze it. The HUD, menus and prerendered videos are not adjusted either.

Cheats are not exposed: Odin Sphere's gameplay values have not yet been verified for a launcher option. Each build's own
text is used, and no region's assets are ever mixed.

**Menu & Subtitles** is offered for the European build (CUSA05262) only, whose executable selects its menu and subtitle
archives from the console's System Language: English, French, German, Spanish or Italian. The setting is stored in
`user\osl.ini` as `text_language` and is passed to the game as the PS4 System Language. The USA build contains a single
English text set and the Japanese build Japanese text, so neither shows the control; a language chosen for the European
build is kept if you switch builds and used again when you return.

**Voice Language** is the game's own setting. The European build ships the Japanese and English voice banks in its
single `Odin2_BT_UK.CPK` audio container (the per-language `Odin2_BT_*.CPK` files are secondary), and the in-game
**Options → Voice Language** chooses between them. The launcher does not add a Voice Language option and does not
change the game's choice; Menu & Subtitles affects text only.

**Skip intro** (Gameplay, Off by default) skips the startup notice and the title intro with its logos, so the game opens
at the title's PRESS START, in both Refined and Classic mode. It acts once per start of the game: returning to the title,
switching mode and the title's own demo play as usual. The save data and trophy checks before the title still run. It is
stored in `user\osl.ini` as `skip_intro`, and it is available for every supported build.

## Screenshots

<img width="1441" height="853" alt="2026-10-27_19-11" src="https://github.com/user-attachments/assets/5a94903d-2680-4679-9098-6cb0cb305b8b" />

## Controls

- **F11** switches between Windowed and Borderless Fullscreen while playing; the key can be changed on the Controls page.
- Xbox controllers work without setup (on Linux through SDL's gamepad support).
- The keyboard plays too: the Controls page lists every binding, and **Reset to defaults** restores them.
- **Controller prompts** chooses the button pictures the game shows: PlayStation (the original) or Xbox.

## Trophies

Trophies are local and offline: unlocks are kept in `user\trophy\` on this PC, nothing is sent anywhere, and the
launcher's **Trophies** page lists your progress with the trophy list and icons unpacked from your own game.

## Updates

When the launcher opens it asks GitHub once, in the background, for the latest release. A newer one is shown at the top
right of the header with its release notes and **UPDATE NOW**; the launcher downloads it, checks its SHA-256 checksum,
replaces only the port's own files and restarts. The game files, `user` and `logs` are never touched, and a failed
update puts the previous version back. Without a connection, or while there is no release, nothing is shown.

- **Windows**: the release ZIP's one folder is unpacked into a temporary workspace, and the small helper
  `trg-updater.exe`, run from that workspace, waits for the launcher to close, backs up and replaces the program files,
  rolls them back if anything fails, and starts the new version with the launcher shown.
- **Linux**: the AppImage you started (not a mounted or unpacked copy) is replaced next to itself and started again.

## Logs, crash reports and freezes

**Detailed logging** (About, Diagnostics) is on by default and keeps the log of every session. The launcher and the
game write one `odin_sphere_leifthrasir_NNN.log` for the whole run; it records the port version, the operating system,
the processor, the game build and the graphics and display settings, and nothing per frame. Turned off, a
run still writes its log while it runs, but a normal exit discards it so normal sessions do not accumulate files. A
crash or abnormal exit keeps the log, and crash diagnostics are never discarded: a crash appends one `CRASH REPORT`
section (exception, faulting module or guest offset, registers and stack) to the same log, and no other crash file or
minidump is written. The newest 50 numbered logs are kept. **Open logs folder** opens the folder that holds the logs,
creating it first when it does not exist.

On Windows, a freeze that is not a crash writes `logs\hang_NNN.txt`: the port version, the game build, and a short stack
(module and offset, or guest offset) for every thread. The run's log gets one line naming the file. It is written when
the game window stops answering for 5 seconds, or when no frame is presented for 15 seconds while the game is in front
and not minimized. The game keeps running, and a session writes at most three such files. Linux writes no freeze report.

## Building it yourself

1. Install [MSYS2](https://www.msys2.org) with the CLANG64 toolchain. `.\tools\setup.ps1` checks it and prints the `pacman` command for anything missing.
2. `.\tools\build.ps1` fetches the pinned dependencies (TRG Launcher, Dear ImGui and a few small libraries) and builds `build\cmake\clang64\bin\odin_sphere_leifthrasir.exe`.
3. `.\tools\build.ps1 -Run` also starts it. Install the game from the launcher's Play page, as in a release; the development build keeps it in `game\prepared\<build folder>` and the boot image in `build\image\<build folder>` of the repository (`CUSA05083-1.00`, `CUSA01290-1.00`, `CUSA01290-1.02`, `CUSA05262-1.00` or `CUSA05262-1.01`).
4. `.\tools\test.ps1` builds and runs the focused headless tests (game profile, settings, keys, window fit, GPU tiling, patch workflow, updater and update helper). Python is not needed to build, test or play; the optional development scripts in `scripts\` use it (`.\tools\setup.ps1 -InstallPython`).
5. `.\tools\package.ps1` builds the Release configuration (`.\tools\build.ps1 -Release`, in `build\cmake\clang64-release`: `-O3` with ThinLTO across the program and its own libraries, without a symbol table or build id) and the release update helper (`.\tools\build-updater.ps1`: MSYS2 CLANG64 clang++ and LLD, static, only Windows system DLLs, its debug information split into `trg-updater.debug`), then packages `dist\OdinSphereLeifthrasirPC-v<version>-windows-x64.zip`, which holds one folder of that name, and its `.sha256` file. The ZIP is extracted again, compared file by file with the staging folder and scanned, checked with the updater's own rules, and the update helper tests run against the packaged `trg-updater.exe`; `trg-updater.debug` goes to `dist\OdinSphereLeifthrasirPC-v<version>-windows-x64-symbols\`, outside the ZIP. `.\tools\package.ps1 -VerifyOnly <ZIP>` checks a finished ZIP, and `.\tools\test-release-check.ps1` checks that the release check refuses player files, game data, symbols, local paths and leftovers. Nothing is published.
6. Linux: `tools/build.sh` builds on Linux x86_64 (`--release` for the Release configuration in `build/cmake/linux-release`), and `tools/package-linux.sh` packages `dist/OdinSphereLeifthrasirPC-v<version>-x86_64.AppImage` and its `.sha256` on Ubuntu 20.04 (in WSL2 from this Windows checkout: `tools/package-release-linux.sh`); `--verify-only <AppImage>` checks a finished one. See [docs/LINUX.md](docs/LINUX.md).

The version is `OSL_PORT_VERSION` in `CMakeLists.txt`, the only place it is written: the program's version resource, the
launcher, the logs, the updater and the release file names all come from it.

### What rebuilds

The build is incremental: `ninja: no work to do` means every input is unchanged.

- The video core in `gpu\shadps4` is this project's own fork of shadPS4 (provenance in [gpu/VENDOR.txt](gpu/VENDOR.txt)) and is edited in place. Ninja compiles it directly, so an edited source or header rebuilds its objects and relinks the loader.
- The pinned dependencies in `gpu\deps.txt` are fetched by `build.ps1` into git-ignored folders, which stay pristine and record their commit in `.osl_commit`. Changes to them go in `gpu\patches\<dependency>\NNNN-name.patch`. CMake applies the patches to a copy in `build\cmake\clang64\patched` whenever a patch, the patch list or the pinned commit changes, and rewrites only the files whose content changed.
- Ninja re-runs CMake by itself when a CMake file, `gpu\deps.txt` or a patch changes. A plain `ninja` stops with a message when a dependency must be fetched again first.
- `build.ps1` rebuilds the boot image when the game is installed again or the builder sources change.
- `src\import_names.inc` is generated by hand from a game's imports: `python scripts\gen_import_names.py <game dir> --aerolib <aerolib.inl>`.

### Reference comparison

When a PS4 behavior needs research, the reference ports this one grew from are listed in
[gpu/VENDOR.txt](gpu/VENDOR.txt): the shadPS4 video core, gow3_pc and its lineage
(uncharted-ndc-native, MediEvil PC, the HayatoG ports) and my own Dragon's Crown Pro PC port, which
is the architectural model for the loader, runtime, launcher and installer.

## Reporting problems

If the game does not start or crashes, please open an [issue](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues).
Attach the newest `odin_sphere_leifthrasir_*.log` file from the `logs` folder next to the program, and say what you were
doing when the problem happened. The launcher and the game write to the same log for one run; after a crash the log
ends with a `CRASH REPORT` section, and no separate crash file or minidump is written. One log is normally enough. If
the game froze without crashing, attach the newest `hang_NNN.txt` from the same folder as well.

For a problem that is not a crash, keep **Detailed logging** on in the launcher (About, Diagnostics), reproduce it, and
send the newest log. On Linux, please also give your distribution, your GPU and its driver.

## Sponsoring

Odin Sphere Leifthrasir PC is maintained and developed in my free time. If you enjoy the project or find it useful, consider supporting its continued development.

<p align="center">
<a href="https://github.com/sponsors/saulob" target="_blank"><img src="https://img.shields.io/badge/GitHub%20Sponsors-Sponsor-EA4AAA?style=for-the-badge&logo=githubsponsors&logoColor=white" alt="GitHub Sponsors"></a>
</p>

## Credits

- Odin Sphere Leifthrasir by Vanillaware, published by Atlus (2016). This is an unofficial fan-made PC port. It is not affiliated with or endorsed by Vanillaware, Atlus, SEGA, Sony Interactive Entertainment or any other rights holder, and it contains none of their files.
- [shadPS4](https://github.com/shadps4-emu/shadPS4) for the Vulkan renderer, shader recompiler and GPU command processing the video core is built on, and for the PS4 package extractor the installer is adapted from
- [gow3_pc](https://github.com/FelipePrado0/gow3_pc) and its lineage, uncharted-ndc-native, MediEvil PC and the HayatoG ports, for the loader, runtime and video core this port grew from
- Dragon's Crown Pro PC, the port this architecture is reproduced from
- [TRG Launcher](https://github.com/TekRantGaming/trg-launcher) by TekRantGaming, the launcher interface, pinned at v1.1.0, licensed under the MIT License
- The licenses of the libraries the release includes are in the `licenses` folder of the ZIP

## AI disclosure

AI tools were used to assist with code analysis, development, debugging and documentation. Changes were directed, tested and reviewed by the maintainer.

## License

Original code in this project is licensed under the GNU General Public License v2.0 or later (GPL-2.0-or-later). See [LICENSE](LICENSE).

Third-party components retain their respective licenses and notices. See [packaging/licenses/THIRD-PARTY-LICENSES.txt](packaging/licenses/THIRD-PARTY-LICENSES.txt).

No game files are included. Odin Sphere Leifthrasir and its assets are not covered by this project's license.

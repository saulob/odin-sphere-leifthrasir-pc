<div align="center">

# Odin Sphere Leifthrasir - PC Port

### Odin Sphere Leifthrasir, Vanillaware's PS4 action RPG, running natively on Windows, with the TRG launcher

<sub>The original PS4 game code runs on the PC with a replacement for the PS4 system libraries and a Vulkan renderer derived from <a href="https://github.com/shadps4-emu/shadPS4">shadPS4</a>. <b>No game files included</b>: bring your own legally obtained Odin Sphere Leifthrasir PS4 package.</sub>

</div>

## Status

Initial bring-up. This repository is the foundation of the port: the loader and runtime that map and
run the game's own x86-64 code, the shadPS4-derived Vulkan video core, the launcher and installer,
and the diagnostics that make the first boot debuggable. Only the USA 1.00 base package
(`CUSA05083`, APP_VER `01.00`, VERSION `01.01`) is supported and verified; any other version is
refused.

## Features

- **Native PC port**: the game's own x86-64 code runs directly on the PC. It is not an emulator.
- **TRG launcher** that installs the game directly from your own `.pkg` file and remembers its window position
- **Windowed** and **Borderless Fullscreen**, switched with **F11** while playing, with window size and monitor selection
- **Aspect Ratio**: 16:9 Original, 16:10, 21:9 Ultrawide, 32:9 Super Ultrawide, 4:3 and 5:4
- **Render resolution** from 480p to 4K, separate from the window size
- **Graphics card selection**, **VSync**, **Presentation** (letterbox or stretch)
- **Upscaling** (AMD FSR 1) and **sharpening** (AMD RCAS)
- **SMAA** anti-aliasing and **anisotropic texture filtering** (Game, 2x, 4x, 8x, 16x)
- Frame rate and render resolution in the window title, and an in-game **frame counter**
- An in-game settings menu: **F10** on the keyboard, or **R3 + L2** on the controller
- Xbox controllers without setup, and keyboard support
- **Local PS4 trophies**, offline on this PC, shown on the launcher's Trophies page
- Portable `user` and `logs` folders next to the program for settings, saves, the shader cache and logs

## What you need

- Windows 10 (version 1903 or newer) or Windows 11, 64-bit
- A graphics card and driver with **Vulkan 1.3**, `VK_EXT_robustness2`, `VK_KHR_push_descriptor` and `VK_EXT_vertex_attribute_divisor`
- **Your own legally obtained Odin Sphere Leifthrasir package**: a decrypted PS4 `.pkg` of the supported build, and about 4 GB of free space

| | |
| --- | --- |
| Game | Odin Sphere Leifthrasir |
| Platform | PlayStation 4 |
| Supported build | Title ID `CUSA05083`, Content ID `UP0177-CUSA05083_00-ODINSPHERELE0000`, `APP_VER` `01.00`, `VERSION` `01.01`, `eboot.bin` SHA-256 `7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6` |

The launcher checks the Title ID, Content ID and versions in the package's `param.sfo` before it unpacks anything, and
the `eboot.bin` hash before it installs the result. A package encrypted for a PS4 console cannot be unpacked on a PC.

## Installing

1. Build the port (see below) or extract a release anywhere you can write to.
2. Run **osl-loader.exe**. The launcher opens.
3. On the **Play** page click **Install game...** and select your own Odin Sphere Leifthrasir `.pkg` file.
4. Wait until **Ready to play** appears. The package is unpacked on your PC into the `game` folder next to the program
   (about 4 GB). The `.pkg` file is only read, and you can delete it afterwards.
5. Press **PLAY**.

**No game files are included** in the release, and the port does not download the game.

### Saves and settings

Everything is kept next to the program:

| Folder | Contents |
| --- | --- |
| `user\osl.ini` | Launcher and game settings |
| `user\savedata\` | Your saves |
| `user\trophy\` | Your trophy progress and the trophy list and icons unpacked from your game |
| `user\keys.json` | Your Trophy Key, if you configure one |
| `user\cache\CUSA05083\` | Shader cache, so later starts load faster |
| `logs\` | One `odin_sphere_leifthrasir_NNN.log` per run |
| `game\` | The installed game (unpacked from your own package) |

## The launcher

- **Play**: install or reinstall the game from your `.pkg`, the **Ready to play** status and **Show this launcher**
- **Display**: display mode, window size, monitor, Aspect Ratio, graphics card, VSync, Presentation, FSR 1 upscaling and RCAS sharpening
- **Graphics**: Render resolution, SMAA anti-aliasing and anisotropic texture filtering
- **Gameplay**: frame rate and render resolution in the title, and the in-game frame counter
- **Controls**: every keyboard binding and the port hotkeys, with **Reset to defaults**
- **Trophies**: your local PS4 trophy progress, the trophy list and the Trophy Key setup
- **About**: the port version, the game, the renderer and the library versions, the game, saves and logs folders, the settings file, **Reset all settings**, and **Diagnostics** with Detailed logging and a button that opens the logs folder

Hold **Shift** while starting to bring the launcher back after turning **Show this launcher** off.

**Alt+F4** (or the window close button) quits the launcher. **Esc** cancels the keyboard-binding capture and pop-ups; it does not quit. **Ctrl+S** saves the launcher settings without starting the game.

**F10** (or **R3 + L2**) opens the in-game settings menu with the display mode, VSync, FSR 1, sharpening and the frame counter.

The Render resolution and Aspect Ratio options change the game's verified internal render-target sites. Only
**16:9 Original** is undistorted: the game's 2D scene is still laid out for its original 16:9 canvas, so
21:9 and 32:9 stretch the gameplay horizontally instead of showing more of the world, and 16:10, 4:3 and
5:4 squeeze it. The HUD, menus and prerendered videos are not adjusted either.

Xbox button prompts, cheats and the menu-text language are not exposed: the US build used here contains a
single English text set, and Odin Sphere's prompt atlases and gameplay values have not yet been verified for
those launcher options. The PlayStation prompts and the game's own text are used.

**Detailed logging** (About, Diagnostics) is on by default and keeps the log of every session. Turned off, a run
still writes its log while it runs, but a normal exit discards it so normal sessions do not accumulate files. A
crash or abnormal exit keeps the log, and crash diagnostics are never discarded. **Open logs folder** opens the
folder that holds the logs, creating it first when it does not exist.

## Building it yourself

1. Install [MSYS2](https://www.msys2.org) with the CLANG64 toolchain. `.\tools\setup.ps1` checks it and prints the `pacman` command for anything missing.
2. `.\tools\build.ps1` fetches the pinned dependencies (TRG Launcher, Dear ImGui and a few small libraries) and builds `build\cmake\clang64\bin\osl-loader.exe`.
3. `.\tools\build.ps1 -Run` also starts it. Install the game from the launcher's Play page, as in a release; the development build keeps it in `game\prepared\CUSA05083-1.00` and the boot image in `build\image\CUSA05083-1.00` of the repository.
4. `.\tools\test.ps1` builds and runs the focused headless tests (game profile, settings, keys, window fit, GPU tiling). Python is not needed to build, test or play; the optional development scripts in `scripts\` use it (`.\tools\setup.ps1 -InstallPython`).

### Reference comparison

When a PS4 behavior needs research, the reference ports this one grew from are listed in
[gpu/VENDOR.txt](gpu/VENDOR.txt): the shadPS4 video core, gow3_pc and its lineage
(uncharted-ndc-native, MediEvil PC, the HayatoG ports) and my own Dragon's Crown Pro PC port, which
is the architectural model for the loader, runtime, launcher and installer.

## Reporting problems

If the game does not start or crashes, attach the newest `odin_sphere_leifthrasir_*.log` from the
`logs` folder next to the program, and say what you were doing when the problem happened.

## Credits

- Odin Sphere Leifthrasir by Vanillaware, published by Atlus (2016). This is an unofficial fan-made PC port. It is not affiliated with or endorsed by Vanillaware, Atlus, SEGA, Sony Interactive Entertainment or any other rights holder, and it contains none of their files.
- [shadPS4](https://github.com/shadps4-emu/shadPS4) for the Vulkan renderer, shader recompiler and GPU command processing the video core is built on, and for the PS4 package extractor the installer is adapted from
- [gow3_pc](https://github.com/FelipePrado0/gow3_pc) and its lineage, uncharted-ndc-native, MediEvil PC and the HayatoG ports, for the loader, runtime and video core this port grew from
- Dragon's Crown Pro PC, the port this architecture is reproduced from
- [TRG Launcher](https://github.com/TekRantGaming/trg-launcher), the launcher interface, pinned at v1.1.0
- The licenses of the libraries the release includes are in the `licenses` folder of the ZIP

## AI disclosure

AI tools were used to assist with code analysis, development, debugging and documentation. Changes were directed, tested and reviewed by the maintainer.

## License

Original code in this project is licensed under the GNU General Public License v2.0 or later (GPL-2.0-or-later). See [LICENSE](LICENSE).

Third-party components retain their respective licenses and notices. See [packaging/licenses/THIRD-PARTY-LICENSES.txt](packaging/licenses/THIRD-PARTY-LICENSES.txt).

No game files are included. Odin Sphere Leifthrasir and its assets are not covered by this project's license.

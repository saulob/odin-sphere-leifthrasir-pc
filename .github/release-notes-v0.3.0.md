# Odin Sphere Leifthrasir PC v0.3.0

Odin Sphere Leifthrasir for PlayStation 4 as a native PC port for Windows and, experimentally, Linux and Steam Deck

**No game files are included.** You need your own legally obtained Odin Sphere Leifthrasir PS4 package from one of the supported builds

If you use an earlier release, the launcher updater can install v0.3.0 while preserving your installed game files, saves, settings and logs

## Portuguese (Brazil)

v0.3.0 adds Portuguese (Brazil) directly to the port, in both Refined and Classic mode

- **Menus, subtitles and story text**, translated from the game's official English
- **Dynamic text**, archive pages, credit headings and trophies
- **Visual assets**: the title menu (NOVO JOGO, CARREGAR, SAIR DO JOGO, PRESSIONE QUALQUER BOTÃO), HUD and menu tabs, save and load screens, stage results, stage and location cards, chapter and act titles, the chapter prologue pages, Mode Select, the kitchen, the world map and more, redrawn from the game's own letters
- **Classic mode** has its own redrawn art, so the Portuguese interface is not limited to Refined
- **The launcher** and the port's in-game screens follow the language
- **Accented letters** that the game's fonts lack (`ã õ Ã Õ Ç` and others) are drawn into your own font atlas in memory, and no game file is written
- Choose it under **Menu & Subtitles**, where it is listed right after English on every supported build

This addresses the missing characters reported in [Issue #18](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/18)

The issue remains open on GitHub while waiting for reporter confirmation

### What is covered

- The text and art were verified against the real files of USA 1.00, Europe 1.00 and 1.01, and Japan 1.00 and 1.02
- USA 1.01, Hong Kong and Asia use the same translation, but their executables' fonts have not been measured, and no real files of those builds were available for checking the redrawn art, so a record that needs a letter the font lacks keeps the game's own text there
- On Japan 1.00 and 1.02, records that are not proven to be the English text keep Japanese
- Still in English: the Bookshelf book frames, the manual pages, the Skill screen and the Classic logo and ending layouts

## Cheats

v0.3.0 introduces native cheats, changed in game with the **F10** cheat menu

- **F10** opens the in-game **CHEATS** menu, and its changes apply at once and last for the current session
- The launcher's **Cheats** page sets the state the game starts with, and every cheat is **Off** by default
- The key can be changed on the Controls page
- **Refined and Classic mode** are both supported

The cheats

- **Max Gold Gain**
- **Max Phozon Gain**
- **No AP Consumption**
- **Infinite PP**
- **Infinite POW**
- **Infinite Health**
- **Infinite Arrows**
- **Experience Multiplier** (Off, 2x, 5x, 10x or 100x)

The cheats change a few instructions of the game's code in memory, found by their code and checked first, and no game file or save is modified

A cheat whose code is not found exactly once in your build shows as **Unavailable**, and the log says why

In Classic mode, **No AP Consumption** is already true because Classic has no AP, and experience comes from food and items only, so the multiplier has nothing to multiply for defeated enemies

The cheat code was checked by automated tests that run the game's own instructions on the real files of USA 1.00, Europe 1.00 and 1.01, and Japan 1.00 and 1.02. USA 1.01, Hong Kong and Asia find their cheat sites by scanning `eboot.bin`

The cheats are the port's own implementation. Initial research was contributed by the reporter of [Issue #20](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/20), which remains open

## Launcher improvements

- **Portuguese (Brazil)** launcher text
- **Bug Report** page: previews exactly what would be sent (a description and one privacy-filtered log), keeps your sent reports in **My Reports** and can save the same report as a text file
- **Controller bindings**: every controller button can be rebound, with **Reset to defaults**
- **Controller prompts**: the game's PlayStation button pictures or Xbox prompts
- **Build diagnostics**: for a build that is refused, detailed read-only diagnostics are written to the log, holding hashes and metadata and never the game's content
- **Settings** are kept in `user\osl.ini`, and **Reset all settings** is on the About page
- A modernized launcher layout, with sidebar icons and header art

Controller rebinding addresses [Issue #17](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/17), which remains open on GitHub while waiting for reporter confirmation

## Updater improvements

- Downloads are accepted only over HTTPS from GitHub's release hosts and this project's release assets
- A stalled transfer is cancelled and can be retried
- The SHA-256 is checked before anything is installed
- On Linux, the AppImage you started is replaced and started again

## Graphics and frame rate

- **Renderer** choice: Vulkan stays the supported default. **Auto**, **OpenGL** (experimental) and, on Windows, **Direct3D 11** through ANGLE (experimental) are also available
- **Frame rate**: 30 to 240 FPS or Unlimited. The game still makes 60 new frames a second, so game speed is unchanged
- **GPUs without sparse buffer binding** now use fully resident buffer arenas instead of sparse binding, which lost the device on the first bind. GPUs with sparse binding keep the existing path
- The launcher now opens on systems without Vulkan 1.3, and **Play** says why Vulkan cannot be used and points to the other renderers
- The Vulkan renderer still requires Vulkan 1.3. The OpenGL renderer needs OpenGL 3.3 core, and the Direct3D 11 renderer needs a card with Direct3D feature level 10_1 or newer

## Stability

- **Audio resync after sleep or a device stall**: a cap on the audio backlog brings the sound back in sync with the picture after the PC resumes from sleep
- **Pipeline background warm-up**: with pipeline binaries, they are restored by a background warm-up first, and any pipeline it has not built is still compiled when the game needs it
- Compatibility with the current MSYS2 CLANG64 toolchain

### Integrated pull requests

The fixes below are based on community pull requests. The changes were written into this port, and the pull requests themselves were not merged on GitHub

- [PR #21](https://github.com/saulob/odin-sphere-leifthrasir-pc/pull/21) by N0rule, "buffer_cache: fall back to fully resident arenas on drivers without sparse binding". Integrated fixes based on PR #21 give GPUs and drivers without sparse buffer binding, such as the Intel Gen9 and Gen9.5 integrated graphics on Mesa described in the PR, a fully resident arena fallback. This has not been confirmed on the original reporter's hardware
- [PR #24](https://github.com/saulob/odin-sphere-leifthrasir-pc/pull/24) by KlaPeWin, "Fix A/V desync after resume from sleep and pipeline hitches with pipeline binaries". Integrated fixes based on PR #24 add the audio resync and the background pipeline warm-up. This has not been confirmed on handhelds

## Supported game versions

| Region | Title ID | Versions |
| --- | --- | --- |
| USA | `CUSA05083` | 1.00 and 1.01 |
| Japan | `CUSA01290` | 1.00 and 1.02 |
| Europe | `CUSA05262` | 1.00 and 1.01 |
| Hong Kong | `CUSA03873` | 1.00 |
| Asia (Korean archives) | `CUSA03873` | 1.00 |

The two `CUSA03873` 1.00 releases share their package metadata, so the launcher tells them apart by `eboot.bin` and gives each its own game folder

USA 1.00, Japan 1.00 and 1.02, and Europe 1.00 and 1.01 were tested on real files. USA 1.01, Hong Kong and Asia had no real local files for some validations: they find their executable sites by scanning `eboot.bin` when installed, and are supported from package metadata, those scans, the reporters' diagnostics and synthetic test data

Those are not failed tests, only checks that could not run on real data here

Any other version is refused, and the log then holds diagnostic information useful for adding it

## Windows

- Windows 10 version 1903 or newer, or Windows 11, 64-bit
- Vulkan 1.3 for the default renderer
- Direct3D 11 through ANGLE is Windows-only by design
- The Windows executable is not digitally signed, so SmartScreen can warn on first launch

## Linux / Steam Deck

Linux remains an experimental native x86-64 AppImage build

It does **not** use Wine or Proton

The AppImage contains the same shared features as the Windows release, built from the same code and checked by the same automated tests: Portuguese (Brazil) text and art in Classic and Refined, all cheats and the F10 menu, controller rebinding and prompts, the Bug Report page, the updater, every supported build, and the graphics, PR #21 and PR #24 fixes

Differences by design: Direct3D 11 / ANGLE and the `hang_NNN.txt` freeze report are Windows-only, Linux updates replace the AppImage instead of using the Windows updater helper, and Linux uses the system `curl` for the Bug Report page and the updater

The AppImage keeps the Ubuntu 20.04 compatibility target, with a glibc 2.31 baseline. Final binary inspection found a maximum required GLIBC symbol version of **2.30**

The AppImage now also carries AppStream metadata

It has not yet been tested on real Linux PCs or a Steam Deck

Platform parity is documented in [docs/PLATFORM_PARITY.md](https://github.com/saulob/odin-sphere-leifthrasir-pc/blob/v0.3.0/docs/PLATFORM_PARITY.md)

## Issues and community reports

Community reports relevant to this release include

- [Issue #17](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/17) Rebind Any Action to Any Button: controller buttons can be rebound, addressed in v0.3.0 and awaiting reporter confirmation
- [Issue #18](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/18) Missing Portuguese characters: addressed by the built-in Portuguese (Brazil) support and the drawing of the missing letters, awaiting reporter confirmation
- [Issue #19](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/19) Unsupported build `CUSA03873` 1.00 (HK/Asia) with Korean data: the Asia release is now supported next to Hong Kong, awaiting reporter confirmation
- [Issue #20](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/20) Cheat codes: native cheats and the F10 menu were added, awaiting reporter confirmation
- [Issue #23](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/23) Crash on a PC exposing only Vulkan 1.0: partially addressed. The launcher no longer stops there and explains why Vulkan cannot be used, but the Vulkan renderer still needs Vulkan 1.3. The experimental OpenGL renderer does not need it, and has not been confirmed on that PC
- [Issue #16](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/16) Others APIs: partially addressed. The experimental OpenGL renderer and, on Windows, Direct3D 11 through ANGLE were added. DirectX 12 and Vulkan 1.2 are not part of this release
- [Issue #13](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/13) QOL suggestions: partially addressed. The frame rate part is in (up to 240 FPS or Unlimited). HD texture pack support and DLSS are not

All of these issues remain open on GitHub

## Known issues

- Vulkan 1.3 is still required by the supported renderer ([Issue #23](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/23)). The OpenGL and Direct3D 11 renderers are experimental
- HD texture pack support and DLSS are not available ([Issue #13](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/13)), and DirectX 12 is not available ([Issue #16](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/16))
- A few painted Portuguese (Brazil) screens are still English, and USA 1.01, Hong Kong and Asia have no measured font data
- The Linux AppImage is experimental and has not yet been tested on real Linux PCs or a Steam Deck
- The fixes integrated from PR #21 and PR #24 are not yet confirmed on the original reporters' hardware

## Downloads

### Windows 10 / 11 x64

[Download OdinSphereLeifthrasirPC-v0.3.0-windows-x64.zip](https://github.com/saulob/odin-sphere-leifthrasir-pc/releases/download/v0.3.0/OdinSphereLeifthrasirPC-v0.3.0-windows-x64.zip)

[Windows SHA-256 file](https://github.com/saulob/odin-sphere-leifthrasir-pc/releases/download/v0.3.0/OdinSphereLeifthrasirPC-v0.3.0-windows-x64.sha256)

Extract the ZIP into a writable folder (not `Program Files`) and run **odin_sphere_leifthrasir.exe**

### Linux x86-64 / Steam Deck

[Download OdinSphereLeifthrasirPC-v0.3.0-x86_64.AppImage](https://github.com/saulob/odin-sphere-leifthrasir-pc/releases/download/v0.3.0/OdinSphereLeifthrasirPC-v0.3.0-x86_64.AppImage)

[AppImage SHA-256 file](https://github.com/saulob/odin-sphere-leifthrasir-pc/releases/download/v0.3.0/OdinSphereLeifthrasirPC-v0.3.0-x86_64.sha256)

Make the AppImage executable and run it

Full installation and configuration information is in the [README](https://github.com/saulob/odin-sphere-leifthrasir-pc#readme)

## Security

The exact final v0.3.0 release artifacts were checked with VirusTotal

At the time of validation all four artifacts reported **0 malicious** and **0 suspicious** detections

VirusTotal is an additional verification signal and not an absolute guarantee of safety

| File | SHA-256 | VirusTotal |
| --- | --- | --- |
| `OdinSphereLeifthrasirPC-v0.3.0-windows-x64.zip` | `2ee353cbc9b7abb18852192a35ce3c8b09b7c981a293f8cf2cd5218b2a557a7b` | [VirusTotal](https://www.virustotal.com/gui/file/2ee353cbc9b7abb18852192a35ce3c8b09b7c981a293f8cf2cd5218b2a557a7b?nocache=1) |
| `OdinSphereLeifthrasirPC-v0.3.0-x86_64.AppImage` | `551a81ccd688e1581e9c18cad148116c091fc7da05e2eb3ff2d0ed0e0cad0799` | [VirusTotal](https://www.virustotal.com/gui/file/551a81ccd688e1581e9c18cad148116c091fc7da05e2eb3ff2d0ed0e0cad0799?nocache=1) |
| `odin_sphere_leifthrasir.exe` (inside the ZIP) | `cbb453635da6a67a56551a78477f15e69706fde85f37324908c373e636cfcb27` | [VirusTotal](https://www.virustotal.com/gui/file/cbb453635da6a67a56551a78477f15e69706fde85f37324908c373e636cfcb27?nocache=1) |
| `trg-updater.exe` (inside the ZIP) | `7a1bc7beaeb6fa047bbe1aeef0040c85d3de1039fa3032cb60d83a315de66eaa` | [VirusTotal](https://www.virustotal.com/gui/file/7a1bc7beaeb6fa047bbe1aeef0040c85d3de1039fa3032cb60d83a315de66eaa) |

Final inspection of the public downloads found no `DEBUG_SAULO` markers, local repository paths, user saves, logs, game data, PDB or development symbol files, RenderDoc binaries, development Vulkan layers, or credentials and private tokens

## Checksums

```
2ee353cbc9b7abb18852192a35ce3c8b09b7c981a293f8cf2cd5218b2a557a7b  OdinSphereLeifthrasirPC-v0.3.0-windows-x64.zip
551a81ccd688e1581e9c18cad148116c091fc7da05e2eb3ff2d0ed0e0cad0799  OdinSphereLeifthrasirPC-v0.3.0-x86_64.AppImage
```

The matching `.sha256` files are also attached to the release, and the updater uses them to verify what it downloads

## Changes since v0.2.3

[Compare v0.2.3...v0.3.0](https://github.com/saulob/odin-sphere-leifthrasir-pc/compare/v0.2.3...v0.3.0)

Thank you to everyone who reported problems, tested builds and contributed fixes and research, including the authors of the reports and pull requests linked above

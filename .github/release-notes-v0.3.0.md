# Odin Sphere Leifthrasir PC v0.3.0

Odin Sphere Leifthrasir for PlayStation 4 as a native PC port, for Windows and, experimentally, Linux and Steam Deck.
**No game files are included.** You need your own legally obtained Odin Sphere Leifthrasir PS4 package.

If you use v0.2.3, the launcher offers this release with **UPDATE NOW**. Your game files, saves, settings and logs are kept.

## Highlights
- **Portuguese (Brazil)**: a complete, original translation of the menus, subtitles and story, with the in-game art that
  carries words redrawn in Portuguese, in both Refined and Classic mode.
- **Native cheats**: seven optional cheats and an Experience Multiplier, set in the launcher and changed in game with the
  **F10** menu, in Refined and Classic mode.
- **Controller rebinding**, a **Bug Report** page in the launcher and a modernized launcher.
- **New supported build**: `CUSA03873` 1.00 Asia, next to Hong Kong.
- **Stability and compatibility**: audio resync after sleep, background pipeline warm-up, Vulkan on GPUs without sparse
  buffer binding, and a launcher that opens on systems without Vulkan 1.3.

## Portuguese (Brazil)
- **Text**: menus, subtitles, story text, archive pages, credit headings and trophies, translated from the game's official
  English. The launcher and the port's in-game screens follow the language. Choose it under **Menu & Subtitles**, where it
  is listed right after English on every build.
- **Visual assets**: the title menu (NOVO JOGO, CARREGAR, SAIR DO JOGO, PRESSIONE QUALQUER BOTÃO), HUD and menu tabs,
  save and load screens, stage results, stage and location cards, chapter and act titles, the chapter prologue pages,
  Mode Select, the kitchen, the world map and more, redrawn from the game's own letters. Refined and Classic mode are
  both covered.
- **Accents**: letters the game's fonts lack (`ã õ Ã Õ Ç` and others) are drawn into your own font atlas in memory
  ([issue #18](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/18)). No game file is written.
- **Verified on**: the real files of USA 1.00, Europe 1.00 and 1.01, and Japan 1.00 and 1.02. USA 1.01, Hong Kong and Asia
  use the same translation, but their executables' fonts have not been measured, so a record that needs a letter the
  font lacks keeps the game's own text there. On Japan 1.00 and 1.02, records not proven to be the English text keep Japanese.
- **Still in English**: the Bookshelf book frames, the manual pages, the Skill screen and the Classic logo and ending layouts.

## Cheats
- **Max Gold Gain**, **Max Phozon Gain**, **No AP Consumption**, **Infinite PP**, **Infinite POW**, **Infinite Health**
  and **Infinite Arrows**, all Off by default, plus an **Experience Multiplier** (Off, 2x, 5x, 10x or 100x).
- The launcher's **Cheats** page sets the state the game starts with. **F10** opens the in-game **CHEATS** menu, whose
  changes apply at once and last for the current session. The key can be changed on the Controls page.
- Refined and Classic mode both work. The cheats change a few instructions of the game's code in memory, found by their
  code and checked first; no game file or save is modified. A cheat whose code is not found exactly once in your build
  shows as **Unavailable**, and the log says why.
- Classic mode support is new in this release. In Classic, **No AP Consumption** is already true (it has no AP), and
  experience comes from food and items only, so the multiplier has nothing to multiply for defeated enemies.

## Launcher and diagnostics
- **Modernized launcher**: sidebar icons, header art and a compact layout, with the launcher in Portuguese when that
  language is chosen.
- **Bug Report page**: previews exactly what would be sent (a description and one privacy-filtered log), keeps your sent
  reports in **My Reports** and can save the same report as a text file.
- **Controller bindings**: every controller button can be rebound, with **Reset to defaults**
  ([issue #17](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/17)).
- **Unsupported builds**: detailed read-only diagnostics, written to the log, for any build that is refused. They hold
  hashes and metadata, never the game's content.
- **Updater**: downloads are accepted only over HTTPS from GitHub's release hosts and this project's release assets, a
  stalled transfer is cancelled and can be retried, and the SHA-256 is checked before anything is installed.
- **Renderer and frame rate**: a **Renderer** choice with experimental OpenGL and, on Windows, Direct3D 11 renderers, and a
  **Frame rate** setting (30 to 240 FPS or Unlimited; game speed is unchanged).

## Compatibility and performance
- **GPUs without sparse buffer binding** (integrated Intel GPUs on some Mesa drivers, for example): the video core
  now uses fully resident buffer arenas instead of sparse binding, which lost the device on the first bind. GPUs with
  sparse binding keep the existing path. Integrated fixes based on
  [PR #21](https://github.com/saulob/odin-sphere-leifthrasir-pc/pull/21) by N0rule; not yet confirmed on the original
  reporter's hardware.
- **Audio after sleep**: a cap on the SDL audio queue brings the sound back in sync with the picture after the PC resumes
  from sleep. **Pipeline hitches**: with pipeline binaries, they are restored by a background warm-up first, and any
  pipeline it has not built is still compiled when the game needs it. Integrated fixes based on
  [PR #24](https://github.com/saulob/odin-sphere-leifthrasir-pc/pull/24) by KlaPeWin; not yet confirmed on handhelds.
- **Systems without Vulkan 1.3**: the launcher opens instead of stopping, and Play says why Vulkan cannot be used and
  hints at the other renderers ([issue #23](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/23)).

Neither PR was merged: the changes were written into this port.

## Supported builds
| Build | Title ID | Version |
| --- | --- | --- |
| USA | `CUSA05083` | 1.00 and 1.01 |
| Japan | `CUSA01290` | 1.00 and 1.02 |
| Europe | `CUSA05262` | 1.00 and 1.01 |
| Hong Kong | `CUSA03873` | 1.00 |
| Asia | `CUSA03873` | 1.00 (Korean archives) |

The two `CUSA03873` 1.00 releases share their package metadata; the launcher tells them apart by `eboot.bin` and gives
each its own game folder. USA 1.01, Hong Kong and Asia find their executable sites by scanning `eboot.bin` when installed,
and are supported from package metadata, those scans and the reporters' diagnostics; the other builds are also tested on
real files. Any other version is refused, and the log then holds diagnostic information useful for adding it.

## Fixes
- **Hong Kong title menu**: the Exit Game row's label art is corrected across builds, so it reads EXIT GAME (SAIR DO JOGO
  in Portuguese) instead of INSTALL.

## Windows
- `OdinSphereLeifthrasirPC-v0.3.0-windows-x64.zip` holds one folder. Extract it where you can write to (not
  `Program Files`) and run `odin_sphere_leifthrasir.exe`.

## Linux / Steam Deck
- `OdinSphereLeifthrasirPC-v0.3.0-x86_64.AppImage` (experimental, glibc 2.31 or newer). It has been built and checked by
  scripts but not tested on real Linux PCs or a Steam Deck. The Bug Report page needs `curl` on Linux.

## Downloads
- **Windows:** `OdinSphereLeifthrasirPC-v0.3.0-windows-x64.zip`
- **Linux / Steam Deck (experimental):** `OdinSphereLeifthrasirPC-v0.3.0-x86_64.AppImage`, glibc 2.31 or newer

Installation steps, requirements and known limitations are in the
[README](https://github.com/saulob/odin-sphere-leifthrasir-pc#readme).

## GitHub issues
- [#4 - Hong Kong build (`CUSA03873`)](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/4): the Exit Game
  label reported there is corrected. It stays open until the reporter has tested v0.3.0.
- [#13 - QOL suggestions](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/13): the frame rate part is in
  (up to 240 FPS or Unlimited). HD texture packs and DLSS are not.
- [#16 - Others APIs](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/16): experimental OpenGL and Direct3D
  11 renderers can run on systems without Vulkan 1.3; the Vulkan renderer still needs it. DirectX 12 is not part of this.
- [#17 - Rebind Any Action to Any Button](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/17): controller
  buttons can be rebound.
- [#18 - Missing Portuguese characters](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/18): the port draws
  the missing letters itself.
- [#19 - CUSA03873 1.00 with Korean data](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/19): supported as
  **CUSA03873 1.00 Asia**.
- [#20 - Cheat codes](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/20): native cheats and the F10 menu.
  Initial research for them came from the reporter.
- [#23 - Crash with code 23 on a system with Vulkan 1.0](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/23): the launcher
  no longer stops there.

### Security

Each release file is scanned with VirusTotal before publication. A VirusTotal scan is an additional check, not a
guarantee of safety, so please also verify the SHA-256 of your download.

| File | VirusTotal |
| --- | --- |
| `OdinSphereLeifthrasirPC-v0.3.0-windows-x64.zip` | TBD after packaging |
| `odin_sphere_leifthrasir.exe` (Windows, inside the ZIP) | TBD after packaging |
| `trg-updater.exe` (Windows, inside the ZIP) | TBD after packaging |
| `OdinSphereLeifthrasirPC-v0.3.0-x86_64.AppImage` | TBD after packaging |

## Checksums
| File | SHA-256 |
| --- | --- |
| `OdinSphereLeifthrasirPC-v0.3.0-windows-x64.zip` | TBD after packaging |
| `odin_sphere_leifthrasir.exe` (inside the ZIP) | TBD after packaging |
| `trg-updater.exe` (inside the ZIP) | TBD after packaging |
| `OdinSphereLeifthrasirPC-v0.3.0-x86_64.AppImage` | TBD after packaging |

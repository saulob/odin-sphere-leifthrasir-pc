# Odin Sphere Leifthrasir PC v0.2.0

Odin Sphere Leifthrasir for PlayStation 4 as a native PC port, for Windows and, experimentally, Linux and Steam Deck.
**No game files are included.** You need your own legally obtained Odin Sphere Leifthrasir PS4 package.

If you use v0.1.0, the launcher offers this release with **UPDATE NOW**. Your game files, saves, settings and logs are kept.

## Highlights
- **Unlock Bookshelf** (Gameplay, Off by default): the attic Bookshelf is usable from the start, with every entry and the
  Special artwork. Your save is not changed, trophies use your real progression, and turning it Off restores what your
  save has earned.
- **Older CPUs**: processors without MOVBE, BMI1 or LZCNT, such as Intel Ivy Bridge, now run the game.
- **Text Archive** pages are no longer magnified or cropped at render resolutions above native.
- **Game hang fixed**: an event synchronization problem that could freeze the game during Netherworld progression.
- **Xbox controller prompts** now cover more of the game's screens in all supported regions.
- **F2** shows or hides the in-game frame counter.
- **Startup hints** confirm Skip intro and Unlock Bookshelf when they are on.
- **Improved launcher** and **diagnostics for unsupported builds**.

## Fixes and compatibility
- Intel Ivy Bridge and other older CPUs: MOVBE and BMI1 instructions are emulated and LZCNT is rewritten for all
  supported builds. A processor with AVX and POPCNT is still required.
- Text Archive pages scale correctly at non-native render resolutions; native 1080p is unchanged.
- Event flag synchronization fix for the freeze reported during Netherworld progression.
- Controller prompt archives are handled across the supported regions.
- The Windows launcher no longer opens an unwanted console window.
- Guest memory on Windows is committed on demand.

## Unsupported versions
Any other Odin Sphere Leifthrasir build is still refused. It now writes diagnostic information to the log, which helps
add future versions. **CUSA05083 1.01 (USA) is not supported yet.**

## Supported builds
| Build | Title ID | Version |
| --- | --- | --- |
| USA | `CUSA05083` | 1.00 |
| Japan | `CUSA01290` | 1.00 and 1.02 |
| Europe | `CUSA05262` | 1.00 and 1.01 |

## Downloads
- **Windows:** `OdinSphereLeifthrasirPC-v0.2.0-windows-x64.zip`
- **Linux / Steam Deck (experimental):** `OdinSphereLeifthrasirPC-v0.2.0-x86_64.AppImage`, glibc 2.31 or newer

Installation steps, requirements and known limitations are in the
[README](https://github.com/saulob/odin-sphere-leifthrasir-pc#readme); they are unchanged from v0.1.0.

## Fixed GitHub issues
- [#2](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/2) Strange behaviour in the storybook text:
  fixed Text Archive and Storybook rendering at non-native render resolutions, so text and page regions no longer
  appear magnified, cropped or stretched.
- [#3](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/3) Xbox controller prompts: fixed prompts not being
  applied correctly to some localized Remaster archives, including Spanish, French and Italian.
- [#6](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/6) Game Hang: fixed event flag synchronization that
  could cause a black-screen hang when progressing through the Netherworld in Valkyrie Chapter Epilogue Act III.
- [#7](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/7) Game crashes when opening on older PC: added
  compatibility for older CPUs through MOVBE and BMI1 emulation and LZCNT rewriting, including systems such as Intel
  Ivy Bridge.

## Still being investigated
- [#5](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/5) CUSA05083 1.01 USA support: this build is not
  supported yet. v0.2.0 adds automatic diagnostics for unsupported Odin Sphere Leifthrasir builds, so users can provide
  the log needed to map new versions.
- [#4](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/4) CUSA03873 1.00 Hong Kong support: this build is
  not supported yet and remains under investigation.

## Checksums
| File | SHA-256 |
| --- | --- |
| `OdinSphereLeifthrasirPC-v0.2.0-windows-x64.zip` | `90454b682d0f29d66d972dd8184805ec3754c7f2592ae1d07e145815afb6bc9a` |
| `OdinSphereLeifthrasirPC-v0.2.0-x86_64.AppImage` | `ab1d9b5dffddf2f06b010e745e3091ba783ea57c101ba689dbc696afa212205e` |

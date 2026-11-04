# Odin Sphere Leifthrasir PC v0.2.2

Odin Sphere Leifthrasir for PlayStation 4 as a native PC port, for Windows and, experimentally, Linux and Steam Deck.
**No game files are included.** You need your own legally obtained Odin Sphere Leifthrasir PS4 package.

If you use v0.2.1, the launcher offers this release with **UPDATE NOW**. Your game files, saves, settings and logs are kept.

## Highlights
- **EXIT GAME** is now in the native title menu, in Refined and Classic mode, on Windows and Linux.
- **Xbox controller prompts** now cover every supported build, including copies of the game files that differ slightly
  from the ones the patch was originally built from.
- **CUSA03873 1.00 Hong Kong**: Traditional Chinese and Korean are now selectable as the game's text language.
- **USA 1.01 (`CUSA05083`)**: package and update detection is corrected.

## Fixes and compatibility
- **EXIT GAME**: leaves the game cleanly from the title menu, in both the Refined and Classic layouts. The label is
  drawn from each build's own title art and follows the title language where its letters allow (Italian, Spanish,
  French and German); other languages show EXIT GAME. Switching controller prompts afterwards does not remove or
  break this option.
- **Hong Kong (`CUSA03873`)**: the Menu & Subtitles language can now be changed, alongside the European build.
  Traditional Chinese is the default, and Korean is selectable.
- **USA 1.01 (`CUSA05083`)**: fixes a case where installing the 1.01 update package over the 1.00 base, or installing
  a complete 1.01 package by itself, was not detected correctly. Diagnostics now clearly report whether a package is
  recognized as a base, an update or a complete package.
- **Controller prompts**: Xbox prompts can now be applied to any supported build, even a legitimately obtained copy
  whose game files do not exactly match the ones the prompt patch was built against. An unrecognized or ambiguous
  copy is left unpatched rather than risking a corrupted file, and the log now reports more clearly when and why
  prompts are unavailable for a build.
- **USA 1.01 LZCNT diagnostics**: the launcher's diagnostics for older, pre-LZCNT processors on USA 1.01 are more
  detailed. This has not yet been confirmed on real pre-LZCNT hardware running USA 1.01.
- For the builds whose executable patch sites are found by scanning `eboot.bin` rather than from a fixed table (USA
  1.01, Hong Kong 1.00), a feature whose sites the scan cannot find uniquely is hidden instead of being applied, and
  the log holds the scan.
- The Windows development build script's `-Run` is quiet by default now: the runtime's output goes to its log file
  only, and the terminal shows just the log path and the exit code. `-VerboseRuntime` streams it live again.

## Supported builds
| Build | Title ID | Version |
| --- | --- | --- |
| USA | `CUSA05083` | 1.00 and 1.01 |
| Japan | `CUSA01290` | 1.00 and 1.02 |
| Europe | `CUSA05262` | 1.00 and 1.01 |
| Hong Kong | `CUSA03873` | 1.00 |

Any other version is refused, and the log then holds diagnostic information useful for adding it.

## Downloads
- **Windows:** `OdinSphereLeifthrasirPC-v0.2.2-windows-x64.zip`
- **Linux / Steam Deck (experimental):** `OdinSphereLeifthrasirPC-v0.2.2-x86_64.AppImage`, glibc 2.31 or newer

Installation steps, requirements and known limitations are in the
[README](https://github.com/saulob/odin-sphere-leifthrasir-pc#readme).

## GitHub issues
- [#9 - Error when trying to install v1.01 pkg](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/9): the
  USA 1.01 update package's identity and its relation to the 1.00 base package are now detected correctly.
- [#10 - Please add language selection support for CUSA03873 as well](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/10):
  Traditional Chinese (default) and Korean are now selectable for the Hong Kong build.

### Security

The final release artifacts were scanned with VirusTotal before publication. A VirusTotal scan is an additional
check, not a guarantee of safety, so please also verify the SHA-256 of your download.

| File | VirusTotal |
| --- | --- |
| `OdinSphereLeifthrasirPC-v0.2.2-windows-x64.zip` | https://www.virustotal.com/gui/file/f85f36c85185246a3fe283aa2109184ae3ab26aa56ce0b237724dc7cd1e167ea?nocache=1 |
| `OdinSphereLeifthrasirPC-v0.2.2-x86_64.AppImage` | https://www.virustotal.com/gui/file/3fc2111f31a057cfee02fc93d66766b977f26a4c8ebacada2d43201e34ed0c4d?nocache=1 |

## Checksums
| File | SHA-256 |
| --- | --- |
| `OdinSphereLeifthrasirPC-v0.2.2-windows-x64.zip` | `f85f36c85185246a3fe283aa2109184ae3ab26aa56ce0b237724dc7cd1e167ea` |
| `OdinSphereLeifthrasirPC-v0.2.2-x86_64.AppImage` | `3fc2111f31a057cfee02fc93d66766b977f26a4c8ebacada2d43201e34ed0c4d` |

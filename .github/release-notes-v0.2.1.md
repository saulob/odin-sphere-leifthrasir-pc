# Odin Sphere Leifthrasir PC v0.2.1

Odin Sphere Leifthrasir for PlayStation 4 as a native PC port, for Windows and, experimentally, Linux and Steam Deck.
**No game files are included.** You need your own legally obtained Odin Sphere Leifthrasir PS4 package.

If you use v0.2.0, the launcher offers this release with **UPDATE NOW**. Your game files, saves, settings and logs are kept.

## Highlights
- **CUSA03873 1.00 Hong Kong** is supported.
- **CUSA05083 1.01 USA** is supported.
- **Netherworld hang follow-up**: a second cause of the freeze reported in Issue #6 is fixed.

## Fixes and compatibility
- **Hong Kong 1.00 (`CUSA03873`)**: the base package installs and runs. The build has one fixed text language.
- **USA 1.01 (`CUSA05083`)**: install the base package and then the update package, or a complete 1.01 package by
  itself. It keeps the save and trophy identity of USA 1.00.
- For these two builds the launcher finds the executable patch sites by scanning their `eboot.bin` when the game is
  installed. A feature whose sites cannot be found uniquely is hidden instead of being applied, and the log holds the scan.
- Issue #6 follow-up: the v0.2.0 event flag fix did not fully resolve the reporter's case. The newer hang report shows an
  additional path, in which the game cancels a worker thread (`scePthreadCancel`) while that thread is waiting. v0.2.1
  implements thread cancellation in the runtime: a cancelled thread now leaves a sleep, a condition wait (timed or not)
  or a join, and cancelling a thread that has finished, is detached or is being joined is handled. It is the same
  runtime code for every supported build, on Windows and on Linux.
- A package or game folder of an Odin Sphere Leifthrasir version that is not supported is still refused, and the
  diagnostics now cover more of the cases.

## Supported builds
| Build | Title ID | Version |
| --- | --- | --- |
| USA | `CUSA05083` | 1.00 and 1.01 |
| Japan | `CUSA01290` | 1.00 and 1.02 |
| Europe | `CUSA05262` | 1.00 and 1.01 |
| Hong Kong | `CUSA03873` | 1.00 |

Any other version is refused, and the log then holds diagnostic information useful for adding it.

## Downloads
- **Windows:** `OdinSphereLeifthrasirPC-v0.2.1-windows-x64.zip`
- **Linux / Steam Deck (experimental):** `OdinSphereLeifthrasirPC-v0.2.1-x86_64.AppImage`, glibc 2.31 or newer

Installation steps, requirements and known limitations are in the
[README](https://github.com/saulob/odin-sphere-leifthrasir-pc#readme).

## GitHub issues
- [#4](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/4) CUSA03873 1.00 Hong Kong support: added.
- [#5](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/5) CUSA05083 1.01 USA support: added.
- [#6](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/6) Game Hang: v0.2.1 addresses an additional thread
  cancellation path found in the newer hang report. This has not been verified by the reporter yet; it needs their test
  of this release.

### Security

The final release artifacts were scanned with VirusTotal before publication. A VirusTotal scan is an additional check, not a guarantee of safety, so please also verify the SHA-256 of your download.

| File | VirusTotal |
| --- | --- |
| `OdinSphereLeifthrasirPC-v0.2.1-windows-x64.zip` | https://www.virustotal.com/gui/file/45a401a227b8e2e9444e0c0d3edd38a2c1faf34b5edbc7e361f19cf5b6c58014 |
| `OdinSphereLeifthrasirPC-v0.2.1-x86_64.AppImage` | https://www.virustotal.com/gui/file/ad348500a75045c6416005fbce811d140a6b103314ca2ea4f1d331f57d8bdaee |

## Checksums
| File | SHA-256 |
| --- | --- |
| `OdinSphereLeifthrasirPC-v0.2.1-windows-x64.zip` | `45a401a227b8e2e9444e0c0d3edd38a2c1faf34b5edbc7e361f19cf5b6c58014` |
| `OdinSphereLeifthrasirPC-v0.2.1-x86_64.AppImage` | `ad348500a75045c6416005fbce811d140a6b103314ca2ea4f1d331f57d8bdaee` |

# Odin Sphere Leifthrasir PC v0.2.3

Odin Sphere Leifthrasir for PlayStation 4 as a native PC port, for Windows and, experimentally, Linux and Steam Deck.
**No game files are included.** You need your own legally obtained Odin Sphere Leifthrasir PS4 package.

If you use v0.2.2, the launcher offers this release with **UPDATE NOW**. Your game files, saves, settings and logs are kept.

## Highlights
- **Hong Kong (`CUSA03873`) Xbox controller prompts**: Xbox prompt compatibility is improved for the Hong Kong 1.00
  game files reported in issue #4, in Refined and Classic mode.
- **Stricter release security checks**: the Windows package, both Windows executables and the Linux AppImage are now
  scanned separately before a release is published.

## Fixes and compatibility
- **Hong Kong controller prompts**: `CUSA03873` 1.00 remains a supported build. Xbox prompt compatibility is
  improved for the known Hong Kong button-art file reported in issue #4. Any other file is still checked as strictly as
  before. The fix is based on that file's known identity and on the project's synthetic test coverage; it has not yet
  been confirmed on a real Hong Kong installation, and the launcher's log says when this compatibility path is used.
- **Release security**: the v0.2.2 Windows executable triggered a Microsoft Defender machine-learning detection on
  VirusTotal while other tested builds with the same source did not. No malicious or unexpected code was found during
  review. v0.2.3 introduces stricter release verification: the Windows package and its executables are scanned
  separately, because a clean package scan does not cover the programs inside it.

## Supported builds
| Build | Title ID | Version |
| --- | --- | --- |
| USA | `CUSA05083` | 1.00 and 1.01 |
| Japan | `CUSA01290` | 1.00 and 1.02 |
| Europe | `CUSA05262` | 1.00 and 1.01 |
| Hong Kong | `CUSA03873` | 1.00 |

Any other version is refused, and the log then holds diagnostic information useful for adding it.

## Downloads
- **Windows:** `OdinSphereLeifthrasirPC-v0.2.3-windows-x64.zip`
- **Linux / Steam Deck (experimental):** `OdinSphereLeifthrasirPC-v0.2.3-x86_64.AppImage`, glibc 2.31 or newer

Installation steps, requirements and known limitations are in the
[README](https://github.com/saulob/odin-sphere-leifthrasir-pc#readme).

## GitHub issues
- [#4 - Hong Kong build (`CUSA03873`)](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/4): Hong Kong
  controller prompt compatibility is improved. It stays open until the reporter has tested v0.2.3.
- [#15 - Microsoft Defender detection of the v0.2.2 executable](https://github.com/saulob/odin-sphere-leifthrasir-pc/issues/15):
  release-security and false-positive investigation. v0.2.3 is scanned file by file; the result is listed below.

### Security

Each release file is scanned with VirusTotal before publication. A VirusTotal scan is an additional check, not a
guarantee of safety, so please also verify the SHA-256 of your download.

| File | VirusTotal |
| --- | --- |
| `OdinSphereLeifthrasirPC-v0.2.3-windows-x64.zip` | [VirusTotal](https://www.virustotal.com/gui/file/b3712ecb5ad6b0b0cbf5ef8bc4c4b6cc8a1d117ebb3c2b2b40e48b0516853ac2?nocache=1) |
| `odin_sphere_leifthrasir.exe` (Windows, inside the ZIP) | [VirusTotal](https://www.virustotal.com/gui/file/6a45aee57185bf606620f6f359fdfd5413f55854a687f0366b4951be0ab06a66) |
| `trg-updater.exe` (Windows, inside the ZIP) | [VirusTotal](https://www.virustotal.com/gui/file/7a1bc7beaeb6fa047bbe1aeef0040c85d3de1039fa3032cb60d83a315de66eaa) |
| `OdinSphereLeifthrasirPC-v0.2.3-x86_64.AppImage` | [VirusTotal](https://www.virustotal.com/gui/file/6bf58d0cd7af0ff7dd32826ebd75b5f8b29db54f70a152c83af6aec5b7306a4d?nocache=1) |

## Checksums
| File | SHA-256 |
| --- | --- |
| `OdinSphereLeifthrasirPC-v0.2.3-windows-x64.zip` | `b3712ecb5ad6b0b0cbf5ef8bc4c4b6cc8a1d117ebb3c2b2b40e48b0516853ac2` |
| `odin_sphere_leifthrasir.exe` (inside the ZIP) | `6a45aee57185bf606620f6f359fdfd5413f55854a687f0366b4951be0ab06a66` |
| `trg-updater.exe` (inside the ZIP) | `7a1bc7beaeb6fa047bbe1aeef0040c85d3de1039fa3032cb60d83a315de66eaa` |
| `OdinSphereLeifthrasirPC-v0.2.3-x86_64.AppImage` | `6bf58d0cd7af0ff7dd32826ebd75b5f8b29db54f70a152c83af6aec5b7306a4d` |

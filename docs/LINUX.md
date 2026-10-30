# Linux port

The loader, runtime, video core and launcher are the same code on Linux x86-64 as on Windows (Dragon's Crown Pro PC's
Linux port, which this one follows). This page covers the build, the AppImage and what has been verified.

## Building

Releases are built on Ubuntu 20.04: its glibc (2.31) is the oldest the AppImage runs on. `packaging/linux/ci-deps.sh`
prepares a clean 20.04 system as root (Clang 20, the GCC 14 C++ library, CMake, and pinned, checksum-verified builds of
what 20.04 lacks: Vulkan-Headers, VMA, glslang, Boost headers, fmt, Zydis, xxHash, robin-map, OpenSSL's libcrypto, SDL3).

| Command | Result |
| --- | --- |
| `tools/build.sh` | fetches `gpu/deps.txt` and builds `build/cmake/linux/bin/odin_sphere_leifthrasir` (RelWithDebInfo, for development) |
| `tools/build.sh --release` | builds CMake's Release configuration in `build/cmake/linux-release`, the one that is packaged |
| `tools/build.sh --test` | also builds and runs the focused headless tests |
| `tools/package-linux.sh` | builds with `--release`, then writes `dist/OdinSphereLeifthrasirPC-v<version>-x86_64.AppImage` and `dist/OdinSphereLeifthrasirPC-v<version>-x86_64.sha256`; the program's debug symbols go to `dist/OdinSphereLeifthrasirPC-v<version>-x86_64-symbols/`, which is not packaged (`--out-dir DIR` writes all three elsewhere, `--clean` rebuilds) |
| `tools/package-linux.sh --verify-only FILE` | checks a finished AppImage: its runtime, then its extracted contents with the release check below |
| `tools/package-linux.sh --verify DIR` | only the release check of an AppDir |
| `tools/package-release-linux.sh` | for a checkout on a Windows drive, run in WSL2: mirrors the sources into `~/.cache/odin-sphere-leifthrasir-pc-build`, runs `package-linux.sh` there and copies the AppImage back into `dist/` |
| `tools/test-appdir-check.sh` | checks that the release check accepts a clean synthetic AppDir and refuses contaminated ones; nothing is built or started |

The version is `OSL_PORT_VERSION` in `CMakeLists.txt`, the only place it is written. The file names follow the
AppImageHub convention, `<name>-v<version>-x86_64.AppImage` (no `-linux-`), and the updater looks for exactly these.

A WSL2 release environment: import the Ubuntu 20.04 base image (`ubuntu-base-20.04.5-base-amd64.tar.gz` from
https://cdimage.ubuntu.com/ubuntu-base/releases/20.04/release/, listed in that folder's signed `SHA256SUMS`) and
prepare it once, from the repository folder:

```powershell
wsl --import OslFocalBuild <folder> ubuntu-base-20.04.5-base-amd64.tar.gz --version 2
wsl -d OslFocalBuild -u root -- bash packaging/linux/ci-deps.sh
wsl -d OslFocalBuild -u root -- bash tools/package-release-linux.sh
```

An Ubuntu 20.04 distribution already prepared by Dragon's Crown Pro PC's identical `ci-deps.sh` works as well.

## AppImage

`usr/bin/odin_sphere_leifthrasir` has `DT_RPATH=$ORIGIN/../lib` (the build's own RUNPATH is removed first) and finds
the bundled libraries there (SDL3, FreeType, libpng, libcrypto, fmt, Zydis, xxHash and their dependencies), each with
`RUNPATH=$ORIGIN`. Its debug sections are split off with `objcopy --only-keep-debug` into the symbols folder, and the
packaged copy keeps only a `.gnu_debuglink`. No `LD_LIBRARY_PATH` is set, except that `AppRun` adds
`usr/lib/compat` (libstdc++ and libgcc_s) when the system's copies are older than the build needs. glibc, the Vulkan
loader and drivers, X11/Wayland and the audio libraries always come from the player's system.

`package-linux.sh` refuses to package on a system with a newer glibc than 2.31, and checks the staged AppDir and the
written AppImage again: every file must be in an allowed place (AppRun, the desktop entry and icon, the program, the
launcher images, the bundled libraries, the licences and README); every ELF (found by content) must be x86-64, need no glibc
newer than 2.31 (its version-needs section), carry no debug sections, no absolute RPATH/RUNPATH and no RenderDoc or
Vulkan layer; the program must resolve every library (`ldd`) through `$ORIGIN/../lib`; and the package must hold no game
data, PS4 packages, user files, saves, trophies, logs, caches, hang reports, crash dumps, symbols, updater leftovers,
objects or static libraries, scripts, sources, Windows programs, links other than `.DirIcon`, glibc or system graphics
libraries, `DEBUG_SAULO` or local paths (searched as text and as UTF-16), and must hold the licences and README. The
extracted AppImage must hold exactly the staged files, byte for byte.

The data folders `game`, `user` and `logs` live next to the AppImage file (`$APPIMAGE`); a read-only folder falls back
to `$XDG_DATA_HOME/OdinSphereLeifthrasirPC`, and `OSL_PORTABLE_DIR` chooses the place explicitly (`launcher/paths.cpp`).
Nothing is written inside the AppImage's read-only mount. The updater replaces the original AppImage file, never the
mounted or extracted copy.

### FUSE and the AppImage runtime

The AppImage mounts itself with FUSE first. When that mount cannot be made, the patched runtime unpacks the AppImage into
a private folder under `/tmp`, runs `AppRun` from there with the same arguments, and removes the folder afterwards.
`APPIMAGE_EXTRACT_AND_RUN=1` (or `--appimage-extract-and-run`) forces that mode. `$APPIMAGE` keeps naming the original
file, so the data folders stay beside it.

- `patches/appimage-runtime/baseline.env` pins the upstream [AppImage/type2-runtime](https://github.com/AppImage/type2-runtime)
  commit (`dd6cebe`, tag `20251108`), the Alpine root file system the build runs in, and the SHA-256 of the reviewed
  result.
- `patches/appimage-runtime/0001-extract-and-run-when-fuse-fails.patch` adds the fallback.
- `tools/build-appimage-runtime.sh` builds it (root or passwordless `sudo`, network access the first time) and stops
  when the result does not match the pinned SHA-256; `--check FILE` verifies a runtime or a finished AppImage.
- appimagetool is pinned to 1.9.1 by SHA-256 and never downloads the unpinned "continuous" runtime.

These are the same pins and patch as Dragon's Crown Pro PC and Hard Corps: Uprising Recompiled.

## Differences from Windows

- The launcher restores its saved size, maximized and fullscreen states, but Wayland compositors choose the window
  position themselves.
- No `hang_NNN.txt` freeze report is written: the hang watchdog is Windows-only. `--timeout <seconds>` stops a run that
  is still going after that long and writes every thread's frame chain to the log (a development aid).

## Verification

Verified headlessly for v0.1.0, in a WSL2 Ubuntu 20.04 (glibc 2.31) build environment: the Release build and the
AppImage checks above (the newest glibc any shipped ELF needs is 2.30); the runtime built from the pins reproduces the
reviewed SHA-256; and, through a command-line mode of the program that exits before SDL, the launcher or the game start,
the FUSE mount, the automatic extract-and-run fallback (an unprivileged user without `fusermount`) and the forced one
(`APPIMAGE_EXTRACT_AND_RUN=1`) each forward the program's exit code and leave nothing behind in `/tmp`.

Not verified yet: the launcher and the game on any Linux desktop, distribution, GPU or driver, a Steam Deck, signal
forwarding in the extract-and-run fallback, and the updater replacing an AppImage.

# Windows and Linux feature parity (v0.3.0)

Every port feature is built from the same sources on both platforms: `src/cheats.c`, `src/runtime_cheats.cpp`, the
translation, Bug Report, controller, build scanner, updater core and visual patch code are in `osl_launcher_core` and
the loader with no platform condition. Only operating system services are split (see the last table).

Evidence: `tools/test.ps1` on Windows and `tools/build.sh --test` on Ubuntu 20.04 (glibc 2.31) run the same headless
suites; the real-file sections read the same local `game/prepared` and `build/image` folders. The embedded data (PT-BR
corpus, visual and title patches, controller prompts, page icons, trophy text) is byte-identical in both builds.

Status values: **PARITY VERIFIED** (same code, same test verdicts on both), **PLATFORM SPECIFIC BY DESIGN** (an OS
service with an equivalent on the other platform, or none possible), **DATA UNAVAILABLE** (no local game files for
that build, on either platform).

## Features

| Feature | Windows | Linux | Classic | Refined | Evidence |
| --- | --- | --- | --- | --- | --- |
| Portuguese (Brazil) text | PARITY VERIFIED | PARITY VERIFIED | yes | yes | `osl-text-tests` 4274/0 on both; same served counts per build |
| PT-BR visual overlays | PARITY VERIFIED | PARITY VERIFIED | yes | yes | same visual entries served (USA 190, Japan 192, Europe 397/419); embedded patches identical |
| PT-BR launcher text | PARITY VERIFIED | PARITY VERIFIED | n/a | n/a | `osl-ui-text-tests` 4002/0 on both |
| Cheats, all 8 | PARITY VERIFIED | PARITY VERIFIED | yes | yes | `osl-cheats-tests` cheat matrix 75 verified / 5 inherent / 48 data unavailable / 0 unknown on both |
| F10 cheat menu | PARITY VERIFIED | PARITY VERIFIED | yes | yes | menu logic and hotkey in `osl-cheats-tests` |
| Bug Report | PARITY VERIFIED | PARITY VERIFIED | n/a | n/a | `osl-bug-report-tests` on both; Linux also proves the "needs curl" error |
| Updater | PARITY VERIFIED | PARITY VERIFIED | n/a | n/a | `osl-update-tests` 394/0, `osl-update-network-tests` 184/0 on both |
| Controller rebinding, prompts, keyboard, hotkeys (F2, F10, F11) | PARITY VERIFIED | PARITY VERIFIED | yes | yes | `osl-tests` 8 builds PASS on both |
| Build scanner and supported builds | PARITY VERIFIED | PARITY VERIFIED | yes | yes | same 8 builds PASS in `osl-tests`; `osl-build-diagnostics-tests` on both |
| Skip Intro, Unlock Bookshelf, Exit Game title row | PARITY VERIFIED | PARITY VERIFIED | yes | yes | `osl-tests` build parity rows |
| Render resolution, aspect ratio, VSync, frame rate, frame counter | PARITY VERIFIED | PARITY VERIFIED | yes | yes | `osl-tests`, `osl-frame-pacing-tests` on both |
| Vulkan: SMAA, FSR 1, RCAS, anisotropic filtering | PARITY VERIFIED | PARITY VERIFIED | yes | yes | shared `video_core`; `osl-renderer-selection-tests` on both |
| OpenGL renderer (experimental) | PARITY VERIFIED | PARITY VERIFIED | yes | yes | `osl-opengl-backend-tests`, `osl-opengl-perf-tests` on both |
| Direct3D 11 renderer (experimental) | yes | PLATFORM SPECIFIC BY DESIGN | yes | yes | Direct3D through ANGLE exists only on Windows |
| PR #21 non-sparse Vulkan arenas | PARITY VERIFIED | PARITY VERIFIED | yes | yes | `osl-buffer-arena-tests` 392/0 on both |
| PR #24 audio backlog cap, pipeline warm-up | PARITY VERIFIED | PARITY VERIFIED | yes | yes | `osl-runtime-stability-tests` on both; shared `runtime_audio.c` |
| Run log, crash report | PARITY VERIFIED | PARITY VERIFIED | n/a | n/a | `osl-log-tests` on both |
| Freeze report `hang_NNN.txt` | yes | PLATFORM SPECIFIC BY DESIGN | n/a | n/a | uses Windows thread suspension and hung-window detection |

## Builds

The eight supported builds have one definition (`launcher/game_profile.cpp`, mirrored by `scripts/game_profiles.py`)
and pass the same build-parity rows on both platforms. USA 1.00, Japan 1.00 and 1.02 and Europe 1.00 and 1.01 are
verified on real files on both; USA 1.01, HK and Asia are DATA UNAVAILABLE on both (no local game files), with their
structure proved on synthetic data.

## Platform services

| Service | Windows | Linux |
| --- | --- | --- |
| HTTPS (Bug Report, updater) | WinHTTP | system `curl`, with a clear error when it is missing |
| Hashes and ciphers | CNG | OpenSSL libcrypto (bundled) |
| Update install | ZIP and `trg-updater.exe` | the original AppImage file replaced and started again |
| File chooser | Windows dialog | zenity or kdialog, otherwise the built-in chooser |
| Show the launcher again | Shift at start | Shift at start (X11) or `--launcher` |
| Guest memory and faults | `VirtualAlloc2`, vectored exception handler | `memfd`, signals |
| Release build | ThinLTO, stripped | stripped, symbols split off |

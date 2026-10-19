# First boot

The project builds and the installed USA 1.00 and Japanese 1.02 games are recognized and prepared.
These checks run without a window and without executing game code:

```powershell
.\tools\build.ps1
.\tools\test.ps1
build\cmake\clang64\bin\osl-loader.exe --check-game game\prepared\CUSA05083-1.00 build\image\CUSA05083-1.00
build\cmake\clang64\bin\osl-loader.exe build\image\CUSA05083-1.00\boot.bin --app0 game\prepared\CUSA05083-1.00 --cpu-only --check-imports
build\cmake\clang64\bin\osl-loader.exe --check-game game\prepared\CUSA01290-1.02 build\image\CUSA01290-1.02
build\cmake\clang64\bin\osl-loader.exe build\image\CUSA01290-1.02\boot.bin --app0 game\prepared\CUSA01290-1.02 --cpu-only --check-imports
```

`--check-imports` prints `Imports: 0 missing` for each supported build: every PS4 import of the
eboot and its bundled modules is either provided by the game's own modules or implemented by the
runtime. `--check-game` runs the same readiness check the launcher's Play page uses and prints the
active build label.

## The first manual run

From the repository, after `tools\build.ps1` has completed:

```powershell
.\tools\build.ps1 -Run
```

`-Run` sets the MSYS2 `PATH`, starts `osl-loader.exe`, and keeps the console attached. The launcher
opens; the Play page should already say **Ready to play** for the active build. With both releases
installed, the active build is the newest prepared one; the status names its region and version, for
example **Ready to play · CUSA05083 1.00 USA** or **CUSA01290 1.02 Japan**. Press **PLAY**. The
runtime, graphics, audio, controller and UI behavior is tested manually only.

The first manual pass should also check the new parity features: Display's Aspect Ratio and Window
size choices, Graphics' Render resolution (try 720p, then 1440p, then 4K), the Gameplay readouts,
the Trophies page, and that the in-game menu (F10 / R3+L2) still opens and saves its settings. Keep
Render resolution at Native while checking the rest first.

To return the diagnostics, attach the newest log:

```
build\logs\odin_sphere_leifthrasir_NNN.log
```

Detailed logging is on by default. If it is turned off in About → Diagnostics, a normal exit removes that
run's log; crashes and abnormal exits still keep it. The About page's **Open logs folder** button opens the
active log folder (or creates the default logs folder when no log exists yet).

(The release layout uses `logs\odin_sphere_leifthrasir_NNN.log` next to `osl-loader.exe`.)

## Reading the log

The boot path reports these stages in order:

| Line | Meaning |
| --- | --- |
| `OSL: game build CUSA05083 1.00 USA` or `CUSA01290 1.02 Japan` | the boot image matches the supported executable |
| `GPU: window and Vulkan presenter ready; ... HLE symbols` | the video core created its window, device and presenter |
| `Mapped N bytes, ... applied N relocations` | the guest image was mapped and imports were written |
| `Starting linked module ...` / `Module ... initializer returned 0` | `libc.prx` and `libSceFios2.prx` initialized |
| `Entering original x86-64 code at guest offset ...` | the first guest instruction is about to run |
| `STOP: first unsupported PS4 import: ...` | the guest reached an import the runtime does not implement; the caller return offset is printed |
| `Fault: ... at guest offset ...` | the guest faulted; registers, the stack top and the frame chain follow |

If the process stops at an import or a fault, the log identifies the first broken stage. Do not
patch a later failure before that stage is understood.

# Launcher: graphics settings

The launcher writes `user\osl.ini`, and the video core reads the same file when the game starts. This page covers the
settings that choose and shape the renderer; the rest of the launcher is described in the README.

## Renderer

The **Renderer** row is on the Display page, directly above Graphics card.

| Row | `renderer` |
|---|---|
| Auto | `auto` |
| Vulkan | `vulkan` (default) |
| OpenGL (experimental) | `opengl` |
| Direct3D 11 (experimental), Windows only | `d3d11` |

Vulkan is the supported renderer. OpenGL and Direct3D 11 are experimental and render only part of the game's features;
Direct3D 11 is intended mainly for Windows systems where Vulkan or OpenGL has problems. Auto uses Vulkan, and OpenGL when this
system cannot run Vulkan. A renderer chosen by name never falls back to another one. The choice takes effect the next time the
game starts. Linux has no Direct3D 11 row, and an unknown or unavailable value reads as Vulkan. **Reset all settings** puts
Vulkan back. The launcher and the video core share one parser (`gpu/shim/osl_renderer.h`).

On Linux the launcher's Play check asks for Vulkan only when the saved renderer needs it: OpenGL does not, and Auto can do
without it.

## Frame rate

The **Frame rate** row is on the Gameplay page, directly above Frame rate in title. It is not a renderer setting: the same
row, key and behaviour apply to Vulkan, Auto (whichever renderer it picked), OpenGL and Direct3D 11, to every supported build
and to every text language.

| Row | `frame_rate` |
|---|---|
| 30 FPS | `30` |
| 60 FPS (Original) | `60` (default) |
| 90 FPS | `90` |
| 120 FPS | `120` |
| 144 FPS | `144` |
| 165 FPS | `165` |
| 240 FPS | `240` |
| Unlimited | `0` |

Any other value reads as 60, in the launcher and in the video core alike (`gpu/shim/osl_frame_pacing.h`). The choice applies
the next time the game starts. **Reset all settings** puts 60 back, and the other lines of `osl.ini` are kept.

It is presentation timing only. The game still builds 60 unique frames per second and its vblank, simulation, animation,
physics and input keep their original rate; the setting never changes game speed. At 30 the VideoOut present thread takes
every flip and shows every second one. Above 60 it shows the latest completed frame again in the slots between two flips
(120 FPS: one repeat per game frame), scheduled on absolute times so the rate does not drift when it is not a multiple of 60.
Unlimited is bounded at 480 presentations a second. There is no interpolation. With VSync On the rate cannot pass the
monitor's refresh rate (120 FPS on a 60 Hz monitor is presented at 60; 240 FPS on a 144 Hz monitor at 144); with VSync Off
the requested rate is used. The log has one line at the start of the game, for example
`VideoOut: vblank 60 Hz, frame rate 120, repeat cadence 120 FPS, VSync off`.

**Frame rate in title** and the frame counter measure presentations, repeats included, so at 120 they approach 120.

This implements the Frame rate part of issue #13; HD texture packs and DLSS are separate requests.

## Settings that depend on the renderer

| Setting | Vulkan | OpenGL | Direct3D 11 |
|---|---|---|---|
| Graphics card | yes | the system's default adapter | the default adapter |
| VSync, Presentation | yes | yes | yes |
| Upscaling (FSR 1), Sharpening (RCAS), Anti-aliasing (SMAA) | yes | no | no |
| Texture filtering | yes | yes, where the driver offers it | yes, where ANGLE offers it |
| Render resolution, Aspect Ratio, window size, monitor, display mode | yes | yes | yes |
| Frame rate (30 to 240, Unlimited) | yes | yes | yes |

Details and limitations: [GRAPHICS.md](GRAPHICS.md), [opengl-renderer.md](opengl-renderer.md),
[d3d11-renderer.md](d3d11-renderer.md).

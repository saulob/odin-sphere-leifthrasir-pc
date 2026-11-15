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

## Settings that depend on the renderer

| Setting | Vulkan | OpenGL | Direct3D 11 |
|---|---|---|---|
| Graphics card | yes | the system's default adapter | the default adapter |
| VSync, Presentation | yes | yes | yes |
| Upscaling (FSR 1), Sharpening (RCAS), Anti-aliasing (SMAA) | yes | no | no |
| Texture filtering | yes | yes, where the driver offers it | yes, where ANGLE offers it |
| Render resolution, Aspect Ratio, window size, monitor, display mode | yes | yes | yes |

Details and limitations: [GRAPHICS.md](GRAPHICS.md), [opengl-renderer.md](opengl-renderer.md),
[d3d11-renderer.md](d3d11-renderer.md).

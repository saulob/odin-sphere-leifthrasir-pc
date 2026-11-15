# Graphics

## Renderers

Vulkan is the supported renderer and the default. OpenGL and, on Windows, Direct3D 11 are **experimental**: they render
only part of what the Vulkan renderer does and are not feature complete. The launcher's **Renderer** row on the Display page, above Graphics card,
(`renderer` in `osl.ini`) chooses one, and the choice applies the next time the game starts: a running game cannot change its
renderer.

| Setting | Key | Runs the game on |
|---|---|---|
| Vulkan (default) | `vulkan` | The Vulkan renderer |
| Auto | `auto` | Vulkan, or OpenGL when this system cannot run Vulkan |
| OpenGL (experimental) | `opengl` | Desktop OpenGL 3.3 core |
| Direct3D 11 (experimental), Windows only | `d3d11` | OpenGL ES 3.0 on ANGLE's Direct3D 11 backend |

An unknown value is Vulkan. On Linux `d3d11` is not a choice and also reads as Vulkan. **Reset all settings** restores Vulkan.

Only Auto ever falls back, and only from Vulkan to OpenGL. A renderer chosen by name starts, or fails with its own message;
it is never replaced by another one. Auto never picks Direct3D 11.

```
Liverpool -> renderer_backend.h -> Vulkan::Presenter / Rasterizer
                                -> OpenGL::Presenter / Rasterizer -> Desktop OpenGL 3.3
                                                                  -> OpenGL ES 3.0 -> ANGLE -> Direct3D 11
```

There is no separate Direct3D 11 renderer: it is the OpenGL backend running as OpenGL ES 3.0 on ANGLE
([d3d11-renderer.md](d3d11-renderer.md)).

### Selection

`oslgpu_init` (`gpu/shim/oslgpu.cpp`) reads `osl.ini` once and resolves the setting with `gpu/shim/osl_renderer.h`, the one
place that knows the keys and how to parse them: the launcher's Renderer row, the settings file and the video core all use
it. Each renderer needs its own kind of SDL window, so the choice is made before the window exists. Auto asks
`osl_check_vulkan` (`gpu/shim/osl_gpu_list.cpp`) whether Vulkan can run: the loader, the API version and a device that meets
the renderer's requirements. The window is then created once for the chosen backend (`GraphicsApi::Vulkan`, `OpenGL` or
`Angle` in `gpu/shim/sdl_window.h`), and the factory in `video_core/renderer_factory.cpp` creates the presenter behind
`video_core/renderer_backend.h`, the only boundary Liverpool, VideoOut and the guest memory hooks see.

The session log opens with the result:

```
Renderer: selected auto, active Vulkan
Renderer: selected auto, active OpenGL
Renderer: Auto fell back to OpenGL: <why Vulkan cannot run>
Renderer: selected opengl, active OpenGL
Renderer: selected d3d11, active Direct3D 11 via ANGLE
```

A Vulkan device that passes the check and still fails to start is not caught by Auto. If the OpenGL that Auto fell back to
cannot start either, its message carries Vulkan's reason too.

### Graphics card

The Graphics card row selects among the Vulkan devices and applies to Vulkan only. OpenGL uses the system's own choice of
adapter, and Direct3D 11 through ANGLE uses the default adapter; neither has a way to select another one here.

### What each renderer supports

| Feature | Vulkan | OpenGL | Direct3D 11 (ANGLE) | Where it lives |
|---|---|---|---|---|
| Render resolution, Aspect Ratio | yes | yes | yes | Shared: they rewrite the game's own render target sizes (`launcher/resolution.c`) |
| Window modes, F11, window size, monitor, DPI | yes | yes | yes | Shared: `gpu/shim/window.cpp` |
| Frame rate and resolution in the title, frame counter | yes | yes | yes | Shared: `window.cpp`, `osl_overlay.cpp` |
| Presentation (letterbox, stretch) | yes | yes | yes | Each presenter |
| VSync | yes | yes | yes | Each presenter |
| F10 Cheats menu, save list, text entry, startup notices, controller navigation | yes | yes | yes | Shared overlay logic; Vulkan, OpenGL and GLES 3 each submit it |
| Texture filtering (anisotropic) | yes | yes, where the driver offers it | yes, where ANGLE offers it | Each rasterizer |
| FSR 1 upscaling, RCAS sharpening, SMAA | yes | **no** | **no** | Vulkan only (`host_passes`) |
| Graphics card selection | yes | no | no | Vulkan only |
| Draw preparation workers, draw pipeline, pipeline cache and its warm-up | yes | no | no | Vulkan only |
| Reduced scene targets, HDR | yes | no | no | Vulkan only |
| Frame capture, GPU read back of images to guest memory | yes | no | no | Vulkan only |
| Compute, indirect draws, MSAA, geometry and tessellation stages | yes | no | no | Not implemented in OpenGL; see below |

## Presentation

On Vulkan, every frame goes through SMAA 1x (optional), FSR 1 when the window is larger than the picture and RCAS
sharpening (`host_passes`), then the post-process pass into the presentation frame, which the present thread blits into the
swapchain before it draws the overlay over it. Presentation Letterbox keeps the picture's aspect ratio with bars; Stretch
fills the window. FSR 1 does nothing when the window is the picture's size or smaller; RCAS alone runs only when the window
and the picture are the same size.

On OpenGL and Direct3D 11 the game's display buffer is converted to the output's colour encoding and scaled to the window with a
linear filter, with the same Letterbox and Stretch behaviour. FSR 1, RCAS and SMAA are not available there, and the launcher
says so next to each of them.

## OpenGL renderer (experimental)

How the backend came to this project, and its licensing, is in [opengl-renderer.md](opengl-renderer.md).

`video_core/renderer_opengl` targets OpenGL 3.3 core:

| Part | How |
|---|---|
| Thread | One context, current on the GPU command thread. EGL allows a surface on one thread only, so the present thread posts its work there (`Presenter::Present`) and a VSync wait holds up that thread |
| Shaders | The same recompiler, then `shader_recompiler/backend/glsl`: GLSL 3.30 from the IR. It refuses what 3.30 cannot express, with the reason in the log, and the draw is skipped |
| Guest buffers | No storage buffers in 3.3: one streaming ring, read by the shaders as a `usamplerBuffer` of dwords and by vertex and index fetch directly. With `ARB_buffer_storage` it stays mapped and fences guard its segments; otherwise each draw maps what it wrote and a wrap orphans the storage |
| Images | `ImageInfo` from the T#, CB and DB as in Vulkan; `vk::Format` is the shared name, mapped in `gl_format.cpp`. Unmapped formats are logged and draw black. Cube maps are bound as 2D arrays |
| Detiling | On the CPU (`gl_detile.cpp`, a port of `tiling.comp`, including the micro tiled mips of macro tiled images): 3.3 has no compute |
| Coherency | `GuestWriteWatch` write protects an image's pages, as Vulkan's `PageManager` does but without its rasterizer: only a page written since the last check gets the image hashed, and only a changed hash uploads it. A written page stays unprotected for 16 frames, doubling up to 1024 for a page that keeps being written. Fills and copies the GPU thread makes itself tell the watch first, so they do not fault |
| State | Bindings and fixed-function state go through `GlState`, which drops calls that change nothing. Anything that sets OpenGL state behind it must be followed by `GlState::Invalidate`, as the ImGui overlay is |
| Rect and quad lists | A generated geometry shader completes them |
| Coordinates | Render targets keep Vulkan's window coordinates: row 0 of an image is OpenGL's y = 0, so only the final picture is flipped. A negative viewport height becomes a flip in the vertex stage, and clip Z [0, w] is converted to [-w, w] |

The game's compute work does not run on the GPU. A dispatch that clears metadata becomes a clear. A program whose only
effect, proven from its translated IR for that dispatch, is one constant dword per invocation at its global index (a
buffer or render target fill) is carried out on the CPU, and the images over that memory are uploaded again. Anything
else is logged once and skipped.

Not implemented: render target array slices and texture views of later slices, sRGB reinterpretation of an image (logged),
one image bound twice in a draw with different swizzles or mip ranges, MSAA (targets render single sampled), storage images
and buffer writes from shaders, depth bounds, indirect draws and dispatches, depth-stencil copy passes, geometry and
tessellation stages, mip level queries, texture gather, sample shading, scaled MIN/MAX blending that needs a second pass,
reads of render targets through guest memory, and per-target blending below OpenGL 4.0. SMAA, FSR 1 and RCAS are Vulkan only.
The overlay uses the ImGui OpenGL 3 backend.

Each `OpenGL:` log line names what was refused, once. `Renderer: selected <setting>, active <backend>` opens the session.

## Direct3D 11 renderer (experimental)

On Windows, `d3d11` runs the OpenGL backend as OpenGL ES 3.0 on ANGLE's Direct3D 11 backend. Vulkan and desktop OpenGL do not
load ANGLE. What differs, and what it does not do, is in [d3d11-renderer.md](d3d11-renderer.md).

## Render resolution and Aspect Ratio

`launcher/resolution.c` rewrites the build's render target sizes in the game's code after checking each site still holds its
original instruction; the game does not start if one does not. They are properties of the game, not of a renderer, so every
renderer draws what the game renders and presents it the same way.

## Texture filtering

Texture filtering 2x to 16x replaces the filter of the game's mipmapped bilinear samplers in graphics draws with
anisotropic filtering, unless the game already asks for at least as much. The limit is what the device offers
(`maxSamplerAnisotropy` on Vulkan, `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT` on OpenGL and Direct3D 11).

## Linux

Linux has Auto, Vulkan and OpenGL. Direct3D 11 and ANGLE are Windows only: nothing about ANGLE is built or packaged for
Linux, and the desktop OpenGL path compiles without it. Linux keeps its glibc baseline.

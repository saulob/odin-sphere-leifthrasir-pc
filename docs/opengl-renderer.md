# OpenGL renderer: origin and licensing

The OpenGL backend is experimental. It exists so that systems where Vulkan is missing or unreliable can still run the game,
and so that the video core is not tied to one graphics API. Vulkan remains the supported renderer and the default; OpenGL is
chosen with the **Renderer** setting in the launcher, and the choice applies at the next start. How the backend works is in
[GRAPHICS.md](GRAPHICS.md#opengl-renderer-experimental).

This backend, its GLSL shader backend, its ANGLE glue and the renderer boundary were ported from the Dragon's Crown Pro PC
project, which has the same shadPS4-derived GPU architecture. They were adapted to this project's own Vulkan renderer, which
is unchanged by them, rather than written again. The Vulkan renderer (`gpu/shadps4/video_core/renderer_vulkan`) and shadPS4
define PS4 GPU behavior: GNM and GCN shader semantics, textures, render targets, buffers, synchronization, presentation and
resource handling.

## Sources

| Source | Used for |
|---|---|
| Dragon's Crown Pro PC, OpenGL and Direct3D 11 renderers | The source of `renderer_opengl`, `shader_recompiler/backend/glsl`, `renderer_backend.h`, `renderer_factory.cpp`, the ANGLE glue and the ES 3 ImGui shim, adapted here |
| This project's Vulkan renderer | The semantic reference. The OpenGL classes follow its roles |
| [shadPS4](https://github.com/shadps4-emu/shadPS4), commit `b4e7ae7` (GPL-2.0-or-later) | The PS4 runtime and GPU front end this project is built on: Liverpool, the shader recompiler, VideoOut, tiling |
| Historical Yuzu OpenGL renderer, tag `mainline-0-1734`, commit `537296095ab24eddcb196b5ef98004f91de9c8c2`, as referenced by the [Flathub packaging](https://github.com/flathub/org.yuzu_emu.yuzu) | Studied for generic, mature OpenGL backend design and performance ideas by the project this backend comes from. No Yuzu source is included, and no GPU semantics come from it: PS4 behavior comes from shadPS4 and the Vulkan renderer only |

## Files

The `gl_*` files are in `gpu/shadps4/video_core/renderer_opengl`.

| Files | Origin |
|---|---|
| `gl_rasterizer.*`, `gl_shader_cache.*`, `gl_presenter.*` | Adapted from shadPS4's `vk_rasterizer`, `vk_pipeline_cache` and `vk_presenter`, with the Vulkan calls replaced; they keep the shadPS4 copyright notice |
| `gl_detile.*` | Port of shadPS4's `tiling.comp` to the CPU, because OpenGL 3.3 has no compute; keeps the shadPS4 notice |
| `shader_recompiler/backend/glsl/emit_glsl.*` | A GLSL backend over the recompiler's IR with the targets `Desktop330` and `Gles300`, structured after shadPS4's SPIR-V backend; keeps the shadPS4 notice |
| `gl_state.h` (`GlState`), `gl_stream_ring.*`, `gl_guest_watch.*` (`GuestWriteWatch`), `gl_texture_cache.*`, `gl_format.*`, `gl_api.*`, `gl_angle.*`, `gl_capabilities.h`, `gl_primitives.h` | Original to the Dragon's Crown Pro PC project: state caching, a streaming buffer, write-protecting guest pages to detect changes and a format table are standard emulator and OpenGL practice; `GuestWriteWatch` plays the role shadPS4's `PageManager` plays for Vulkan |
| `renderer_backend.h`, `renderer_factory.cpp`, `gpu/shim/osl_renderer.h` | The boundary between the GPU front end and a renderer, and the Renderer setting |
| `gpu/shim/imgui_impl_gles3.*`, `gpu/shim/gles3_shim` | The ES 3 build of ImGui's OpenGL 3 backend, for Direct3D 11 through ANGLE |

## Adaptations to this project

- The renderer boundary also carries this project's additional Vulkan hooks (`NoteQueuedMemoryWrite`,
  `RequestHostReadBarrier`); the OpenGL rasterizer does nothing for them.
- `gl_detile` follows this project's tiler: a mip too small for a macro tile is micro tiled.
- Cube maps are bound as 2D arrays, because this project's `GetViewType` keeps a cube a cube.
- The GLSL backend emits this project's IR: the split cube face operations, and no `IAddCarry32`, which this IR does not have.
- The shader cache sets `dx10_clamp` and `depth_before_shader`, which this project's recompiler reads.
- The game specific compute shader replaced by a CPU fill in the original is not carried over.

## Licensing

The project is GPL-2.0-or-later. Every OpenGL file carries `SPDX-License-Identifier: GPL-2.0-or-later`, and the files adapted
from shadPS4 also carry its `SPDX-FileCopyrightText`. Code that was only studied conceptually carries no attribution because
none of it was copied. ANGLE and what it contains are covered in [d3d11-renderer.md](d3d11-renderer.md).

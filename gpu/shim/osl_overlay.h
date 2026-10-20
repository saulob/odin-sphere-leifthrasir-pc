// SPDX-License-Identifier: GPL-2.0-or-later
// The runtime UI (Dear ImGui), drawn by the presenter into the swapchain image after the game frame: the loading
// screen, the frame counter, the game's text dialog and its save list.

#pragma once

#include <string>
#include <vector>
#include "common/types.h"
#include "osl_png.h"
#include "video_core/renderer_vulkan/vk_common.h"

union SDL_Event;

namespace Vulkan {
class Instance;
}

namespace OslOverlay {

/// Present thread, once: the ImGui context and its Vulkan backend.
void Init(const Vulkan::Instance& instance, vk::Format format, u32 image_count);

/// Window thread, for every SDL event: true when the save list consumed it.
bool HandleEvent(const SDL_Event& event);

/// Start-up loading screen (pipeline cache warm-up): `total` 0 shows no count.
void SetLoading(bool active, u32 done, u32 total);
/// Background cache warm-up: a small corner note while the game runs; `total` 0 hides it.
void SetBackgroundLoading(u32 done, u32 total);

/// Whether anything is drawn this frame (a dialog, the loading screen or the frame counter).
bool Visible();

/// Counts a presented game frame (the frame rate in the window title).
void LogPresent();
u64 PresentedFrames();

/// Present thread: draws into `view` (layout ColorAttachmentOptimal).
void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent);

/// The window's display content scale (DPI): the runtime UI grows with it.
void SetDisplayScale(float scale);

/// A game dialog is open: the game's input is held neutral.
bool CapturesInput();

/// The runtime's buttons (PS4 bits) that move and confirm the save list, polled every frame.
void SetChoiceInput(uint32_t (*buttons)());
/// Reads the save list's buttons now (also done every presented frame).
void PollChoiceInput();

/// Window thread: the game's text dialog (sceImeDialog). Shown as a box on screen until
/// `active` is false; typing goes to the window (sdl_window), the box only displays it.
void SetTextEntry(bool active, const std::string& prompt, const std::string& text);

/// The game's save list: shown until the player chooses (arrows/D-pad, Enter/Cross) or
/// cancels (Esc/Circle). `pictures`: one per item (empty = none), drawn beside its text.
/// PollChoice: -1 while open, -2 cancelled, else the chosen index.
void BeginChoice(const std::string& title, const std::vector<std::string>& items,
                 std::vector<OslPng::Image> pictures, int focus);
int PollChoice();

} // namespace OslOverlay

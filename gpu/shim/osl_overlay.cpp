// SPDX-License-Identifier: GPL-2.0-or-later
#include "osl_overlay.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <vector>

#include <SDL3/SDL.h>
#include "osl_accent.h"
#include "osl_cheat_menu.h"
#include "osl_notice.h"
#include "osl_settings.h"
#include "runtime_cheats.h"
#include "imgui.h"
#include "imgui_internal.h" // RegisterUserTexture (save pictures)
#include "imgui_impl_vulkan.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

// DejaVu Sans (Cyrillic), embedded (third_party/fonts, Bitstream Vera license).
#ifdef _WIN32
asm(".section .rdata,\"dr\"\n"
    ".balign 16\n"
    ".global osl_font_ttf\n"
    "osl_font_ttf:\n"
    ".incbin \"" OSL_FONT_PATH "\"\n"
    ".global osl_font_ttf_end\n"
    "osl_font_ttf_end:\n"
    ".text\n");
#else
asm(".section .rodata\n"
    ".balign 16\n"
    ".hidden osl_font_ttf\n"
    ".global osl_font_ttf\n"
    "osl_font_ttf:\n"
    ".incbin \"" OSL_FONT_PATH "\"\n"
    ".hidden osl_font_ttf_end\n"
    ".global osl_font_ttf_end\n"
    "osl_font_ttf_end:\n"
    ".previous\n");
#endif
extern "C" const unsigned char osl_font_ttf[];
extern "C" const unsigned char osl_font_ttf_end[];

namespace OslOverlay {

namespace {

ImVec4 AccentColor() {
    return ImVec4(osl::kAccentRed / 255.0f, osl::kAccentGreen / 255.0f, osl::kAccentBlue / 255.0f, 1.0f);
}

ImVec4 AccentHotColor() {
    return ImVec4(osl::kAccentHotRed / 255.0f, osl::kAccentHotGreen / 255.0f, osl::kAccentHotBlue / 255.0f, 1.0f);
}

ImVec4 AccentPressedColor() {
    return ImVec4(osl::kAccentPressedRed / 255.0f, osl::kAccentPressedGreen / 255.0f,
                  osl::kAccentPressedBlue / 255.0f, 1.0f);
}

ImVec4 AccentSubtleColor() {
    return ImVec4(osl::kAccentSubtleRed / 255.0f, osl::kAccentSubtleGreen / 255.0f,
                  osl::kAccentSubtleBlue / 255.0f, 1.0f);
}

std::mutex imgui_mutex; // the ImGui context: window thread (input) and present thread
bool initialized = false;
std::atomic<float> display_scale{1.0f};
std::atomic<bool> loading{false};
std::atomic<u32> loading_done{0}, loading_total{0};
std::atomic<u32> background_done{0}, background_total{0};
// The game's text dialog (SetTextEntry), guarded by imgui_mutex.
bool text_entry_active = false;
std::string text_entry_prompt, text_entry_text;
// The game's save list (sceSaveDataDialog): a choice made with the arrows/D-pad.
std::atomic<bool> choice_active{false};
std::string choice_title;
std::vector<std::string> choice_items;
int choice_index = 0;
std::atomic<int> choice_result{-1}; // -1 open, -2 cancelled, else the chosen index
// Save pictures: decoded by BeginChoice, made textures on the present thread (null = none).
// Closed lists' textures wait in choice_retired until the backend has destroyed them.
std::vector<OslPng::Image> choice_pictures;
std::vector<ImTextureData*> choice_textures, choice_retired;
std::atomic<bool> choice_retiring{false};
float base_scale = 1.0f;

// Present rate for the FPS counter.
std::chrono::steady_clock::time_point last_present{};
float frame_ms_avg = 0.0f;

// Startup Gameplay hints ("Skip Intro: On"), queued once per launch by QueueStartupNotice and
// drawn in the order they were queued, stacked below the frame counter.
constexpr double kStartupNoticeSeconds = 5.0;
std::mutex notice_mutex;
std::vector<OslNotice::StartupNotice> startup_notices;

double NowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The frame counter's text.
std::string OverlayText() {
    char text[64];
    std::snprintf(text, sizeof(text), "%.0f FPS  %.1f ms", frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f,
                  frame_ms_avg);
    return text;
}

struct FpsBox {
    ImVec2 p0, p1, pad;
    float size;
};

FpsBox FpsCounterBox(const char* text) {
    const auto& s = OslSettings::Get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float size = ImGui::GetFontSize() * s.overlay_scale / 100.0f;
    const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const ImVec2 pad(size * 0.6f, size * 0.3f);
    const ImVec2 box(text_size.x + pad.x * 2, text_size.y + pad.y * 2);
    const int corner = s.overlay_corner;
    const bool right = corner == 1 || corner == 3, bottom = corner >= 2;
    const ImVec2 p0(right ? viewport->WorkPos.x + viewport->WorkSize.x - size - box.x : viewport->WorkPos.x + size,
                    bottom ? viewport->WorkPos.y + viewport->WorkSize.y - size - box.y : viewport->WorkPos.y + size);
    return {p0, ImVec2(p0.x + box.x, p0.y + box.y), pad, size};
}

bool FpsCounterShown() {
    return OslNotice::FrameCounterVisible(OslSettings::Get().show_fps, frame_ms_avg);
}

// Upper-right box for a startup notice, `top` below the viewport's work area; same look as the
// frame counter's panel but independent of its corner (notices always stay upper-right).
FpsBox NoticeBox(const char* text, float top) {
    const auto& s = OslSettings::Get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float size = ImGui::GetFontSize() * s.overlay_scale / 100.0f;
    const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const ImVec2 pad(size * 0.6f, size * 0.3f);
    const ImVec2 box(text_size.x + pad.x * 2, text_size.y + pad.y * 2);
    const ImVec2 p0(viewport->WorkPos.x + viewport->WorkSize.x - size - box.x, viewport->WorkPos.y + top);
    return {p0, ImVec2(p0.x + box.x, p0.y + box.y), pad, size};
}

// Where the first startup notice begins: below the frame counter when it is shown in its default
// top-right corner, otherwise the overlay's normal top margin.
float StartupNoticeTop() {
    const auto& s = OslSettings::Get();
    const float size = ImGui::GetFontSize() * s.overlay_scale / 100.0f;
    if (s.overlay_corner == 1 && FpsCounterShown()) {
        const FpsBox fps_box = FpsCounterBox(OverlayText().c_str());
        return (fps_box.p1.y - ImGui::GetMainViewport()->WorkPos.y) + size * 0.5f;
    }
    return size;
}

void StartupNotices() {
    std::scoped_lock lock{notice_mutex};
    if (startup_notices.empty()) {
        return;
    }
    constexpr ImU32 panel_color = IM_COL32(7, 10, 22, 190);
    constexpr ImU32 border_color = IM_COL32(osl::kAccentRed, osl::kAccentGreen, osl::kAccentBlue, 255);
    constexpr ImU32 text_color = IM_COL32(255, 255, 255, 255);
    const bool frame_counter_enabled = OslSettings::Get().show_fps;
    const bool ready = OslNotice::StartupNoticeReady(frame_counter_enabled, FpsCounterShown());
    const double now = NowSeconds();
    const float gap = ImGui::GetFontSize() * OslSettings::Get().overlay_scale / 100.0f * 0.4f;
    float top = StartupNoticeTop();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    for (OslNotice::StartupNotice& notice : startup_notices) {
        const FpsBox box = NoticeBox(notice.text().c_str(), top);
        if (notice.Visible(now, ready, kStartupNoticeSeconds)) {
            draw->AddRectFilled(box.p0, box.p1, panel_color, box.size * 0.5f);
            draw->AddRect(box.p0, box.p1, border_color, box.size * 0.5f, 0, 1.0f);
            draw->AddText(ImGui::GetFont(), box.size, ImVec2(box.p0.x + box.pad.x, box.p0.y + box.pad.y),
                         text_color, notice.text().c_str());
        }
        top += (box.p1.y - box.p0.y) + gap;
    }
}

bool AnyStartupNoticePending() {
    std::scoped_lock lock{notice_mutex};
    const double now = NowSeconds();
    for (const OslNotice::StartupNotice& notice : startup_notices) {
        if (notice.Pending(now, kStartupNoticeSeconds)) {
            return true;
        }
    }
    return false;
}

// The game's text dialog: what is typed, and how to finish (keyboard or controller).
void TextEntryBox() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.42f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(viewport->WorkSize.x * 0.32f, 0.0f),
                                        ImVec2(viewport->WorkSize.x * 0.8f, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##text_entry", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextColored(AccentHotColor(), "%s", text_entry_prompt.c_str());
    ImGui::Separator();
    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("%s_", text_entry_text.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::TextDisabled("Keyboard: type, Backspace to erase, Enter = OK, Esc = cancel");
    ImGui::TextDisabled("Controller: Cross (A) = OK, Circle (B) = cancel");
    ImGui::End();
}

void ChoiceBox() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(viewport->WorkSize.x * 0.36f, 0.0f),
                                        ImVec2(viewport->WorkSize.x * 0.8f, viewport->WorkSize.y * 0.8f));
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("##choice", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextColored(AccentHotColor(), "%s", choice_title.c_str());
    ImGui::Separator();
    // Save pictures are 228x128; rows without one keep the text aligned when others have one.
    const bool pictures = std::any_of(choice_textures.begin(), choice_textures.end(),
                                      [](ImTextureData* tex) { return tex != nullptr; });
    const ImVec2 picture_size(228.0f * base_scale, 128.0f * base_scale);
    for (int i = 0; i < int(choice_items.size()); ++i) {
        const bool selected = i == choice_index;
        ImGui::BeginGroup();
        ImTextureData* tex = i < int(choice_textures.size()) ? choice_textures[i] : nullptr;
        if (tex) {
            ImGui::Image(tex->GetTexRef(), picture_size);
        } else if (pictures) {
            ImGui::Dummy(picture_size);
        }
        if (pictures) {
            ImGui::SameLine();
        }
        if (selected) {
            ImGui::TextColored(AccentHotColor(), "> %s", choice_items[i].c_str());
        } else {
            ImGui::Text("  %s", choice_items[i].c_str());
        }
        ImGui::EndGroup();
        if (selected) {
            if (pictures) {
                const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                const float pad = 3.0f * base_scale;
                ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - pad, min.y - pad),
                                                    ImVec2(max.x + pad, max.y + pad),
                                                    IM_COL32(osl::kAccentRed, osl::kAccentGreen, osl::kAccentBlue, 255),
                                                    0.0f, 0, 2.0f * base_scale);
            }
            ImGui::SetScrollHereY();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Up/Down or D-pad: choose   Enter or Cross (A): OK   Esc or Circle (B): cancel");
    ImGui::End();
}

void LoadingScreen() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.4f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##loading", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    const u32 done = loading_done, total = loading_total;
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(total ? "Loading shader cache..." : "Preparing graphics...");
    ImGui::SetWindowFontScale(1.0f);
    if (total) {
        char count[64];
        std::snprintf(count, sizeof(count), "%u / %u (%u%%)", done, total,
                      u32(u64(done) * 100 / total));
        ImGui::ProgressBar(float(done) / float(total), ImVec2(-1.0f, 0.0f), count);
    }
    ImGui::TextDisabled("The first start after an update takes longer.");
    ImGui::End();
}

void FpsCounter() {
    if (frame_ms_avg <= 0.0f) {
        return;
    }
    const std::string label = OverlayText();
    constexpr ImU32 panel_color = IM_COL32(7, 10, 22, 190);
    constexpr ImU32 border_color = IM_COL32(osl::kAccentRed, osl::kAccentGreen, osl::kAccentBlue, 255);
    constexpr ImU32 text_color = IM_COL32(255, 255, 255, 255);
    const FpsBox box = FpsCounterBox(label.c_str());
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(box.p0, box.p1, panel_color, box.size * 0.5f);
    draw->AddRect(box.p0, box.p1, border_color, box.size * 0.5f, 0, 1.0f);
    draw->AddText(ImGui::GetFont(), box.size, ImVec2(box.p0.x + box.pad.x, box.p0.y + box.pad.y), text_color,
                  label.c_str());
}

void BackgroundLoading() {
    const u32 done = background_done, total = std::max(background_total.load(), 1u);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + pad,
                                   viewport->WorkPos.y + viewport->WorkSize.y - pad),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("##background_loading", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextDisabled("Loading shader cache %u%%", u32(u64(done) * 100 / total));
    ImGui::End();
}

} // namespace

void Init(const Vulkan::Instance& instance, vk::Format format, u32 image_count) {
    std::scoped_lock lock{imgui_mutex};
    if (initialized) {
        return;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // window positions are not kept
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "osl";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;
    const ImVec4 accent = AccentColor();
    const ImVec4 accent_hot = AccentHotColor();
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = accent_hot;
    style.Colors[ImGuiCol_PlotHistogram] = accent;
    style.Colors[ImGuiCol_PlotHistogramHovered] = accent_hot;
    style.Colors[ImGuiCol_ButtonHovered] = accent;
    style.Colors[ImGuiCol_ButtonActive] = AccentPressedColor();
    style.Colors[ImGuiCol_HeaderHovered] = accent;
    style.Colors[ImGuiCol_HeaderActive] = AccentPressedColor();
    style.Colors[ImGuiCol_TabHovered] = accent;
    style.Colors[ImGuiCol_TabSelected] = accent;
    style.Colors[ImGuiCol_TabSelectedOverline] = accent_hot;
    style.Colors[ImGuiCol_FrameBgActive] = AccentSubtleColor();
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    style.Colors[ImGuiCol_NavCursor] = accent;

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(osl_font_ttf),
                                   int(osl_font_ttf_end - osl_font_ttf), 18.0f, &font_config);

    const vk::Instance vk_instance = instance.GetInstance();
    ImGui_ImplVulkan_LoadFunctions(
        instance.ApiVersion(),
        [](const char* name, void* user) {
            return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(
                *static_cast<const vk::Instance*>(user), name);
        },
        const_cast<vk::Instance*>(&vk_instance));

    const VkFormat color_format = static_cast<VkFormat>(format);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = instance.ApiVersion();
    info.Instance = vk_instance;
    info.PhysicalDevice = instance.GetPhysicalDevice();
    info.Device = instance.GetDevice();
    info.QueueFamily = instance.GetGraphicsQueueFamilyIndex();
    info.Queue = instance.GetGraphicsQueue();
    // Font atlas plus the save list pictures (up to 33, and a closed list's until freed).
    info.DescriptorPoolSize = 128;
    info.MinImageCount = std::max(image_count, 2u);
    info.ImageCount = std::max(image_count, 2u);
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };
    if (!ImGui_ImplVulkan_Init(&info)) {
        std::printf("Runtime UI: ImGui Vulkan backend init failed\n");
        ImGui::DestroyContext();
        return;
    }
    initialized = true;
}

// imgui_mutex held. The backend frees a texture some frames after its last use.
void RetireChoiceTextures() {
    for (ImTextureData* tex : choice_textures) {
        if (tex) {
            tex->WantDestroyNextFrame = true;
            choice_retired.push_back(tex);
        }
    }
    choice_textures.clear();
    choice_pictures.clear();
    choice_retiring = !choice_retired.empty();
}

// imgui_mutex held, before NewFrame: new pictures become textures, destroyed ones are freed.
void UpdateChoiceTextures() {
    if (!choice_pictures.empty()) {
        choice_textures.assign(choice_pictures.size(), nullptr);
        for (size_t i = 0; i < choice_pictures.size(); ++i) {
            const OslPng::Image& picture = choice_pictures[i];
            if (picture.rgba.empty()) {
                continue;
            }
            ImTextureData* tex = IM_NEW(ImTextureData)();
            tex->Create(ImTextureFormat_RGBA32, picture.width, picture.height);
            std::memcpy(tex->GetPixels(), picture.rgba.data(), picture.rgba.size());
            ImGui::RegisterUserTexture(tex);
            choice_textures[i] = tex;
        }
        choice_pictures.clear();
    }
    std::erase_if(choice_retired, [](ImTextureData* tex) {
        if (tex->Status != ImTextureStatus_Destroyed) {
            return false;
        }
        ImGui::UnregisterUserTexture(tex);
        IM_DELETE(tex);
        return true;
    });
    choice_retiring = !choice_retired.empty();
}

// The save list takes the keys and buttons that move and confirm; the rest stays with the game
// (held neutral by CapturesInput).
// Moves or closes the list (lock held). finish: 1 accept, 2 cancel.
void ChoiceStep(int move, int finish) {
    if (!choice_active) {
        return;
    }
    const int count = int(choice_items.size());
    if (move && count) {
        choice_index = (choice_index + move + count) % count;
    }
    if (finish) {
        choice_result = finish == 1 && count ? choice_index : -2;
        choice_active = false;
        RetireChoiceTextures();
    }
}

// The runtime's buttons for the list (gamepad and keyboard as mapped, scripted input, the left
// stick as up/down), polled every frame: the game may stop reading the pad during the dialog and
// raw window events depend on focus. Holding up or down repeats.
uint32_t (*choice_input)() = nullptr;

void PollChoiceInput() {
    static std::mutex poll_mutex; // the present thread and the game thread both poll
    std::scoped_lock poll_lock{poll_mutex};
    static uint32_t previous = 0;
    static bool open = false;
    static std::chrono::steady_clock::time_point repeat_at{};
    if (!choice_input) {
        return;
    }
    if (!choice_active) {
        open = false;
        return;
    }
    const uint32_t buttons = choice_input();
    const auto now = std::chrono::steady_clock::now();
    if (!open) { // buttons held when the list opened (the press that opened it) wait for release
        open = true;
        previous = buttons;
        return;
    }
    constexpr uint32_t Up = 0x10, Down = 0x40, Circle = 0x2000, Cross = 0x4000;
    const uint32_t pressed = buttons & ~previous;
    previous = buttons;
    int move = (pressed & Up) ? -1 : (pressed & Down) ? 1 : 0;
    if (move) {
        repeat_at = now + std::chrono::milliseconds(400);
    } else if ((buttons & (Up | Down)) && now >= repeat_at) {
        move = (buttons & Up) ? -1 : 1;
        repeat_at = now + std::chrono::milliseconds(120);
    }
    const int finish = (pressed & Cross) ? 1 : (pressed & Circle) ? 2 : 0;
    if (move || finish) {
        std::scoped_lock lock{imgui_mutex};
        ChoiceStep(move, finish);
    }
}

bool HandleChoiceEvent(const SDL_Event& event) {
    int move = 0, finish = 0; // finish: 1 accept, 2 cancel
    if (event.type == SDL_EVENT_KEY_DOWN) {
        const SDL_Keycode key = event.key.key;
        move = key == SDLK_UP || key == SDLK_W ? -1 : key == SDLK_DOWN || key == SDLK_S ? 1 : 0;
        finish = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE ? 1
                 : key == SDLK_ESCAPE || key == SDLK_BACKSPACE                   ? 2
                                                                                 : 0;
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (choice_input) {
            return true; // PollChoiceInput reads the gamepad
        }
        const u8 button = event.gbutton.button;
        move = button == SDL_GAMEPAD_BUTTON_DPAD_UP ? -1 : button == SDL_GAMEPAD_BUTTON_DPAD_DOWN ? 1 : 0;
        finish = button == SDL_GAMEPAD_BUTTON_SOUTH ? 1 : button == SDL_GAMEPAD_BUTTON_EAST ? 2 : 0;
    } else {
        return event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP;
    }
    ChoiceStep(move, finish);
    return true;
}

void SetChoiceInput(uint32_t (*buttons)()) {
    choice_input = buttons;
}

// The cheat menu (F10): open, or closing and still holding the game's input until what it received is released.
// Guarded by imgui_mutex.
namespace {

std::atomic<bool> cheat_open{false};
osl::menu::CloseGate cheat_close;
osl::menu::PadNav cheat_pad;
osl::cheats::MenuList cheat_list;
std::set<int> cheat_keys_held; // SDL scancodes that went down while the menu was open
bool cheat_focused = false;

float PixelDensity(SDL_WindowID id) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    const float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
    return density > 0.0f ? density : 1.0f;
}

void ActivateCheatRow(size_t index) {
    const auto rows = osl::cheats::MenuRows();
    if (index >= rows.size()) {
        return;
    }
    if (rows[index].kind == osl::cheats::RowKind::Experience) {
        const int multiplier = runtime_experience_cycle();
        if (multiplier >= 0) {
            std::printf("%s\n", osl::cheats::ExperienceLogLine(multiplier).c_str());
        }
        return;
    }
    const int state = runtime_cheat_toggle(int(rows[index].cheat));
    if (state >= 0) {
        std::printf("%s\n", osl::cheats::ToggleLogLine(rows[index].label, state != 0).c_str());
    }
}

void StepCheatList(int move) {
    cheat_list.Step(move, int(osl::cheats::kMenuRowCount));
}

void OpenCheatMenu() {
    cheat_open = true;
    cheat_close = {};
    cheat_keys_held.clear();
    cheat_pad = {};
    cheat_focused = false;
    cheat_list.Open();
}

void CloseCheatMenu() {
    cheat_open = false;
    cheat_close = {};
    cheat_keys_held.clear();
    cheat_pad = {};
    cheat_focused = false;
    ImGui::GetIO().ClearInputKeys();
}

// The menu's key opens and closes it; it stays away from the game's own dialogs and the loading screen.
void PressCheatMenuKey(bool repeat) {
    const bool busy = text_entry_active || choice_active || loading;
    switch (osl::cheats::TogglePress(repeat, cheat_open, busy)) {
    case osl::menu::ToggleAction::Open: OpenCheatMenu(); break;
    case osl::menu::ToggleAction::Close: cheat_close.Request(); break;
    case osl::menu::ToggleAction::None: break;
    }
}

// The controller's steps, from the runtime's pad state polled every frame.
void PollCheatPad(uint32_t buttons) {
    if (!choice_input || choice_active) {
        return;
    }
    const osl::menu::NavStep step = cheat_pad.Poll(buttons, NowSeconds());
    if (cheat_close.requested()) {
        return;
    }
    if (step.move) {
        StepCheatList(step.move);
    }
    if (step.activate) {
        ActivateCheatRow(size_t(cheat_list.selected));
    }
    if (step.close) {
        cheat_close.Request();
    }
}

// True when the row was clicked.
bool DrawCheatRow(const osl::cheats::MenuRow& row, size_t index, bool selected, float scale, const ImVec4& accent,
                  const ImVec4& focus, const ImVec4& dim) {
    const float height = ImGui::GetFrameHeight() + 4.0f * scale;
    ImGui::PushID(int(index));
    const bool pressed = ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(0.0f, height));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (selected) {
        draw->AddRectFilled(min, max, ImGui::GetColorU32(focus), 3.0f * scale);
    }
    const float pad = ImGui::GetFontSize() * 0.4f;
    const float y = min.y + (height - ImGui::GetFontSize()) * 0.5f;
    const char* text;
    bool unavailable, lit;
    if (row.kind == osl::cheats::RowKind::Experience) {
        const bool available = runtime_experience_available() != 0;
        const int multiplier = runtime_experience_multiplier();
        text = osl::cheats::ExperienceText(available, multiplier);
        unavailable = !available;
        lit = osl::cheats::ExperienceLit(available, multiplier);
    } else {
        const osl::cheats::RowState state = osl::cheats::StateOf(runtime_cheat_available(int(row.cheat)) != 0,
                                                                runtime_cheat_enabled(int(row.cheat)) != 0);
        text = osl::cheats::StateText(state);
        unavailable = state == osl::cheats::RowState::Unavailable;
        lit = state == osl::cheats::RowState::On;
    }
    draw->AddText(ImVec2(min.x + pad, y), ImGui::GetColorU32(unavailable ? dim : ImGui::GetStyleColorVec4(ImGuiCol_Text)),
                  row.label);
    draw->AddText(ImVec2(max.x - pad - ImGui::CalcTextSize(text).x, y), ImGui::GetColorU32(lit ? accent : dim), text);
    ImGui::PopID();
    return pressed;
}

std::string CheatCloseHint() {
    const char* name = SDL_GetScancodeName(SDL_Scancode(OslSettings::Get().cheat_hotkey.load()));
    return osl::cheats::CloseHint(name && *name ? name : "F10");
}

void CheatMenu() {
    ImGuiIO& io = ImGui::GetIO();
    if (cheat_close.requested()) {
        if (cheat_close.Step(!cheat_keys_held.empty() || cheat_pad.Held() != 0, NowSeconds())) {
            CloseCheatMenu();
        }
        return;
    }
    const auto rows = osl::cheats::MenuRows();
    const int count = int(rows.size());
    cheat_list.Clamp(count);
    const ImVec4 accent = AccentColor();
    const ImVec4 dim(0.64f, 0.66f, 0.76f, 1.0f);
    const ImVec4 focus(accent.x, accent.y, accent.z, 0.42f);
    const float scale = osl::cheats::MenuScale(display_scale, io.DisplaySize.x, ImGui::GetFontSize());
    const float em = ImGui::GetFontSize() * scale;
    const float margin = em * 0.8f;
    float top = margin;
    if (FpsCounterShown() && OslSettings::Get().overlay_corner == 1) {
        top = FpsCounterBox(OverlayText().c_str()).p1.y + margin * 0.5f;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = style.ItemSpacing.y * scale;
    const float separator = std::max(style.SeparatorSize, 1.0f);
    const float row_height = em + 2.0f * style.FramePadding.y * scale + 4.0f * scale;
    const float body_height = osl::cheats::MenuBodyHeight(rows.size(), row_height, spacing);
    const float header_height = em + spacing + separator + spacing;
    const float footer_height = spacing + separator + spacing + em + spacing + em;
    const float window_padding = em * 0.5f;
    const float available_height = std::max(io.DisplaySize.y - top - margin, em);
    const bool opening = !cheat_focused;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - margin, top), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(em * osl::cheats::kMenuWidthEm, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, available_height));
    if (opening) {
        cheat_focused = true;
        ImGui::SetNextWindowFocus();
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.11f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, accent);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, focus);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, focus);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * 0.7f, window_padding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * scale, style.FramePadding.y * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x * scale, spacing));
    // No ImGui navigation: the list is moved by the menu's own steps.
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##osl_cheat_menu", nullptr, flags)) {
        ImGui::SetWindowFontScale(scale);
        ImGui::TextColored(accent, "%s", osl::cheats::kMenuTitle);
        ImGui::Separator();
        const float body_available = available_height - window_padding * 2.0f - header_height - footer_height;
        const float child_height = std::max(std::min(body_height, body_available), 1.0f);
        const bool needs_scrollbar = osl::cheats::MenuNeedsScrollbar(body_height, child_height);
        const ImGuiWindowFlags child_flags =
            (needs_scrollbar ? ImGuiWindowFlags_AlwaysVerticalScrollbar : ImGuiWindowFlags_NoScrollbar) |
            ImGuiWindowFlags_NoNav;
        ImGui::BeginChild("##cheat_rows", ImVec2(0.0f, child_height), ImGuiChildFlags_None, child_flags);
        ImGui::SetWindowFontScale(scale); // the child does not inherit the outer window's font scale
        ImVec2 selected_min{}, selected_max{};
        int clicked = -1;
        for (int i = 0; i < count; ++i) {
            const bool selected = i == cheat_list.selected;
            if (DrawCheatRow(rows[size_t(i)], size_t(i), selected, scale, accent, focus, dim)) {
                clicked = i;
            }
            if (selected) {
                selected_min = ImGui::GetItemRectMin();
                selected_max = ImGui::GetItemRectMax();
            }
        }
        const ImDrawList* list = ImGui::GetWindowDrawList();
        const float scroll = ImGui::GetScrollY();
        const float wanted = cheat_list.Resolve(scroll, ImGui::GetScrollMaxY(), selected_min.y, selected_max.y,
                                                list->GetClipRectMin().y, list->GetClipRectMax().y);
        if (wanted != scroll) {
            ImGui::SetScrollY(wanted);
        }
        if (clicked >= 0) {
            cheat_list.Click(clicked, count);
            ActivateCheatRow(size_t(clicked));
        }
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::TextColored(dim, "%s", CheatCloseHint().c_str());
        ImGui::TextColored(dim, "%s", osl::cheats::kMenuNote);
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(4);
}

bool HandleCheatKey(const SDL_Event& event, bool down) {
    const SDL_Scancode code = event.key.scancode;
    if (code == OslSettings::Get().cheat_hotkey) {
        if (cheat_open) {
            if (down) {
                cheat_keys_held.insert(int(code));
            } else {
                cheat_keys_held.erase(int(code));
            }
        }
        if (down) {
            PressCheatMenuKey(event.key.repeat);
        }
        return true;
    }
    if (!cheat_open) {
        return false;
    }
    if (code == OslSettings::Get().window_mode_key || code == SDL_SCANCODE_F2) {
        return false; // the port's own shortcuts keep working with the menu open
    }
    if (down) {
        cheat_keys_held.insert(int(code));
    } else {
        cheat_keys_held.erase(int(code));
    }
    if (!down || cheat_close.requested()) {
        return true;
    }
    switch (osl::menu::KeyIntentOf(event.key.key, event.key.repeat)) {
    case osl::menu::KeyIntent::Up: StepCheatList(-1); break;
    case osl::menu::KeyIntent::Down: StepCheatList(1); break;
    case osl::menu::KeyIntent::Activate: ActivateCheatRow(size_t(cheat_list.selected)); break;
    case osl::menu::KeyIntent::Close: cheat_close.Request(); break;
    case osl::menu::KeyIntent::None: break;
    }
    return true;
}

bool HandleCheatEvent(const SDL_Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: return HandleCheatKey(event, event.type == SDL_EVENT_KEY_DOWN);
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        if (!cheat_open) {
            return false;
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && !choice_input && !cheat_close.requested()) {
            const u8 button = event.gbutton.button; // before the runtime's pad state is available
            if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                StepCheatList(-1);
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                StepCheatList(1);
            } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                ActivateCheatRow(size_t(cheat_list.selected));
            } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
                cheat_close.Request();
            }
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: return cheat_open;
    case SDL_EVENT_TEXT_INPUT: return cheat_open;
    case SDL_EVENT_MOUSE_MOTION:
        if (!cheat_open) {
            return false;
        }
        io.AddMousePosEvent(event.motion.x * PixelDensity(event.motion.windowID),
                            event.motion.y * PixelDensity(event.motion.windowID));
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!cheat_open) {
            return false;
        }
        const int button = event.button.button == SDL_BUTTON_LEFT    ? 0
                           : event.button.button == SDL_BUTTON_RIGHT  ? 1
                           : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                                      : -1;
        if (button >= 0 && !cheat_close.requested()) {
            io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (!cheat_open) {
            return false;
        }
        io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
        return true;
    default: return false;
    }
}

} // namespace

bool HandleEvent(const SDL_Event& event) {
    std::scoped_lock lock{imgui_mutex};
    if (!initialized) {
        return false;
    }
    if (choice_active) {
        return HandleChoiceEvent(event);
    }
    return HandleCheatEvent(event);
}

void SetBackgroundLoading(u32 done, u32 total) {
    background_done = done;
    background_total = total;
}

void SetLoading(bool active, u32 done, u32 total) {
    loading_done = done;
    loading_total = total;
    loading = active;
}

void QueueStartupNotice(std::string text) {
    std::scoped_lock lock{notice_mutex};
    for (const OslNotice::StartupNotice& notice : startup_notices) {
        if (notice.text() == text) {
            return; // already queued this launch
        }
    }
    OslNotice::StartupNotice notice;
    notice.QueueStartup(std::move(text));
    startup_notices.push_back(std::move(notice));
}

bool Visible() {
    const auto& s = OslSettings::Get();
    return initialized && (loading || background_total || cheat_open || text_entry_active || choice_active ||
                           choice_retiring || s.show_fps || AnyStartupNoticePending());
}

void SetDisplayScale(float scale) {
    display_scale = scale > 0.0f ? scale : 1.0f;
}

bool CapturesInput() {
    return cheat_open || text_entry_active || choice_active;
}

void BeginChoice(const std::string& title, const std::vector<std::string>& items,
                 std::vector<OslPng::Image> pictures, int focus) {
    std::scoped_lock lock{imgui_mutex};
    RetireChoiceTextures();
    choice_pictures = std::move(pictures);
    choice_pictures.resize(items.size());
    choice_title = title;
    choice_items = items;
    choice_index = focus >= 0 && focus < int(items.size()) ? focus : 0;
    choice_result = -1;
    choice_active = true;
}

int PollChoice() {
    return choice_result;
}

void SetTextEntry(bool active, const std::string& prompt, const std::string& text) {
    std::scoped_lock lock{imgui_mutex};
    text_entry_active = active;
    text_entry_prompt = prompt;
    text_entry_text = text;
}

std::atomic<u64> presented_frames{0};

u64 PresentedFrames() {
    return presented_frames.load(std::memory_order_relaxed);
}

void LogPresent() {
    presented_frames.fetch_add(1, std::memory_order_relaxed);
}

void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent) {
    // Present interval for the FPS readout (measured also while nothing is drawn).
    const auto now = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli>(now - last_present).count();
    last_present = now;
    if (ms > 0.0f && ms < 1000.0f) {
        frame_ms_avg = frame_ms_avg == 0.0f ? ms : frame_ms_avg * 0.95f + ms * 0.05f;
    }
    PollChoiceInput();
    if (!Visible()) {
        return;
    }
    // The runtime's pad state is read before the UI lock, as the save list does.
    const uint32_t cheat_buttons = cheat_open && choice_input ? choice_input() : 0;
    std::scoped_lock lock{imgui_mutex};
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(extent.width), float(extent.height));
    io.DeltaTime = ms > 0.0f && ms < 1000.0f ? ms / 1000.0f : 1.0f / 60.0f;
    // UI scale follows the display height (1080p = 1).
    const float scale = std::max(float(extent.height) / 1080.0f, 0.75f);
    if (std::abs(scale - base_scale) > 0.01f) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale / base_scale);
        style.FontScaleMain = scale;
        base_scale = scale;
    }

    UpdateChoiceTextures();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (loading) {
        LoadingScreen();
    }
    if (OslSettings::Get().show_fps) {
        FpsCounter();
    }
    StartupNotices();
    if (background_total && !loading) {
        BackgroundLoading();
    }
    if (text_entry_active) {
        TextEntryBox();
    }
    if (choice_active) {
        ChoiceBox();
    }
    if (cheat_open) {
        PollCheatPad(cheat_buttons);
        CheatMenu();
    }
    ImGui::Render();

    const vk::RenderingAttachmentInfo attachment{
        .imageView = view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    });
    {
        // Font atlas uploads submit to the graphics queue themselves.
        std::scoped_lock submit_lock{Vulkan::Scheduler::submit_mutex};
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf);
    }
    cmdbuf.endRendering();
}

} // namespace OslOverlay

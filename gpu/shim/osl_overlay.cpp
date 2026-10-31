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
#include <vector>

#include <SDL3/SDL.h>
#include "osl_accent.h"
#include "osl_notice.h"
#include "osl_settings.h"
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

bool HandleEvent(const SDL_Event& event) {
    std::scoped_lock lock{imgui_mutex};
    if (!initialized || !choice_active) {
        return false;
    }
    return HandleChoiceEvent(event);
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
    return initialized && (loading || background_total || text_entry_active || choice_active ||
                           choice_retiring || s.show_fps || AnyStartupNoticePending());
}

void SetDisplayScale(float scale) {
    display_scale = scale > 0.0f ? scale : 1.0f;
}

bool CapturesInput() {
    return text_entry_active || choice_active;
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

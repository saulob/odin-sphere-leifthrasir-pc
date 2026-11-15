// Focused tests of the Frame rate setting: the mode table, which flips reach the screen, the repeat cadence above 60 and
// the limiter that schedules it (osl_frame_pacing.h), plus source checks that the pacing lives once in VideoOut, above
// every renderer, and leaves the game's own timing alone. No window, no GPU and no game files.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "osl_frame_pacing.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                             \
    do {                                                        \
        if (!(cond)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            ++g_failures;                                       \
        }                                                       \
    } while (0)

namespace Pacing = OslFramePacing;
using Clock = std::chrono::steady_clock;

std::string ReadSource(const char* path) {
    std::ifstream file(std::string(OSL_SOURCE_DIR) + "/" + path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

bool InOrder(const std::string& text, const std::string& first, const std::string& second) {
    const std::size_t a = text.find(first);
    return a != std::string::npos && text.find(second, a + first.size()) != std::string::npos;
}

// The text from `start` to the first closing brace line that follows (`close`): one function's body.
std::string FunctionBody(const std::string& text, const std::string& start, const char* close = "\n}\n") {
    const std::size_t at = text.find(start);
    if (at == std::string::npos) {
        return {};
    }
    const std::size_t end = text.find(close, at);
    return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

void TestModeTable() {
    const int expected[] = {30, 60, 90, 120, 144, 165, 240, 0};
    CHECK(Pacing::kFrameRateModes.size() == std::size(expected));
    for (std::size_t i = 0; i < std::size(expected); ++i) {
        CHECK(Pacing::kFrameRateModes[i] == expected[i]);
        CHECK(Pacing::IsFrameRateMode(expected[i]));
    }
    CHECK(Pacing::kGameHz == 60 && Pacing::kUnlimitedCadenceHz == 480);
    CHECK(!Pacing::IsFrameRateMode(-1) && !Pacing::IsFrameRateMode(45) && !Pacing::IsFrameRateMode(75) &&
          !Pacing::IsFrameRateMode(1000));
    CHECK(Pacing::RenderFps(45) == 60 && Pacing::RenderFps(-1) == 60);
    CHECK(Pacing::RenderFps(0) == 0 && Pacing::RenderFps(144) == 144);
}

void TestDefaultIsTheOriginalBehavior() {
    CHECK(!Pacing::IsHighRate(60) && !Pacing::IsHighRate(30) && !Pacing::IsHighRate(-1) && !Pacing::IsHighRate(75));
    for (const bool vsync : {false, true}) {
        for (const double display : {0.0, 59.94, 60.0, 144.0}) {
            CHECK(Pacing::RepeatCadenceHz(60, vsync, display) == 0);
            CHECK(Pacing::RepeatCadenceHz(30, vsync, display) == 0);
        }
    }
    for (std::uint64_t flip = 0; flip < 8; ++flip) {
        CHECK(Pacing::PresentsFlip(60, flip));
    }
}

void TestRepeatCadence() {
    for (const int fps : {90, 120, 144, 165, 240}) {
        CHECK(Pacing::IsHighRate(fps));
        CHECK(Pacing::RepeatCadenceHz(fps) == static_cast<unsigned>(fps));
        for (std::uint64_t flip = 0; flip < 8; ++flip) {
            CHECK(Pacing::PresentsFlip(fps, flip));
        }
    }
    CHECK(Pacing::IsHighRate(0) && Pacing::IsUnlimited(0));
    CHECK(Pacing::RepeatCadenceHz(0) == static_cast<unsigned>(Pacing::kUnlimitedCadenceHz));
    CHECK(Pacing::RepeatCadenceHz(0, false, 144.0) == static_cast<unsigned>(Pacing::kUnlimitedCadenceHz));
}

void TestNominalHz() {
    CHECK(Pacing::NominalHz(59.94) == 60 && Pacing::NominalHz(60.0) == 60 && Pacing::NominalHz(119.88) == 120);
    CHECK(Pacing::NominalHz(143.856) == 144 && Pacing::NominalHz(164.9) == 165 && Pacing::NominalHz(239.76) == 240);
    CHECK(Pacing::VsyncBoundsRate(true, 60.0) && !Pacing::VsyncBoundsRate(false, 60.0) &&
          !Pacing::VsyncBoundsRate(true, 0.0));
}

void TestThirtyShowsEverySecondFlip() {
    int shown = 0;
    for (std::uint64_t flip = 0; flip < 100; ++flip) {
        shown += Pacing::PresentsFlip(30, flip) ? 1 : 0;
    }
    CHECK(shown == 50);
    CHECK(Pacing::PresentsFlip(30, 0) && !Pacing::PresentsFlip(30, 1) && Pacing::PresentsFlip(30, 2) &&
          !Pacing::PresentsFlip(30, 3));
}

// The driver takes every flip: each one is shown or given back, none is left unreleased.
void TestEveryFlipIsConsumed() {
    for (const int setting : {30, 60, 90, 120, 144, 165, 240, 0}) {
        int presented = 0, retired = 0;
        for (std::uint64_t flip = 0; flip < 1000; ++flip) {
            (Pacing::PresentsFlip(setting, flip) ? presented : retired)++;
        }
        CHECK(presented + retired == 1000);
        CHECK(setting == 30 ? (presented == 500 && retired == 500) : (presented == 1000 && retired == 0));
    }
}

void TestVsyncBoundsTheCadence() {
    CHECK(Pacing::EffectiveRenderHz(120, true, 60.0) == 60);
    CHECK(Pacing::EffectiveRenderHz(120, true, 59.94) == 60);
    CHECK(Pacing::EffectiveRenderHz(144, true, 143.856) == 144);
    CHECK(Pacing::EffectiveRenderHz(0, true, 165.0) == 165);
    CHECK(Pacing::EffectiveRenderHz(0, false, 165.0) == 0);
    CHECK(Pacing::EffectiveRenderHz(120, false, 60.0) == 120);
    CHECK(Pacing::EffectiveRenderHz(120, true, 0.0) == 120);
    CHECK(Pacing::EffectiveRenderHz(30, true, 144.0) == 30 && Pacing::EffectiveRenderHz(60, true, 144.0) == 60);
    // A display at or below the game's rate leaves nothing to repeat.
    CHECK(Pacing::RepeatCadenceHz(120, true, 60.0) == 0);
    CHECK(Pacing::RepeatCadenceHz(120, true, 59.94) == 0);
    CHECK(Pacing::RepeatCadenceHz(0, true, 60.0) == 0);
    CHECK(Pacing::RepeatCadenceHz(120, true, 144.0) == 120);
    CHECK(Pacing::RepeatCadenceHz(240, true, 143.856) == 144);
    CHECK(Pacing::RepeatCadenceHz(0, true, 165.0) == 165);
    CHECK(Pacing::RepeatCadenceHz(240, false, 144.0) == 240);
    CHECK(Pacing::RepeatCadenceHz(240, true, 0.0) == 240);
    CHECK(Pacing::RepeatCadenceHz(0, true, 1000.0) == static_cast<unsigned>(Pacing::kUnlimitedCadenceHz));
}

// Issue #13: the 120 setting on the displays a player is likely to have.
void TestOneHundredTwenty() {
    CHECK(Pacing::IsFrameRateMode(120) && Pacing::IsHighRate(120) && Pacing::RenderFps(120) == 120);
    CHECK(Pacing::RepeatCadenceHz(120, false, 0.0) == 120);
    CHECK(Pacing::RepeatCadenceHz(120, false, 60.0) == 120);

    CHECK(Pacing::EffectiveRenderHz(120, true, 60.0) == 60 && Pacing::RepeatCadenceHz(120, true, 60.0) == 0);
    CHECK(Pacing::EffectiveRenderHz(120, true, 59.94) == 60 && Pacing::RepeatCadenceHz(120, true, 59.94) == 0);

    for (const double display : {119.88, 120.0}) {
        CHECK(Pacing::NominalHz(display) == 120);
        CHECK(Pacing::EffectiveRenderHz(120, true, display) == 120);
        CHECK(Pacing::RepeatCadenceHz(120, true, display) == 120);
    }
    for (const double display : {143.856, 144.0, 165.0, 239.76, 240.0}) {
        CHECK(Pacing::EffectiveRenderHz(120, true, display) == 120);
        CHECK(Pacing::RepeatCadenceHz(120, true, display) == 120);
    }
    CHECK(Pacing::ParseFrameRate("120") == 120 && Pacing::ModeName(120) == "120");
}

void TestLimiterSlots() {
    const auto start = Clock::now();
    Pacing::FrameLimiter off{0, start};
    CHECK(!off.Enabled() && off.Due(start) && off.Period().count() == 0);

    Pacing::FrameLimiter limiter{120, start};
    const auto period = limiter.Period();
    CHECK(limiter.Enabled() && period == std::chrono::nanoseconds(8333333));
    CHECK(limiter.Due(start));
    limiter.Release(start + std::chrono::microseconds(100));
    CHECK(limiter.NextSlot() == start + period);
    CHECK(!limiter.Due(start + period - std::chrono::nanoseconds(1)));
    CHECK(limiter.Due(start + period));
    // A late wake does not move the later slots.
    limiter.Release(start + period + std::chrono::milliseconds(3));
    CHECK(limiter.NextSlot() == start + 2 * period);
}

void TestLateWakeDoesNotBurst() {
    const auto start = Clock::time_point{};
    Pacing::FrameLimiter limiter{120, start};
    const auto period = limiter.Period();
    // More than one period late re-anchors to now + period.
    const auto late = start + 10 * period;
    limiter.Release(late);
    CHECK(limiter.NextSlot() == late + period);
    CHECK(!limiter.Due(late) && !limiter.Due(late + period - std::chrono::nanoseconds(1)));
    // Lateness is not repaid: after a long stall the next slots are one period apart, never due together.
    int due_now = 0;
    const auto after_stall = late + 5 * period;
    Pacing::FrameLimiter stalled{120, start};
    stalled.Release(after_stall);
    for (int i = 0; i < 8; ++i) {
        if (stalled.Due(after_stall)) {
            ++due_now;
            stalled.Release(after_stall);
        }
    }
    CHECK(due_now == 0);
    // Exactly one period late still keeps the grid; one nanosecond more re-anchors.
    Pacing::FrameLimiter edge{120, start};
    edge.Release(start + period);
    CHECK(edge.NextSlot() == start + period);
    Pacing::FrameLimiter beyond{120, start};
    beyond.Release(start + period + std::chrono::nanoseconds(1));
    CHECK(beyond.NextSlot() == start + 2 * period + std::chrono::nanoseconds(1));
}

// Absolute slots: after many presentations the schedule has not drifted, even when the period is not a whole
// number of milliseconds or a multiple of the game's 60 Hz.
void TestAbsoluteSlotsDoNotDrift() {
    for (const unsigned hz : {90u, 120u, 144u, 165u, 240u, 480u}) {
        const auto start = Clock::time_point{};
        Pacing::FrameLimiter limiter{hz, start};
        for (unsigned i = 0; i < hz * 10; ++i) {
            limiter.Release(limiter.NextSlot());
        }
        const auto expected = start + std::chrono::seconds(10);
        const auto error = std::chrono::abs(limiter.NextSlot() - expected);
        CHECK(error < std::chrono::microseconds(10));
    }
    // A fixed rounded-millisecond sleep would run 3.7% slow at 144 Hz (7 ms for 6.94 ms).
    Pacing::FrameLimiter limiter{144, Clock::time_point{}};
    CHECK(limiter.Period() == std::chrono::nanoseconds(6944444));
}

// Presentations the cadence schedules in one second of 60 Hz vblank ticks: each tick shows its new frame, then the
// repeats whose slots fall before the next tick, as the present thread does.
struct Second {
    int presents = 0;
    int new_frames = 0;
    int repeats = 0;
    long long longest_gap_ns = 0;
};

Second Simulate(unsigned cadence, int ticks) {
    Second second;
    const auto start = Clock::time_point{};
    const auto tick = std::chrono::nanoseconds(16'666'667);
    Pacing::FrameLimiter limiter{cadence, start};
    Clock::time_point last = start;
    bool first = true;
    const auto note = [&](Clock::time_point at) {
        ++second.presents;
        if (!first) {
            second.longest_gap_ns = std::max<long long>(second.longest_gap_ns, (at - last).count());
        }
        first = false;
        last = at;
    };
    for (int i = 0; i < ticks; ++i) {
        const auto tick_start = start + i * tick;
        const auto deadline = tick_start + tick;
        limiter.Release(tick_start);
        ++second.new_frames;
        note(tick_start);
        while (limiter.Enabled()) {
            const auto slot = limiter.NextSlot();
            if (slot >= deadline) {
                break;
            }
            limiter.Release(slot);
            ++second.repeats;
            note(slot);
        }
    }
    return second;
}

void TestRepeatsFillTheTicks() {
    const Second off = Simulate(0, 60);
    CHECK(off.presents == 60 && off.repeats == 0 && off.new_frames == 60);
    for (const unsigned hz : {90u, 120u, 144u, 165u, 240u, 480u}) {
        const Second second = Simulate(hz, 60);
        CHECK(second.new_frames == 60);
        CHECK(second.presents >= static_cast<int>(hz) - 61 && second.presents <= static_cast<int>(hz) + 61);
        CHECK(second.presents == second.new_frames + second.repeats);
        // No gap longer than a tick, so a repeat never delays a new frame.
        CHECK(second.longest_gap_ns <= 16'666'667);
        // Ten seconds of ticks land on the requested rate, not on a multiple of 60.
        const Second long_run = Simulate(hz, 600);
        CHECK(std::abs(long_run.presents - static_cast<int>(hz) * 10) <= 61);
    }
}

// One guest frame interval at 120: the new frame, then one repeat of it half a tick later.
void TestOneHundredTwentySlotStructure() {
    const Second twice = Simulate(120, 60);
    CHECK(twice.repeats >= 58 && twice.repeats <= 61);
    const auto start = Clock::time_point{};
    const auto tick = std::chrono::nanoseconds(16'666'667);
    Pacing::FrameLimiter limiter{120, start};
    limiter.Release(start);
    CHECK(limiter.NextSlot() == start + std::chrono::nanoseconds(8'333'333));
    CHECK(limiter.NextSlot() < start + tick);
    limiter.Release(limiter.NextSlot());
    CHECK(limiter.NextSlot() == start + std::chrono::nanoseconds(16'666'666));
    // The second slot falls at or just before the next tick: it belongs to the next interval's new frame.
    CHECK(limiter.NextSlot() < start + tick + std::chrono::nanoseconds(1));
}

void TestUnlimitedIsBounded() {
    CHECK(Pacing::RepeatCadenceHz(0, false, 0.0) == 480);
    CHECK(Pacing::RepeatCadenceHz(0, false, 1000.0) == 480);
    CHECK(Pacing::RepeatCadenceHz(0, true, 0.0) == 480);
    CHECK(Pacing::RepeatCadenceHz(0, true, 144.0) == 144);
    CHECK(Pacing::RepeatCadenceHz(0, true, 360.0) == 360);
    CHECK(Pacing::RepeatCadenceHz(0, true, 540.0) == 480);
    const Second second = Simulate(Pacing::RepeatCadenceHz(0), 60);
    CHECK(second.presents <= 480 + 61);
    CHECK(Pacing::ModeName(0) == "Unlimited");
}

void TestParseFrameRate() {
    for (const int mode : Pacing::kFrameRateModes) {
        CHECK(Pacing::ParseFrameRate(std::to_string(mode)) == mode);
    }
    for (const char* invalid : {"", "abc", "garbage", "45", "-1", "090", "+90", "90.0", "90fps", "0x5a", "0.5", "00", " 90",
                                "1000"}) {
        CHECK(Pacing::ParseFrameRate(invalid) == 60);
    }
}

void TestModeName() {
    CHECK(Pacing::ModeName(0) == "Unlimited" && Pacing::ModeName(144) == "144" && Pacing::ModeName(60) == "60");
}

// The game's own pacing stays as it was: the vblank is a steady 60 Hz and nothing in the guest is hooked for the
// Frame rate.
void TestGamePacingIsUntouched() {
    const std::string emulator = ReadSource("gpu/shim/core/emulator_settings.h");
    CHECK(Contains(emulator, "u32 GetVblankFrequency() { return 60; }"));
    CHECK(Contains(emulator, "u32 GetFrameLimit() { return 0; }"));
    for (const char* file : {"src/loader.c", "src/runtime_thread.c", "src/runtime_time.c", "launcher/skip_intro.c",
                             "launcher/resolution.c"}) {
        const std::string source = ReadSource(file);
        CHECK(!Contains(source, "frame_rate") && !Contains(source, "FramePacing"));
    }
    const std::string driver = ReadSource("gpu/shadps4/core/libraries/videoout/driver.cpp");
    const std::string constructor = FunctionBody(driver, "VideoOutDriver::VideoOutDriver(");
    CHECK(!constructor.empty() && !Contains(constructor, "frame_rate") && !Contains(constructor, "FramePacing"));
    // The vblank period is the original one; the setting only adds presentations inside it.
    const std::string present_thread = FunctionBody(driver, "void VideoOutDriver::PresentThread(");
    CHECK(Contains(present_thread, "vblank_period(1000000000 /"));
    CHECK(Contains(present_thread, "EmulatorSettings.GetVblankFrequency()"));
    CHECK(Contains(present_thread, "vblank_status.count++;"));
    CHECK(!Contains(present_thread, "vblank_period = ") && !Contains(present_thread, "vblank_status.count += "));
}

// One pacing implementation, in VideoOut above the presenter boundary; the setting reaches every renderer.
void TestVideoOutWiring() {
    const std::string driver = ReadSource("gpu/shadps4/core/libraries/videoout/driver.cpp");
    const std::string flip = FunctionBody(driver, "void VideoOutDriver::Flip(");
    CHECK(InOrder(flip, "PresentsFlip(OslSettings::Get().frame_rate.load(), flips_consumed++)", "RetireFrame(frame)"));
    CHECK(InOrder(flip, "RetireFrame(frame)", "presenter->Present(frame)"));
    CHECK(InOrder(flip, "if (!present) {", "return;"));

    const std::string thread = FunctionBody(driver, "void VideoOutDriver::PresentThread(");
    CHECK(!thread.empty());
    CHECK(Contains(thread, "OslFramePacing::FrameLimiter limiter{OslFramePacing::RepeatCadenceHz(target_fps, vsync, display_hz)"));
    CHECK(Contains(thread, "OslSettings::Get().display_refresh_hz"));
    CHECK(InOrder(thread, "limiter.Release(tick_start);", "Flip(request);"));
    // The repeats come after the vblank events and the status update of the tick, and only while the port is open.
    CHECK(InOrder(thread, "main_port.vblank_cv.notify_all();", "limiter.Enabled() && main_port.is_open"));
    CHECK(InOrder(thread, "limiter.Enabled() && main_port.is_open", "RepeatLastFrame();"));
    CHECK(InOrder(thread, "RepeatLastFrame();", "timer.End();"));
    CHECK(InOrder(thread, "WaitUntilSlot(slot);", "limiter.Release(std::chrono::steady_clock::now());"));
    CHECK(Contains(thread, "if (slot >= tick_deadline) {"));
    CHECK(Contains(thread, "frame rate %s, repeat cadence %s, VSync %s"));
    // Only a flip takes a request; a repeat never does.
    const std::string repeat = FunctionBody(driver, "void VideoOutDriver::RepeatLastFrame(");
    CHECK(Contains(repeat, "DrawLastFrame();") && !Contains(repeat, "receive_request") && !Contains(repeat, "Flip("));
    const std::string draw_last = FunctionBody(driver, "void VideoOutDriver::DrawLastFrame(");
    CHECK(Contains(draw_last, "presenter->PrepareLastFrame()") && Contains(draw_last, "presenter->Present(frame, true)"));
    CHECK(Contains(draw_last, "}, true);")); // only when the swap thread is idle
    // The guest's flip counters move on a flip alone.
    CHECK(!Contains(draw_last, "flip_status") && !Contains(repeat, "flip_status"));

    const std::string settings = ReadSource("gpu/shim/osl_settings.cpp");
    CHECK(Contains(settings, "key == \"frame_rate\""));
    CHECK(Contains(settings, "ParseFrameRate(value)"));
    const std::string window = ReadSource("gpu/shim/window.cpp");
    CHECK(Contains(window, "display_refresh_hz = mode->refresh_rate"));
}

// Frame rate is not conditional on the renderer: the pacing code names none, and no presenter implements any of it.
void TestRendererIndependence() {
    const std::string driver = ReadSource("gpu/shadps4/core/libraries/videoout/driver.cpp");
    const std::string header = ReadSource("gpu/shadps4/core/libraries/videoout/driver.h");
    const std::string backend = ReadSource("gpu/shadps4/video_core/renderer_backend.h");
    for (const std::string* source : {&driver, &header}) {
        CHECK(!Contains(*source, "OslRenderer") && !Contains(*source, "Renderer::Vulkan") &&
              !Contains(*source, "Renderer::OpenGL") && !Contains(*source, "Renderer::Direct3D11"));
        CHECK(!Contains(*source, "OpenGL::Presenter") && !Contains(*source, "Vulkan::Presenter"));
        CHECK(!Contains(*source, "gl_presenter.h"));
    }
    CHECK(Contains(driver, "presenter->RetireFrame(frame)") && Contains(driver, "presenter->PrepareLastFrame()"));
    CHECK(Contains(backend, "virtual void RetireFrame(PresentFrame* frame) = 0;"));
    CHECK(Contains(backend, "virtual PresentFrame* PrepareLastFrame() = 0;"));
    CHECK(Contains(backend, "virtual void Present(PresentFrame* frame, bool is_reusing_frame = false"));

    const std::string vulkan = ReadSource("gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp");
    const std::string vulkan_header = ReadSource("gpu/shadps4/video_core/renderer_vulkan/vk_presenter.h");
    const std::string opengl = ReadSource("gpu/shadps4/video_core/renderer_opengl/gl_presenter.cpp");
    const std::string opengl_header = ReadSource("gpu/shadps4/video_core/renderer_opengl/gl_presenter.h");
    for (const std::string* source : {&vulkan, &vulkan_header, &opengl, &opengl_header}) {
        CHECK(!Contains(*source, "OslFramePacing") && !Contains(*source, "FrameLimiter") &&
              !Contains(*source, "frame_rate") && !Contains(*source, "RepeatCadenceHz") &&
              !Contains(*source, "PresentsFlip"));
    }
    // The renderer-selection code does not touch the setting either.
    for (const char* file : {"gpu/shim/osl_renderer.h", "gpu/shim/oslgpu.cpp", "gpu/shadps4/video_core/renderer_factory.cpp"}) {
        const std::string source = ReadSource(file);
        CHECK(!Contains(source, "frame_rate") && !Contains(source, "FramePacing"));
    }
    // Retiring gives the frame back without showing it: Vulkan returns it to the free queue, OpenGL has no frame
    // ownership to release.
    CHECK(InOrder(vulkan, "void Presenter::RetireFrame(Frame* frame) {", "free_queue.push_back(frame);"));
    CHECK(!Contains(FunctionBody(vulkan, "void Presenter::RetireFrame(Frame* frame) {"), "swapchain"));
    CHECK(Contains(opengl_header, "void RetireFrame(VideoCore::PresentFrame*) override {}"));
    // A repeat is the last frame shown again: the OpenGL one is queued to the thread that owns the context.
    CHECK(Contains(opengl, "liverpool->SendCommand([this, frame, is_game_frame] {"));
}

// Frame rate is a host-side VideoOut feature: nothing in it reads a build, a region, a language or an executable.
void TestBuildIndependence() {
    const std::string pacing = ReadSource("gpu/shim/osl_frame_pacing.h");
    const std::string driver = ReadSource("gpu/shadps4/core/libraries/videoout/driver.cpp");
    const std::string thread = FunctionBody(driver, "void VideoOutDriver::PresentThread(");
    const std::string flip = FunctionBody(driver, "void VideoOutDriver::Flip(");
    for (const std::string* source : {&pacing, &thread, &flip}) {
        for (const char* word : {"CUSA", "SupportedBuilds", "GameBuild", "eboot", "title_id", "text_language", "region",
                                 "Language", "cpk", "Cpk", "package", "0x"}) {
            CHECK(!Contains(*source, word));
        }
    }
    const std::string settings = ReadSource("gpu/shim/osl_settings.cpp");
    const std::string reader = FunctionBody(settings, "void Set(Values& v, const std::string& key, const std::string& value) {");
    const std::size_t at = reader.find("key == \"frame_rate\"");
    CHECK(at != std::string::npos);
    // The branch is one line: no other key or build condition in it.
    CHECK(reader.substr(at, 90).find("ParseFrameRate(value)") != std::string::npos);
}

// The launcher's row: on the Gameplay page directly before Frame rate in title, offered to every build.
void TestLauncherRow() {
    const std::string launcher = ReadSource("launcher/launcher.cpp");
    const char* const member_end = "\n    }\n";
    const std::string gameplay = FunctionBody(launcher, "    void PageGameplay(trg::Ui& ui) {", member_end);
    CHECK(!gameplay.empty());
    const std::size_t row = gameplay.find("ui.Combo(\"Frame rate\", kFrameRateHelp, kFrameRateKey, \"60\", frame_rates);");
    const std::size_t title = gameplay.find("ui.Toggle(\"Frame rate in title\"");
    CHECK(row != std::string::npos && title != std::string::npos && row < title);
    CHECK(gameplay.find("ui.", row + 1) == title); // nothing between the two rows
    CHECK(InOrder(gameplay, "ui.Toggle(\"Unlock Bookshelf\"", "ui.Combo(\"Frame rate\""));
    CHECK(InOrder(gameplay, "ui.Combo(\"Frame rate\"", "ui.Toggle(\"Render resolution in title\""));
    CHECK(!Contains(FunctionBody(launcher, "    void PageDisplay(trg::Ui& ui) {", member_end), "Frame rate\""));
    CHECK(!Contains(FunctionBody(launcher, "    void PageGraphics(trg::Ui& ui) {", member_end), "Frame rate\""));
    // Top level of the page: not inside a feature, language or renderer condition.
    const std::size_t line_start = gameplay.rfind('\n', row) + 1;
    CHECK(gameplay.substr(line_start, row - line_start) == "        ");
    const std::size_t loop = gameplay.find("for (const int fps : {30, 60, 90, 120, 144, 165, 240, 0}) {");
    CHECK(loop != std::string::npos && loop < row);
    CHECK(Contains(launcher, "FrameRateLabel(fps)"));
    // The cheats hotkey page and the Cheats page do not carry it.
    CHECK(!Contains(FunctionBody(launcher, "    void PageCheats(trg::Ui& ui) {", member_end), "Frame rate"));
}

} // namespace

int main() {
    TestModeTable();
    TestDefaultIsTheOriginalBehavior();
    TestRepeatCadence();
    TestNominalHz();
    TestThirtyShowsEverySecondFlip();
    TestEveryFlipIsConsumed();
    TestVsyncBoundsTheCadence();
    TestOneHundredTwenty();
    TestLimiterSlots();
    TestLateWakeDoesNotBurst();
    TestAbsoluteSlotsDoNotDrift();
    TestRepeatsFillTheTicks();
    TestOneHundredTwentySlotStructure();
    TestUnlimitedIsBounded();
    TestParseFrameRate();
    TestModeName();
    TestGamePacingIsUntouched();
    TestVideoOutWiring();
    TestRendererIndependence();
    TestBuildIndependence();
    TestLauncherRow();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("frame pacing tests passed\n");
    return 0;
}

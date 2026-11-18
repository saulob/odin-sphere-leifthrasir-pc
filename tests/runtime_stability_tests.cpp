// Runtime stability: audio backlog cap, pipeline startup decision and current-toolchain sources.
// Usage: runtime_stability_tests [--audio | --pipeline | --toolchain] (no argument runs all). No audio or Vulkan device.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "audio_backlog.h"
#include "osl_pipeline_startup.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                                                       \
    do {                                                                                                       \
        ++g_checks;                                                                                            \
        if (!(condition)) {                                                                                    \
            ++g_failures;                                                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
        }                                                                                                      \
    } while (0)

std::string ReadAll(const std::string& relative) {
    std::ifstream file(std::string(OSL_SOURCE_DIR) + "/" + relative, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

bool Has(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

// Text from the first `start` to the next line that is only a closing brace at column 0.
std::string Body(const std::string& source, const std::string& start) {
    const auto begin = source.find(start);
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = source.find("\n}\n", begin);
    return source.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

struct Format {
    const char* name;
    int frames;
    size_t frame_bytes;
};

// The documented rule, written out independently of the header.
int64_t ExpectedCap(const Format& f) {
    const int64_t bytes = static_cast<int64_t>(f.frames) * static_cast<int64_t>(f.frame_bytes);
    return 2 * bytes + bytes * 4800 / f.frames;
}

void TestBacklogCap() {
    for (const Format& f : {Format{"256 frames stereo float", 256, 8}, Format{"256 frames 8ch float", 256, 32},
                            Format{"2048 frames 8ch float", 2048, 32}}) {
        const size_t bytes = static_cast<size_t>(f.frames) * f.frame_bytes;
        const int low = 2 * static_cast<int>(bytes);
        const int cap = static_cast<int>(ExpectedCap(f));
        CHECK(cap == low + 4800 * static_cast<int>(f.frame_bytes));
        CHECK(!audio_backlog_stale(0, low, bytes, f.frames));
        CHECK(!audio_backlog_stale(low, low, bytes, f.frames));
        CHECK(!audio_backlog_stale(cap - 1, low, bytes, f.frames));
        CHECK(!audio_backlog_stale(cap, low, bytes, f.frames));
        CHECK(audio_backlog_stale(cap + 1, low, bytes, f.frames));
        CHECK(audio_backlog_stale(cap * 4, low, bytes, f.frames));
        CHECK(audio_backlog_stale(1 << 28, low, bytes, f.frames));
    }
    for (const int length : {256, 512, 768, 1024, 2048}) {
        for (const size_t frame_bytes : {size_t{2}, size_t{4}, size_t{8}, size_t{16}, size_t{32}}) {
            const Format f{"", length, frame_bytes};
            const size_t bytes = static_cast<size_t>(length) * frame_bytes;
            const int low = 2 * static_cast<int>(bytes), cap = static_cast<int>(ExpectedCap(f));
            CHECK(!audio_backlog_stale(cap, low, bytes, length));
            CHECK(audio_backlog_stale(cap + 1, low, bytes, length));
        }
    }

    // Invalid input never resets.
    CHECK(!audio_backlog_stale(1 << 28, 4096, 2048, 0));
    CHECK(!audio_backlog_stale(1 << 28, 4096, 2048, -256));
    CHECK(!audio_backlog_stale(1 << 28, 4096, 2048, -1));
    CHECK(!audio_backlog_stale(1 << 28, 4096, 0, 256));
    CHECK(!audio_backlog_stale(-1, 4096, 2048, 256));
    CHECK(!audio_backlog_stale(-(1 << 28), 4096, 2048, 256));
    CHECK(!audio_backlog_stale(0, 0, 2048, 256));
}

void TestBacklogReset() {
    constexpr size_t bytes = 2048;
    constexpr int frames = 256, low = 4096, cap = low + 4800 * 8;

    for (const int level : {0, low, cap - 1, cap}) {
        int queued = level, window = 17, count_before = window;
        int64_t adjust = 123456;
        CHECK(audio_backlog_drop(&queued, low, bytes, frames, &window, &adjust) == 0);
        CHECK(queued == level && window == count_before && adjust == 123456);
    }
    for (const int level : {cap + 1, cap * 3}) {
        int queued = level, window = 17;
        int64_t adjust = -123456;
        CHECK(audio_backlog_drop(&queued, low, bytes, frames, &window, &adjust) == level - low);
        CHECK(queued == 0 && window == 0 && adjust == 0);
    }
    for (const int bad_frames : {0, -256}) {
        int queued = 1 << 28, window = 17;
        int64_t adjust = 99;
        CHECK(audio_backlog_drop(&queued, low, bytes, bad_frames, &window, &adjust) == 0);
        CHECK(queued == 1 << 28 && window == 17 && adjust == 99);
    }
    int queued = -1, window = 5;
    int64_t adjust = 7;
    CHECK(audio_backlog_drop(&queued, low, bytes, frames, &window, &adjust) == 0 && queued == -1 && window == 5 &&
          adjust == 7);

    // Milliseconds in the log: one buffer is 256 frames = 5.333 ms.
    const uint64_t period = 256ull * 1000000000ull / 48000ull;
    CHECK(static_cast<int>(audio_backlog_ms(bytes, period, bytes) * 1000) == 5333);
    CHECK(static_cast<int>(audio_backlog_ms(4800 * 8, period, bytes) + 0.5) == 100);
    CHECK(static_cast<int>(audio_backlog_ms(4800 * 8 * 30, period, bytes) + 0.5) == 3000);
    // 2048-frame 8ch float: 65536 bytes, 42.667 ms per buffer; 2 s is 96000 frames * 32 bytes.
    const uint64_t big_period = 2048ull * 1000000000ull / 48000ull;
    CHECK(static_cast<int>(audio_backlog_ms(96000 * 32, big_period, 65536) + 0.5) == 2000);
    CHECK(audio_backlog_ms(0, period, bytes) == 0.0);
}

void TestAudioSource() {
    const std::string audio = ReadAll("src/runtime_audio.c");
    const std::string output = Body(audio, "static int32_t output_port(");
    CHECK(Has(audio, "#include \"audio_backlog.h\""));
    const auto drop = output.find("int dropped=audio_backlog_drop(&queued,low,bytes,p->frames,&p->window_count,&p->adjust_ns);");
    const auto starved = output.find("if (queued<(int)bytes) {");
    const auto window = output.find("if (!p->window_count || queued<p->window_min)");
    CHECK(drop != std::string::npos && starved != std::string::npos && window != std::string::npos);
    CHECK(drop < starved && starved < window);
    const std::string call_site = output.substr(drop, starved - drop);
    // Clear and log only when something was dropped; the normal path has no other clear.
    CHECK(Has(call_site, "if (dropped) {"));
    CHECK(Has(call_site, "audio_backlog_ms(dropped,period,bytes)"));
    CHECK(Has(call_site, "dropped %.0f ms of backlog (device stalled)"));
    CHECK(Has(call_site, "SDL_ClearAudioStream(p->stream);"));
    CHECK(call_site.find("SDL_ClearAudioStream") > call_site.find("if (dropped) {"));
    CHECK(audio.find("SDL_ClearAudioStream") == audio.rfind("SDL_ClearAudioStream"));
    // The helper resets the steering window and the cadence adjustment.
    const std::string header = ReadAll("src/audio_backlog.h");
    const std::string helper = Body(header, "static inline int audio_backlog_drop(");
    CHECK(Has(helper, "if (!audio_backlog_stale(*queued,low,bytes,frames)) return 0;"));
    CHECK(Has(helper, "*queued=0; *window_count=0; *adjust_ns=0;"));
    // Steering, silence prefill and statistics stay.
    CHECK(Has(output, "p->adjust_ns=p->window_min>low+2*(int)bytes ? (int64_t)(period/32)"));
    CHECK(Has(output, "SDL_PutAudioStreamData(p->stream,silence,"));
    CHECK(Has(output, "++p->stat_starved;"));
    CHECK(!Has(audio, "DEBUG_SAULO") && !Has(header, "DEBUG_SAULO"));
}

enum class Path { Background, OnDemand };

// The Presenter's decision with a stand-in for StartBackgroundWarmUp.
Path WithBinaries(const char* env, bool background_available, int* background_calls) {
    const auto startup = Osl::ChoosePipelineStartup(env, [&] {
        ++*background_calls;
        return background_available;
    });
    return startup == Osl::PipelineStartup::Background ? Path::Background : Path::OnDemand;
}

void TestPipelineStartup() {
    int calls = 0;
    CHECK(WithBinaries(nullptr, true, &calls) == Path::Background && calls == 1);
    calls = 0;
    CHECK(WithBinaries("", true, &calls) == Path::Background && calls == 1);
    calls = 0;
    CHECK(WithBinaries("0", true, &calls) == Path::Background && calls == 1);
    for (const char* value : {"1", "2", "true", "yes", "on", "00", "0x", " ", "-1", "anything"}) {
        calls = 0;
        CHECK(WithBinaries(value, true, &calls) == Path::OnDemand);
        CHECK(calls == 0); // forced on-demand never starts the background warm-up
    }
    for (const char* value : {static_cast<const char*>(nullptr), "", "0", "1"}) {
        calls = 0;
        CHECK(WithBinaries(value, false, &calls) == Path::OnDemand);
    }
    calls = 0;
    CHECK(WithBinaries(nullptr, false, &calls) == Path::OnDemand && calls == 1);

    // The real getenv result is what the Presenter classifies; the process environment is restored afterwards.
#ifdef _WIN32
    const char* saved = std::getenv("OSL_PIPELINE_ON_DEMAND");
    const std::string original = saved ? saved : "";
    _putenv_s("OSL_PIPELINE_ON_DEMAND", "1");
    CHECK(Osl::PipelineOnDemandRequested(std::getenv("OSL_PIPELINE_ON_DEMAND")));
    _putenv_s("OSL_PIPELINE_ON_DEMAND", "0");
    CHECK(!Osl::PipelineOnDemandRequested(std::getenv("OSL_PIPELINE_ON_DEMAND")));
    _putenv_s("OSL_PIPELINE_ON_DEMAND", original.c_str());
#endif

    // The Presenter uses exactly this decision, only with pipeline binaries; the other path is unchanged.
    const std::string presenter = ReadAll("gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp");
    CHECK(Has(presenter, "#include \"osl_pipeline_startup.h\""));
    const std::string constructor = Body(presenter, "Presenter::Presenter(");
    const auto binaries = constructor.find("if (instance.GetPipelineBinaryGlobalKey().keySize != 0) {");
    const auto no_binaries = constructor.find("// Without pipeline binaries");
    CHECK(binaries != std::string::npos && no_binaries != std::string::npos && binaries < no_binaries);
    const std::string with_binaries = constructor.substr(binaries, no_binaries - binaries);
    CHECK(Has(with_binaries, "Osl::ChoosePipelineStartup(\n            std::getenv(\"OSL_PIPELINE_ON_DEMAND\"),"));
    CHECK(Has(with_binaries, "[&] { return rasterizer->GetPipelineCache().StartBackgroundWarmUp(); }"));
    CHECK(Has(with_binaries, "if (startup == Osl::PipelineStartup::OnDemand) {\n            rasterizer->GetPipelineCache().StartOnDemand();\n        }\n        return;"));
    const std::string without = constructor.substr(no_binaries);
    CHECK(Has(without, "if (rasterizer->GetPipelineCache().StartBackgroundWarmUp()) {\n        return;\n    }"));
    CHECK(Has(without, "OslOverlay::SetLoading(true, 0, 0);"));
    CHECK(Has(without, "rasterizer->GetPipelineCache().WarmUp("));
    CHECK(!Has(without, "OSL_PIPELINE_ON_DEMAND") && !Has(without, "StartOnDemand") && !Has(without, "ChoosePipelineStartup"));
    CHECK(!Has(presenter, "DEBUG_SAULO"));

    // The background start declines only without concurrent store reads.
    const std::string cache = ReadAll("gpu/shadps4/video_core/renderer_vulkan/vk_pipeline_serialization.cpp");
    const std::string start = Body(cache, "bool PipelineCache::StartBackgroundWarmUp() {");
    CHECK(Has(start, "if (!Storage::DataBase::Instance().ConcurrentReads()) {\n        return false;"));
}

void TestToolchain() {
    const std::string sirit = ReadAll("gpu/third_party/sirit/src/stream.h");
    CHECK(Has(sirit, "#include <cstdlib>") && Has(sirit, "std::abort();"));
    const std::string install = ReadAll("launcher/game_install.cpp");
    CHECK(Has(install, "#include <algorithm>") && Has(install, "std::stable_partition("));
    const std::string presenter = ReadAll("gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp");
    CHECK(Has(presenter, "#include <cstdlib>") && Has(presenter, "std::getenv("));
    const std::string services = ReadAll("src/runtime_services.c");
    CHECK(!Has(services, "common_initialized"));
    CHECK(Has(services, "static ABI int32_t common_init(void) { return 0; }"));
    CHECK(Has(services, "{\"sceCommonDialogInitialize\",common_init}"));
}

} // namespace

int main(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    const auto run = [&](const char* flag, const char* title, void (*test)()) {
        if (!only.empty() && only != flag) {
            return;
        }
        const int checks = g_checks, failures = g_failures;
        test();
        std::printf("Runtime stability: %s: %d checks, %d failures\n", title, g_checks - checks, g_failures - failures);
    };
    run("--audio", "audio backlog cap", [] {
        TestBacklogCap();
        TestBacklogReset();
        TestAudioSource();
    });
    run("--pipeline", "pipeline startup", TestPipelineStartup);
    run("--toolchain", "current toolchain", TestToolchain);
    std::printf("runtime stability tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures || g_checks == 0 ? 1 : 0;
}

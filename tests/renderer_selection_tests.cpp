// Renderer selection tests: the Renderer setting, Auto's fallback and the OpenGL requirements. Pure logic plus the
// sources that must agree with it: no window, no OpenGL or Vulkan context, no game files.
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>

#include "osl_renderer.h"
#include "osl_vulkan_support.h"
#include "video_core/renderer_opengl/gl_capabilities.h"
#include "video_core/renderer_opengl/gl_primitives.h"

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

void TestParse() {
    using namespace OslRenderer;
    CHECK(ParseChoice("vulkan") == Vulkan);
    CHECK(ParseChoice("auto") == Auto);
    CHECK(ParseChoice("opengl") == OslRenderer::OpenGL);
    CHECK(ParseChoice("") == Vulkan);
    CHECK(ParseChoice("OpenGL") == Vulkan);
    CHECK(ParseChoice("direct3d") == Vulkan);
    CHECK(ParseChoice("unknown") == Vulkan);
    CHECK(Vulkan == 0);
    CHECK(ChoiceKeys[Vulkan] == "vulkan" && ChoiceKeys[Auto] == "auto" && ChoiceKeys[OslRenderer::OpenGL] == "opengl" &&
          ChoiceKeys[Direct3D11] == "d3d11");
}

void TestBackendNames() {
    using namespace OslRenderer;
    CHECK(std::string(BackendName(Backend::Vulkan)) == "Vulkan");
    CHECK(std::string(BackendName(Backend::OpenGL)) == "OpenGL");
    CHECK(std::string(BackendName(Backend::Direct3D11)) == "Direct3D 11 via ANGLE");
}

void TestResolve() {
    using namespace OslRenderer;
    CHECK(Resolve(Vulkan, true, "").backend == Backend::Vulkan);
    CHECK(Resolve(OslRenderer::OpenGL, true, "").backend == Backend::OpenGL);
    CHECK(Resolve(OslRenderer::OpenGL, true, "").fallback_reason.empty());
    CHECK(Resolve(Auto, true, "").backend == Backend::Vulkan);
    CHECK(Resolve(Auto, true, "").fallback_reason.empty());

    const auto fallback = Resolve(Auto, false, "No Vulkan graphics driver was found.");
    CHECK(fallback.backend == Backend::OpenGL);
    CHECK(fallback.fallback_reason == "No Vulkan graphics driver was found.");

    // A renderer chosen by name is never replaced, working or not.
    const auto explicit_vulkan = Resolve(Vulkan, false, "No Vulkan graphics driver was found.");
    CHECK(explicit_vulkan.backend == Backend::Vulkan);
    CHECK(explicit_vulkan.fallback_reason.empty());
    CHECK(Resolve(OslRenderer::OpenGL, false, "anything").backend == Backend::OpenGL);
    CHECK(Resolve(OslRenderer::OpenGL, false, "anything").fallback_reason.empty());
}

void TestDirect3D11() {
    using namespace OslRenderer;
    CHECK(ParseChoice("d3d11", true) == Direct3D11);
    CHECK(ParseChoice("d3d11", false) == Vulkan);
    CHECK(ParseChoice("D3D11", true) == Vulkan);
    CHECK(ParseChoice("direct3d11", true) == Vulkan);
#ifdef _WIN32
    CHECK(Direct3D11Available);
    CHECK(ParseChoice("d3d11") == Direct3D11);
#else
    CHECK(!Direct3D11Available);
    CHECK(ParseChoice("d3d11") == Vulkan);
#endif

    // Chosen by name it starts or fails as itself, and Auto never picks it.
    const auto named = Resolve(Direct3D11, false, "No Vulkan graphics driver was found.");
    CHECK(named.backend == Backend::Direct3D11);
    CHECK(named.fallback_reason.empty());
    CHECK(Resolve(Direct3D11, true, "").backend == Backend::Direct3D11);
    CHECK(Resolve(Auto, false, "anything").backend == Backend::OpenGL);
    CHECK(Resolve(Auto, true, "").backend == Backend::Vulkan);
    for (const bool usable : {true, false}) {
        CHECK(Resolve(Auto, usable, "reason").backend != Backend::Direct3D11);
    }
}

void TestActiveRenderer() {
    using namespace OslRenderer;
    SetActive(Backend::OpenGL, "No Vulkan graphics driver was found.");
    CHECK(Active() == Backend::OpenGL);
    CHECK(FallbackReason() == "No Vulkan graphics driver was found.");
    const std::string failure = FailureText("no OpenGL 3.3 core context");
    CHECK(failure.find("no OpenGL 3.3 core context") != std::string::npos);
    CHECK(failure.find("No Vulkan graphics driver was found.") != std::string::npos);
    SetActive(Backend::OpenGL);
    CHECK(FailureText("no OpenGL 3.3 core context") == "no OpenGL 3.3 core context");
    SetActive(Backend::Vulkan);
    CHECK(Active() == Backend::Vulkan);
}

void TestDirect3D11Windows() {
    const std::string settings = ReadAll("launcher/settings.h");
    const size_t entry = settings.find("{OslRenderer::Direct3D11, \"Direct3D 11 (experimental)\"}");
    CHECK(entry != std::string::npos);
    CHECK(settings.rfind("#ifdef _WIN32", entry) != std::string::npos);
    CHECK(settings.find("#endif", entry) != std::string::npos);

    const std::string angle = ReadAll("gpu/shadps4/video_core/renderer_opengl/gl_angle.cpp");
    CHECK(angle.find("SDL_HINT_OPENGL_ES_DRIVER") != std::string::npos);
    CHECK(angle.find("SDL_HINT_VIDEO_EGL_ALLOW_GETDISPLAY_FALLBACK, \"0\"") != std::string::npos);
    CHECK(angle.find("PlatformAngleTypeD3D11") != std::string::npos);
    CHECK(angle.find("#ifdef _WIN32") != std::string::npos);
    CHECK(angle.find("LoadBesideExecutable(L\"libGLESv2.dll\")") != std::string::npos);
    CHECK(angle.find("LoadBesideExecutable(L\"libEGL.dll\")") != std::string::npos);

    // Native OpenGL keeps its own window path and never asks for ANGLE.
    const std::string window = ReadAll("gpu/shim/window.cpp");
    const size_t native = window.find("if (api == GraphicsApi::OpenGL) {");
    const size_t core = window.find("SDL_GL_CONTEXT_PROFILE_CORE", native);
    CHECK(native != std::string::npos && core != std::string::npos);
    CHECK(window.find("SDL_GL_CONTEXT_PROFILE_ES") == std::string::npos);
    CHECK(window.find("RequestAngleDirect3D11") < native);
    CHECK(ReadAll("gpu/angle-runtime.txt").find("clang64/bin/libGLESv2.dll") != std::string::npos);
}

void TestWindowApi() {
    const std::string header = ReadAll("gpu/shim/sdl_window.h");
    CHECK(header.find("enum class GraphicsApi : u8 { Vulkan, OpenGL, Angle };") != std::string::npos);
    CHECK(header.find("case OslRenderer::Backend::OpenGL:\n        return GraphicsApi::OpenGL;") != std::string::npos ||
          header.find("case OslRenderer::Backend::OpenGL:\r\n        return GraphicsApi::OpenGL;") != std::string::npos);
    CHECK(header.find("return GraphicsApi::Angle;") != std::string::npos);
    CHECK(header.find("return GraphicsApi::Vulkan;") != std::string::npos);

    // The renderer is chosen before the window exists, and the window is built for it once.
    const std::string glue = ReadAll("gpu/shim/oslgpu.cpp");
    const size_t select = glue.find("    SelectRenderer();");
    const size_t window = glue.find("new Frontend::WindowSDL(");
    CHECK(select != std::string::npos && window != std::string::npos && select < window);
    CHECK(glue.find("Frontend::GraphicsApiFor(OslRenderer::Active())") != std::string::npos);
    CHECK(glue.find("Renderer: selected %s, active %s") != std::string::npos);
    CHECK(glue.find("Renderer: Auto fell back to OpenGL: %s") != std::string::npos);
}

// Non-indexed rectangles keep the attribute path that was validated at run time.
void TestRectPathsStaySeparate() {
    using OpenGL::RectListUsesTables;
    CHECK(!RectListUsesTables(true, false, true));
    CHECK(!RectListUsesTables(true, false, false));
    CHECK(!RectListUsesTables(true, true, false));
    CHECK(!RectListUsesTables(false, true, true));
    CHECK(RectListUsesTables(true, true, true));
}

void TestLauncherAgrees() {
    const std::string settings = ReadAll("launcher/settings.h");
    CHECK(settings.find("#include \"osl_renderer.h\"") != std::string::npos);
    CHECK(settings.find("kRendererKey[] = \"renderer\"") != std::string::npos);
    CHECK(settings.find("kDefaultRenderer[] = \"vulkan\"") != std::string::npos);
    CHECK(settings.find("\"OpenGL (experimental)\"") != std::string::npos);
    CHECK(settings.find("{OslRenderer::Auto, \"Auto\"}") != std::string::npos);
    CHECK(settings.find("{OslRenderer::Vulkan, \"Vulkan\"}") != std::string::npos);
    // The launcher spells no renderer key itself: the choices are named by the shared enum.
    for (const auto key : OslRenderer::ChoiceKeys) {
        CHECK(settings.find("{\"" + std::string(key) + "\", ") == std::string::npos);
    }

    const std::string shim = ReadAll("gpu/shim/osl_settings.cpp");
    CHECK(shim.find("key == \"renderer\"") != std::string::npos);
    CHECK(shim.find("OslRenderer::ParseChoice(value)") != std::string::npos);

    const std::string launcher = ReadAll("launcher/launcher.cpp");
    const auto pageBody = [&](const char* signature, const char* next) {
        const size_t begin = launcher.find(signature);
        const size_t end = launcher.find(next, begin);
        return begin == std::string::npos || end == std::string::npos ? std::string{} : launcher.substr(begin, end - begin);
    };
    const std::string display = pageBody("void PageDisplay(trg::Ui& ui) {", "void WindowSizeCombo()");
    const std::string graphics = pageBody("void PageGraphics(trg::Ui& ui) {", "void PageGameplay(trg::Ui& ui) {");
    CHECK(!display.empty() && !graphics.empty());
    const std::string row = "ui.Combo(\"Renderer\", kRendererHelp, kRendererKey, kDefaultRenderer, renderers);";
    const size_t renderer = display.find(row);
    const size_t card = display.find("ui.Row(\"Graphics card\"");
    const size_t aspect = display.find("ui.Combo(\"Aspect Ratio\"");
    const size_t vsync = display.find("ui.Toggle(\"VSync\"");
    CHECK(renderer != std::string::npos && card != std::string::npos && aspect < renderer && renderer < card && card < vsync);
    CHECK(display.find(row, renderer + 1) == std::string::npos);
    CHECK(display.substr(renderer + row.size(), card - renderer - row.size()).find("ui.") == std::string::npos);
    CHECK(graphics.find("\"Renderer\"") == std::string::npos && graphics.find("kRendererKey") == std::string::npos);
    const size_t resolution = graphics.find("ui.Combo(\"Render resolution\"");
    const size_t smaa = graphics.find("ui.Toggle(\"Anti-aliasing\"");
    const size_t filtering = graphics.find("ui.Combo(\"Texture filtering\"");
    CHECK(resolution != std::string::npos && resolution < smaa && smaa < filtering && filtering != std::string::npos);
    CHECK(launcher.find("ui.Combo(\"Renderer\"") == launcher.rfind("ui.Combo(\"Renderer\""));
    CHECK(launcher.find("It takes effect the next time the game starts.") != std::string::npos);
}

// The game code stays out of renderer choices: only the factory and the window know both.
void TestBoundary() {
    const std::string liverpool = ReadAll("gpu/shadps4/video_core/amdgpu/liverpool.cpp");
    CHECK(liverpool.find("Vulkan::Rasterizer") == std::string::npos);
    CHECK(liverpool.find("OpenGL") == std::string::npos);
    const std::string driver = ReadAll("gpu/shadps4/core/libraries/videoout/driver.cpp");
    CHECK(driver.find("Vulkan::Presenter") == std::string::npos);
    CHECK(driver.find("OpenGL") == std::string::npos);
    const std::string factory = ReadAll("gpu/shadps4/video_core/renderer_factory.cpp");
    CHECK(factory.find("make_unique<Vulkan::Presenter>") != std::string::npos);
    CHECK(factory.find("make_unique<OpenGL::Presenter>") != std::string::npos);
    CHECK(factory.find("Active() == OslRenderer::Backend::Vulkan") != std::string::npos);
    const std::string loader = ReadAll("src/loader.c");
    CHECK(loader.find("osl_check_renderer(renderer_settings_file") != std::string::npos);
}

void TestRequirements() {
    OpenGL::Capabilities caps;
    caps.major = 3;
    caps.minor = 3;
    caps.s3tc = true;
    caps.max_texture_buffer_texels = OpenGL::MinimumRingTexels;
    CHECK(OpenGL::MissingRequirement(caps).empty());

    auto old_context = caps;
    old_context.minor = 2;
    CHECK(OpenGL::MissingRequirement(old_context).find("3.3") != std::string::npos);
    auto no_s3tc = caps;
    no_s3tc.s3tc = false;
    CHECK(OpenGL::MissingRequirement(no_s3tc).find("s3tc") != std::string::npos);
    auto small_buffers = caps;
    small_buffers.max_texture_buffer_texels = 65536;
    CHECK(OpenGL::MissingRequirement(small_buffers).find("texture buffer") != std::string::npos);

    auto newer = caps;
    newer.major = 4;
    newer.minor = 0;
    CHECK(OpenGL::MissingRequirement(newer).empty());
}

void TestGlesRequirements() {
    OpenGL::Capabilities caps;
    caps.gles = true;
    caps.major = 3;
    caps.s3tc = true;
    caps.draw_buffers_blend = true;
    caps.max_texture_size = OpenGL::MinimumGlesTextureSize;
    CHECK(OpenGL::MissingRequirement(caps).empty());

    auto level_10_0 = caps;
    level_10_0.major = 2;
    CHECK(OpenGL::MissingRequirement(level_10_0).find("10_1") != std::string::npos);
    auto no_s3tc = caps;
    no_s3tc.s3tc = false;
    CHECK(OpenGL::MissingRequirement(no_s3tc).find("s3tc") != std::string::npos);
    auto no_blend = caps;
    no_blend.draw_buffers_blend = false;
    CHECK(OpenGL::MissingRequirement(no_blend).find("draw_buffers_indexed") != std::string::npos);
    auto small = caps;
    small.max_texture_size = 2048;
    CHECK(OpenGL::MissingRequirement(small).find("4096") != std::string::npos);

    using OpenGL::IsAngleDirect3D11;
    CHECK(IsAngleDirect3D11("ANGLE (NVIDIA, NVIDIA GeForce RTX 4070 (0x00002786) Direct3D11 vs_5_0 ps_5_0, D3D11-32.0.15)"));
    CHECK(!IsAngleDirect3D11("ANGLE (NVIDIA, NVIDIA GeForce RTX 4070 Direct3D9Ex vs_3_0 ps_3_0)"));
    CHECK(!IsAngleDirect3D11("ANGLE (NVIDIA, Vulkan 1.3.280, NVIDIA GeForce RTX 4070)"));
    CHECK(!IsAngleDirect3D11("NVIDIA GeForce RTX 4070/PCIe/SSE2"));
}

// Vulkan decisions without a loader or a device: each step the probe may take is counted, so a test sees which ran.
struct FakeVulkan {
    bool loader = true;
    std::uint32_t version = OslVulkan::Version(1, 3);
    OslVulkan::DeviceProbe devices{OslVulkan::InstanceStatus::Created, {}, 1, 1};
    int versions_asked = 0;
    int instances_made = 0;

    std::optional<OslVulkan::Problem> Find() {
        const OslVulkan::Probe probe{[this] { return loader; },
                                     [this] {
                                         ++versions_asked;
                                         return version;
                                     },
                                     [this] {
                                         ++instances_made;
                                         return devices;
                                     }};
        return OslVulkan::FindProblem(probe, OslVulkan::Version(1, 3));
    }
};

void TestVulkanVersions() {
    using namespace OslVulkan;
    CHECK(Major(Version(1, 3)) == 1 && Minor(Version(1, 3)) == 3);
    CHECK(Version(1, 3) == (1u << 22 | 3u << 12));
    for (const std::uint32_t minor : {0u, 1u, 2u}) {
        FakeVulkan vulkan;
        vulkan.version = Version(1, minor);
        const auto problem = vulkan.Find();
        CHECK(problem.has_value());
        CHECK(problem && problem->cause.find("supports only 1." + std::to_string(minor)) != std::string::npos);
        CHECK(problem && problem->cause.find("Vulkan 1.3 or newer is required") != std::string::npos);
        CHECK(problem && problem->advice.find("another renderer") != std::string::npos);
        CHECK(vulkan.instances_made == 0);
    }
    for (const std::uint32_t minor : {3u, 4u}) {
        FakeVulkan vulkan;
        vulkan.version = Version(1, minor);
        CHECK(!vulkan.Find().has_value());
        CHECK(vulkan.instances_made == 1);
    }
    FakeVulkan newer_major;
    newer_major.version = Version(2, 0);
    CHECK(!newer_major.Find().has_value());
}

void TestVulkanProbeOutcomes() {
    using namespace OslVulkan;
    FakeVulkan no_loader;
    no_loader.loader = false;
    const auto missing = no_loader.Find();
    CHECK(missing && missing->cause.find("not installed") != std::string::npos);
    CHECK(no_loader.versions_asked == 0 && no_loader.instances_made == 0);

    FakeVulkan incompatible;
    incompatible.devices.status = InstanceStatus::IncompatibleDriver;
    CHECK(incompatible.Find().value().cause.find("No Vulkan graphics driver") != std::string::npos);

    FakeVulkan failed;
    failed.devices.status = InstanceStatus::Failed;
    failed.devices.failure = "ErrorInitializationFailed";
    CHECK(failed.Find().value().cause.find("ErrorInitializationFailed") != std::string::npos);

    FakeVulkan none;
    none.devices.devices = 0;
    none.devices.usable = 0;
    CHECK(none.Find().value().cause.find("No Vulkan graphics driver") != std::string::npos);

    FakeVulkan unusable;
    unusable.devices.devices = 2;
    unusable.devices.usable = 0;
    CHECK(unusable.Find().value().cause.find("features the game needs") != std::string::npos);

    FakeVulkan one_of_two;
    one_of_two.devices.devices = 2;
    one_of_two.devices.usable = 1;
    CHECK(!one_of_two.Find().has_value());
}

// Without Vulkan, only Vulkan chosen by name stops Play; OpenGL, Direct3D 11 and Auto go on.
void TestVulkanDoesNotGateOtherRenderers() {
    using namespace OslRenderer;
    CHECK(!PlayAllowed(Vulkan, false));
    CHECK(PlayAllowed(Vulkan, true));
    for (const Choice choice : {OslRenderer::OpenGL, Direct3D11, Auto}) {
        CHECK(PlayAllowed(choice, false));
        CHECK(PlayAllowed(choice, true));
    }
    CHECK(Resolve(OslRenderer::OpenGL, false, "No Vulkan graphics driver was found.").backend == Backend::OpenGL);
    CHECK(Resolve(Direct3D11, false, "No Vulkan graphics driver was found.").backend == Backend::Direct3D11);
    CHECK(Resolve(Auto, false, "No Vulkan graphics driver was found.").backend == Backend::OpenGL);

    const std::string list = ReadAll("gpu/shim/osl_gpu_list.cpp");
    const size_t check = list.find("extern \"C\" int osl_check_renderer");
    const size_t gpu_names = list.find("extern \"C\" int osl_gpu_names");
    CHECK(check != std::string::npos && check < gpu_names);
    const std::string renderer = list.substr(check, gpu_names - check);
    CHECK(renderer.find("choice == OslRenderer::OpenGL || choice == OslRenderer::Direct3D11") != std::string::npos);
    CHECK(renderer.find("OslRenderer::PlayAllowed(choice, vulkan_usable)") != std::string::npos);
}

// Issue #23: on Windows the Graphics card list went straight to CreateInstance, whose assertion ended the launcher on a
// Vulkan 1.0 runtime.
void TestIssue23VulkanOneNeverAsserts() {
    FakeVulkan vulkan;
    vulkan.version = OslVulkan::Version(1, 0);
    const auto problem = vulkan.Find();
    CHECK(problem && problem->cause.find("supports only 1.0") != std::string::npos);
    CHECK(vulkan.instances_made == 0);

    const std::string list = ReadAll("gpu/shim/osl_gpu_list.cpp");
    CHECK(list.find("_WIN32") == std::string::npos);
    const size_t names = list.find("extern \"C\" int osl_gpu_names");
    CHECK(names != std::string::npos);
    const std::string body = list.substr(names);
    const size_t guard = body.find("if (CheckVulkan())");
    const size_t instance = body.find("CreateProbeInstance()");
    CHECK(guard != std::string::npos && instance != std::string::npos && guard < instance);
    CHECK(list.find("CreateInstance(") == std::string::npos);
    CHECK(list.find("OslVulkan::FindProblem(probe, TargetVulkanApiVersion)") != std::string::npos);
    CHECK(ReadAll("gpu/shadps4/video_core/renderer_vulkan/vk_platform.h")
              .find("constexpr u32 TargetVulkanApiVersion = VK_API_VERSION_1_3;") != std::string::npos);

    const std::string loader = ReadAll("src/loader.c");
    const size_t launcher = loader.find("OslLauncherConfig launcher=");
    CHECK(launcher != std::string::npos && loader.find("system_usable,log_path", launcher) != std::string::npos);
    const size_t usable = loader.find("static int system_usable");
    const size_t cpu = loader.find("static int cpu_usable");
    CHECK(cpu != std::string::npos && usable != std::string::npos && cpu < usable);
    CHECK(loader.substr(cpu, usable - cpu).find("#ifndef _WIN32") == std::string::npos);
}

// The CPU fallbacks in the issue's log (LZCNT, MOVBE, BMI1) take no part in the Vulkan decision.
void TestIssue23CpuFallbacksAreSeparate() {
    const std::string support = ReadAll("gpu/shim/osl_vulkan_support.h");
    CHECK(support.find("isa") == std::string::npos && support.find("LZCNT") == std::string::npos);
    const std::string list = ReadAll("gpu/shim/osl_gpu_list.cpp");
    CHECK(list.find("isa_") == std::string::npos && list.find("cpu_") == std::string::npos);
}

// Missing game files open the launcher; the renderer starts only after it returned, and Play checks graphics only
// when the files are ready. The launcher's "direct3d11 renderer" is SDL's own drawing backend.
void TestMissingGameFilesStartNoRenderer() {
    const std::string loader = ReadAll("src/loader.c");
    const size_t launcher = loader.find("osl_launcher_run(&launcher)");
    const size_t renderer = loader.find("oslgpu_init(&gpu)");
    CHECK(launcher != std::string::npos && renderer != std::string::npos && launcher < renderer);
    const std::string window = ReadAll("launcher/launcher.cpp");
    CHECK(window.find("files_ == GameFileState::kValid && !GraphicsUsable()") != std::string::npos);
    CHECK(window.find("oslgpu_init") == std::string::npos);
    CHECK(ReadAll("launcher/host.cpp").find("SDL_GetRendererName(renderer_)") != std::string::npos);
}

} // namespace

int main() {
    TestParse();
    TestBackendNames();
    TestResolve();
    TestDirect3D11();
    TestActiveRenderer();
    TestDirect3D11Windows();
    TestWindowApi();
    TestRectPathsStaySeparate();
    TestGlesRequirements();
    TestLauncherAgrees();
    TestBoundary();
    TestRequirements();
    TestVulkanVersions();
    TestVulkanProbeOutcomes();
    TestVulkanDoesNotGateOtherRenderers();
    TestIssue23VulkanOneNeverAsserts();
    TestIssue23CpuFallbacksAreSeparate();
    TestMissingGameFilesStartNoRenderer();
    std::printf("renderer selection tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

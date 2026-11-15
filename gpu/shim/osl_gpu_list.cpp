// SPDX-License-Identifier: GPL-2.0-or-later
// The launcher's list of Vulkan devices the renderer can use, best first (the Graphics card row), and the check that
// Vulkan can run the renderer at all. Neither may end the process on a system without usable Vulkan.

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "common/types.h"
#include "osl_graphics.h"
#include "osl_renderer.h"
#include "osl_settings.h"
#include "osl_vulkan_support.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"

namespace {

// The renderer's own requirements, asked without its assertions, which end the process with no word to the player.
std::optional<OslVulkan::Problem> FindVulkanProblem() {
    using namespace Vulkan;
    static std::unique_ptr<vk::detail::DynamicLoader> loader;
    const OslVulkan::Probe probe{
        [] {
            if (!loader || !loader->success()) {
                loader = std::make_unique<vk::detail::DynamicLoader>();
            }
            if (loader->success()) {
                VULKAN_HPP_DEFAULT_DISPATCHER.init(
                    loader->getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
            }
            return loader->success();
        },
        [] {
            if (!VULKAN_HPP_DEFAULT_DISPATCHER.vkEnumerateInstanceVersion) {
                return u32{VK_API_VERSION_1_0};
            }
            const auto [result, value] = vk::enumerateInstanceVersion();
            return result == vk::Result::eSuccess ? value : u32{VK_API_VERSION_1_0};
        },
        [] {
            OslVulkan::DeviceProbe probed;
            auto [result, instance] = CreateProbeInstance();
            if (result == vk::Result::eErrorIncompatibleDriver) {
                probed.status = OslVulkan::InstanceStatus::IncompatibleDriver;
            } else if (result != vk::Result::eSuccess) {
                probed.status = OslVulkan::InstanceStatus::Failed;
                probed.failure = vk::to_string(result);
            } else {
                const auto [devices_result, devices] = instance->enumeratePhysicalDevices();
                if (devices_result == vk::Result::eSuccess) {
                    probed.devices = devices.size();
                    probed.usable = static_cast<std::size_t>(std::ranges::count_if(devices, IsDeviceUsable));
                }
            }
            return probed;
        }};
    return OslVulkan::FindProblem(probe, TargetVulkanApiVersion);
}

// A success is remembered; a failure is asked again, as the player may install a driver and press Play again.
std::optional<OslVulkan::Problem> CheckVulkan() {
    static bool usable = false;
    if (usable) {
        return std::nullopt;
    }
    auto problem = FindVulkanProblem();
    usable = !problem;
    return problem;
}

} // namespace

extern "C" int osl_check_vulkan(char* cause, size_t cause_size, char* advice, size_t advice_size) {
    const auto problem = CheckVulkan();
    if (!problem) {
        return 1;
    }
    std::printf("Graphics: %s %s\n", problem->cause.c_str(), problem->advice.c_str());
    std::snprintf(cause, cause_size, "%s", problem->cause.c_str());
    std::snprintf(advice, advice_size, "%s", problem->advice.c_str());
    return 0;
}

// Vulkan stops the start only when the player's Renderer setting names it.
extern "C" int osl_check_renderer(const char* settings_file, char* cause, size_t cause_size,
                                  char* advice, size_t advice_size) {
    OslSettings::Values values;
    OslSettings::Read(settings_file ? settings_file : "", values);
    const auto choice = OslRenderer::Choice(values.renderer.load());
    if (choice == OslRenderer::OpenGL || choice == OslRenderer::Direct3D11) {
        return 1;
    }
    const bool vulkan_usable = osl_check_vulkan(cause, cause_size, advice, advice_size) != 0;
    return OslRenderer::PlayAllowed(choice, vulkan_usable);
}

extern "C" int osl_gpu_names(char (*names)[256], int max) {
    using namespace Vulkan;
    if (CheckVulkan()) {
        return 0;
    }
    std::vector<std::string> listed;
    {
        auto [created, instance] = CreateProbeInstance();
        if (created != vk::Result::eSuccess) {
            return 0;
        }
        const auto [result, devices] = instance->enumeratePhysicalDevices();
        if (result != vk::Result::eSuccess) {
            return 0;
        }
        std::vector<std::string> device_names;
        for (const auto& device : devices) {
            device_names.push_back(IsDeviceUsable(device) ? std::string(device.getProperties().deviceName.data())
                                                          : std::string());
        }
        listed = OslGraphics::ListGpus(device_names, RankPhysicalDevices(devices));
    }
    int count = 0;
    for (const std::string& name : listed) {
        if (count == max) {
            break;
        }
        std::snprintf(names[count++], 256, "%s", name.c_str());
    }
    return count;
}

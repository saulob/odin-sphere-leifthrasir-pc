// SPDX-License-Identifier: GPL-2.0-or-later
// Whether Vulkan can run the renderer, decided from facts a probe gathers step by step. A step runs only once the
// earlier ones passed, so an unsupported system gets an explanation instead of a fatal assertion.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace OslVulkan {

constexpr std::uint32_t Version(std::uint32_t major, std::uint32_t minor) {
    return (major << 22) | (minor << 12);
}
constexpr std::uint32_t Major(std::uint32_t version) {
    return (version >> 22) & 0x7f;
}
constexpr std::uint32_t Minor(std::uint32_t version) {
    return (version >> 12) & 0x3ff;
}

struct Problem {
    std::string cause;
    std::string advice;
};

enum class InstanceStatus { Created, IncompatibleDriver, Failed };

struct DeviceProbe {
    InstanceStatus status = InstanceStatus::Created;
    std::string failure; ///< what the instance reported when status is Failed
    std::size_t devices = 0;
    std::size_t usable = 0;
};

struct Probe {
    std::function<bool()> loader_found;
    std::function<std::uint32_t()> instance_version; ///< 1.0 when the loader cannot report a version
    std::function<DeviceProbe()> probe_devices;      ///< makes a bare instance and looks at its devices
};

inline std::optional<Problem> FindProblem(const Probe& probe, std::uint32_t required) {
    const std::string required_text = std::to_string(Major(required)) + "." + std::to_string(Minor(required));
    const std::string advice = "Update your graphics driver, or choose another renderer.";
    if (!probe.loader_found()) {
        return Problem{"Vulkan is not installed on this system.",
                       "Install your graphics driver's Vulkan support, or choose another renderer."};
    }
    const std::uint32_t version = probe.instance_version();
    if (version < required) {
        return Problem{"This system's Vulkan runtime supports only " + std::to_string(Major(version)) + "." +
                           std::to_string(Minor(version)) + ", but Vulkan " + required_text + " or newer is required.",
                       advice};
    }
    const Problem no_driver{"No Vulkan graphics driver was found.", advice};
    const DeviceProbe devices = probe.probe_devices();
    if (devices.status == InstanceStatus::IncompatibleDriver) {
        return no_driver;
    }
    if (devices.status == InstanceStatus::Failed) {
        return Problem{"Vulkan could not be started (" + devices.failure + ").", advice};
    }
    if (devices.devices == 0) {
        return no_driver;
    }
    if (devices.usable == 0) {
        return Problem{"No graphics card here supports Vulkan " + required_text + " with the features the game needs.",
                       advice};
    }
    return std::nullopt;
}

} // namespace OslVulkan

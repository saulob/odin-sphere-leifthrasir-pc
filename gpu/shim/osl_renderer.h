// SPDX-License-Identifier: GPL-2.0-or-later
// Which renderer runs the game: the Renderer setting as osl.ini stores it, and how it resolves.
#pragma once

#include <array>
#include <atomic>
#include <string>
#include <string_view>

namespace OslRenderer {

#ifdef _WIN32
inline constexpr bool Direct3D11Available = true;
#else
inline constexpr bool Direct3D11Available = false;
#endif

enum Choice : int { Vulkan = 0, Auto = 1, OpenGL = 2, Direct3D11 = 3, ChoiceCount };
inline constexpr std::array<std::string_view, ChoiceCount> ChoiceKeys = {"vulkan", "auto", "opengl", "d3d11"};

/// An unknown value keeps the renderer that has always run the game; so does Direct3D 11 where it does not exist.
inline Choice ParseChoice(std::string_view text, bool direct3d11_available = Direct3D11Available) {
    for (std::size_t i = 0; i < ChoiceKeys.size(); ++i) {
        if (ChoiceKeys[i] == text) {
            return Choice(i) == Direct3D11 && !direct3d11_available ? Vulkan : Choice(i);
        }
    }
    return Vulkan;
}

enum class Backend { Vulkan, OpenGL, Direct3D11 };

inline const char* BackendName(Backend backend) {
    switch (backend) {
    case Backend::OpenGL:
        return "OpenGL";
    case Backend::Direct3D11:
        return "Direct3D 11 via ANGLE";
    default:
        return "Vulkan";
    }
}

struct Resolution {
    Backend backend;
    std::string fallback_reason; ///< empty unless Auto left Vulkan
};

/// Only Auto falls back: a renderer chosen by name starts or fails as itself.
inline Resolution Resolve(Choice choice, bool vulkan_usable, std::string_view vulkan_problem) {
    if (choice == OpenGL) {
        return {Backend::OpenGL, {}};
    }
    if (choice == Direct3D11) {
        return {Backend::Direct3D11, {}};
    }
    if (choice == Auto && !vulkan_usable) {
        return {Backend::OpenGL, std::string{vulkan_problem}};
    }
    return {Backend::Vulkan, {}};
}

inline std::atomic<Backend> active_backend{Backend::Vulkan};

inline std::string& FallbackReasonStorage() {
    static std::string reason;
    return reason;
}

inline Backend Active() {
    return active_backend.load(std::memory_order_relaxed);
}

/// Once, before the window and the renderer start.
inline void SetActive(Backend backend, std::string_view fallback_reason = {}) {
    FallbackReasonStorage() = std::string{fallback_reason};
    active_backend.store(backend, std::memory_order_relaxed);
}

/// Why Auto left Vulkan; empty when it did not.
inline const std::string& FallbackReason() {
    return FallbackReasonStorage();
}

/// A startup failure's text, with Auto's Vulkan reason when the failing OpenGL was Auto's fallback.
inline std::string FailureText(std::string_view what) {
    std::string text{what};
    if (!FallbackReason().empty()) {
        text += "\n\nAuto chose OpenGL because Vulkan could not run: " + FallbackReason();
    }
    return text;
}

} // namespace OslRenderer

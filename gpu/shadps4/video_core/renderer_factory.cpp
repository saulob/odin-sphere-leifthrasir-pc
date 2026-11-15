// SPDX-License-Identifier: GPL-2.0-or-later

#include "osl_renderer.h"
#include "video_core/renderer_backend.h"
#include "video_core/renderer_opengl/gl_presenter.h"
#include "video_core/renderer_vulkan/vk_presenter.h"

namespace VideoCore {

std::unique_ptr<PresenterBackend> CreatePresenter(Frontend::WindowSDL& window,
                                                  AmdGpu::Liverpool* liverpool) {
    if (OslRenderer::Active() == OslRenderer::Backend::Vulkan) {
        return std::make_unique<Vulkan::Presenter>(window, liverpool);
    }
    return std::make_unique<OpenGL::Presenter>(window, liverpool);
}

} // namespace VideoCore

#pragma once

#include <cstring>

namespace Osl {

enum class PipelineStartup { Background, OnDemand };

inline bool PipelineOnDemandRequested(const char* env_value) {
    return env_value && *env_value && std::strcmp(env_value, "0") != 0;
}

// `start_background` runs only when on-demand was not requested and returns whether it started.
template <typename StartBackground>
PipelineStartup ChoosePipelineStartup(const char* env_value, StartBackground&& start_background) {
    if (PipelineOnDemandRequested(env_value) || !start_background()) {
        return PipelineStartup::OnDemand;
    }
    return PipelineStartup::Background;
}

} // namespace Osl

// SPDX-License-Identifier: MIT
// SMAA pixel stages (smaa_common.glsl). Bindings: 0 color (edge detection, blending) or
// edges (weights); 1 area texture or blending weights; 2 search texture.
#version 450
#extension GL_GOOGLE_include_directive : require

#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#include "smaa_common.glsl"
#include "smaa/SMAA.hlsl"

layout (location = 0) in vec2 texcoord;
layout (location = 1) in vec2 pixcoord;
layout (location = 2) in vec4 offset[3];

layout (location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D tex0;
#if SMAA_PASS >= 1
layout (binding = 1) uniform sampler2D tex1;
#endif
#if SMAA_PASS == 1
layout (binding = 2) uniform sampler2D search_tex;
#endif

void main() {
#if SMAA_PASS == 0
    color = vec4(SMAALumaEdgeDetectionPS(texcoord, offset, tex0), 0.0, 0.0);
#elif SMAA_PASS == 1
    color = SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, tex0, tex1, search_tex,
                                            vec4(0.0));
#else
    color = SMAANeighborhoodBlendingPS(texcoord, offset[0], tex0, tex1);
#endif
}

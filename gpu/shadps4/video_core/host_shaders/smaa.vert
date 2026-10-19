// SPDX-License-Identifier: MIT
// SMAA vertex stage (smaa_common.glsl): a full-screen triangle and the pass's offsets.
#version 450
#extension GL_GOOGLE_include_directive : require

#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 0
#include "smaa_common.glsl"
#include "smaa/SMAA.hlsl"

layout (location = 0) out vec2 texcoord;
layout (location = 1) out vec2 pixcoord;
layout (location = 2) out vec4 offset[3];

void main() {
    const vec2 pos = vec2(float((gl_VertexIndex & 1) << 2), float((gl_VertexIndex & 2) << 1));
    gl_Position = vec4(pos - vec2(1.0), 0.0, 1.0);
    texcoord = pos * 0.5;
    pixcoord = vec2(0.0);
#if SMAA_PASS == 0
    SMAAEdgeDetectionVS(texcoord, offset);
#elif SMAA_PASS == 1
    SMAABlendingWeightCalculationVS(texcoord, pixcoord, offset);
#else
    SMAANeighborhoodBlendingVS(texcoord, offset[0]);
    offset[1] = vec4(0.0);
    offset[2] = vec4(0.0);
#endif
}

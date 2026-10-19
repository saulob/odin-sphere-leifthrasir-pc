// SPDX-License-Identifier: MIT
// SMAA 1x (smaa/SMAA.hlsl, Jimenez et al., MIT) for the presenter, one stage per define:
// SMAA_PASS 0 edge detection, 1 blending weights, 2 neighborhood blending.

layout (push_constant) uniform SmaaConstants {
    vec4 rt_metrics;   // 1/width, 1/height, width, height
    uint input_linear; // the color input is sampled through an sRGB view (linear values)
} smaa;

#define SMAA_RT_METRICS smaa.rt_metrics
#define SMAA_PRESET_HIGH 1
#define SMAA_CUSTOM_SL 1

// SMAA_GLSL_4 as SMAA.hlsl defines it, except that edge detection reads gamma-encoded color
// (SMAA's luma edges need it): an sRGB-view input is encoded on the fly.
vec4 SmaaEdgeFetch(sampler2D tex, vec2 coord) {
    vec4 c = textureLod(tex, coord, 0.0);
    if (smaa.input_linear != 0u) {
        c.rgb = sqrt(max(c.rgb, vec3(0.0))); // gamma 2.0: enough for an edge threshold
    }
    return c;
}
#define SMAATexture2D(tex) sampler2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) textureLod(tex, coord, 0.0)
#define SMAASampleLevelZeroPoint(tex, coord) textureLod(tex, coord, 0.0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) textureLodOffset(tex, coord, 0.0, offset)
#define SMAASample(tex, coord) texture(tex, coord)
#if SMAA_PASS == 0
#define SMAASamplePoint(tex, coord) SmaaEdgeFetch(tex, coord)
#else
#define SMAASamplePoint(tex, coord) texture(tex, coord)
#endif
#define SMAASampleOffset(tex, coord, offset) texture(tex, coord, offset)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define lerp(a, b, t) mix(a, b, t)
#define saturate(a) clamp(a, 0.0, 1.0)
#define mad(a, b, c) fma(a, b, c)
#define SMAAGather(tex, coord) textureGather(tex, coord)
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define bool2 bvec2
#define bool3 bvec3
#define bool4 bvec4

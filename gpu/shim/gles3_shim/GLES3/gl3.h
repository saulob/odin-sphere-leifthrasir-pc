// Stands in for <GLES3/gl3.h> in the one file that includes it: imgui's backend calls OpenGL ES by name, and these
// names go through pointers SDL resolves from ANGLE, so the executable links no OpenGL ES library.
#pragma once

#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_video.h>

// Desktop features the ES3 build must not see: the backend tests these macros, not its target.
#undef GL_CLIP_ORIGIN
#undef GL_POLYGON_MODE

#define OSL_IMGUI_GLES3_FUNCTIONS(X)                                                               \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                                       \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                           \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                                                         \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                                 \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                                                     \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate)                                     \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                                             \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                                           \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                                     \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                     \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                     \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                                       \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                                     \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                                     \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                                                   \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                                       \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                                           \
    X(PFNGLDETACHSHADERPROC, glDetachShader)                                                       \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)                               \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                 \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                           \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers)                                                         \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                                 \
    X(PFNGLGETATTRIBLOCATIONPROC, glGetAttribLocation)                                             \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                             \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                       \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                               \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                         \
    X(PFNGLGETSTRINGIPROC, glGetStringi)                                                           \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                           \
    X(PFNGLGETVERTEXATTRIBIVPROC, glGetVertexAttribiv)                                             \
    X(PFNGLGETVERTEXATTRIBPOINTERVPROC, glGetVertexAttribPointerv)                                 \
    X(PFNGLISPROGRAMPROC, glIsProgram)                                                             \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                         \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                                             \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                       \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                                             \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                                               \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                           \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

#define OSL_IMGUI_GLES3_CORE_FUNCTIONS(X)                                                          \
    X(glActiveTexture)                                                                             \
    X(glBindTexture)                                                                               \
    X(glDeleteTextures)                                                                            \
    X(glDisable)                                                                                   \
    X(glDrawElements)                                                                              \
    X(glEnable)                                                                                    \
    X(glGenTextures)                                                                               \
    X(glGetError)                                                                                  \
    X(glGetIntegerv)                                                                               \
    X(glGetString)                                                                                 \
    X(glIsEnabled)                                                                                 \
    X(glPixelStorei)                                                                               \
    X(glScissor)                                                                                   \
    X(glTexImage2D)                                                                                \
    X(glTexParameteri)                                                                             \
    X(glTexSubImage2D)                                                                             \
    X(glViewport)

#define OSL_DECLARE(type, name) static type osl_##name;
OSL_IMGUI_GLES3_FUNCTIONS(OSL_DECLARE)
#undef OSL_DECLARE
#define OSL_DECLARE(name) static decltype(&::name) osl_##name;
OSL_IMGUI_GLES3_CORE_FUNCTIONS(OSL_DECLARE)
#undef OSL_DECLARE

static bool LoadGles3ForImGui() {
#define OSL_RESOLVE(type, name)                                                                    \
    osl_##name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));                             \
    if (!osl_##name) {                                                                             \
        return false;                                                                              \
    }
#define OSL_RESOLVE_CORE(name)                                                                     \
    osl_##name = reinterpret_cast<decltype(osl_##name)>(SDL_GL_GetProcAddress(#name));             \
    if (!osl_##name) {                                                                             \
        return false;                                                                              \
    }
    OSL_IMGUI_GLES3_FUNCTIONS(OSL_RESOLVE)
    OSL_IMGUI_GLES3_CORE_FUNCTIONS(OSL_RESOLVE_CORE)
#undef OSL_RESOLVE
#undef OSL_RESOLVE_CORE
    return true;
}

#define glActiveTexture osl_glActiveTexture
#define glAttachShader osl_glAttachShader
#define glBindBuffer osl_glBindBuffer
#define glBindSampler osl_glBindSampler
#define glBindTexture osl_glBindTexture
#define glBindVertexArray osl_glBindVertexArray
#define glBlendEquation osl_glBlendEquation
#define glBlendEquationSeparate osl_glBlendEquationSeparate
#define glBlendFuncSeparate osl_glBlendFuncSeparate
#define glBufferData osl_glBufferData
#define glBufferSubData osl_glBufferSubData
#define glCompileShader osl_glCompileShader
#define glCreateProgram osl_glCreateProgram
#define glCreateShader osl_glCreateShader
#define glDeleteBuffers osl_glDeleteBuffers
#define glDeleteProgram osl_glDeleteProgram
#define glDeleteSamplers osl_glDeleteSamplers
#define glDeleteShader osl_glDeleteShader
#define glDeleteTextures osl_glDeleteTextures
#define glDeleteVertexArrays osl_glDeleteVertexArrays
#define glDetachShader osl_glDetachShader
#define glDisable osl_glDisable
#define glDisableVertexAttribArray osl_glDisableVertexAttribArray
#define glDrawElements osl_glDrawElements
#define glEnable osl_glEnable
#define glEnableVertexAttribArray osl_glEnableVertexAttribArray
#define glGenBuffers osl_glGenBuffers
#define glGenSamplers osl_glGenSamplers
#define glGenTextures osl_glGenTextures
#define glGenVertexArrays osl_glGenVertexArrays
#define glGetAttribLocation osl_glGetAttribLocation
#define glGetError osl_glGetError
#define glGetIntegerv osl_glGetIntegerv
#define glGetProgramInfoLog osl_glGetProgramInfoLog
#define glGetProgramiv osl_glGetProgramiv
#define glGetShaderInfoLog osl_glGetShaderInfoLog
#define glGetShaderiv osl_glGetShaderiv
#define glGetString osl_glGetString
#define glGetStringi osl_glGetStringi
#define glGetUniformLocation osl_glGetUniformLocation
#define glGetVertexAttribPointerv osl_glGetVertexAttribPointerv
#define glGetVertexAttribiv osl_glGetVertexAttribiv
#define glIsEnabled osl_glIsEnabled
#define glIsProgram osl_glIsProgram
#define glLinkProgram osl_glLinkProgram
#define glPixelStorei osl_glPixelStorei
#define glSamplerParameteri osl_glSamplerParameteri
#define glScissor osl_glScissor
#define glShaderSource osl_glShaderSource
#define glTexImage2D osl_glTexImage2D
#define glTexParameteri osl_glTexParameteri
#define glTexSubImage2D osl_glTexSubImage2D
#define glUniform1i osl_glUniform1i
#define glUniformMatrix4fv osl_glUniformMatrix4fv
#define glUseProgram osl_glUseProgram
#define glVertexAttribPointer osl_glVertexAttribPointer
#define glViewport osl_glViewport

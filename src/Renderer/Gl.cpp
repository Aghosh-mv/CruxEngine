#include "Renderer/Gl.h"
#include <GL/glx.h>
#include <cstdio>

namespace Frost {
namespace Gl {

#define FROST_DEFINE_FUNC(NAME, RET, ARGS) RET (*NAME) ARGS = nullptr;
FROST_GL_FUNCS(FROST_DEFINE_FUNC)
#undef FROST_DEFINE_FUNC

bool loadFunctions() {
    auto load = [](const char* name) -> void* {
        return (void*)glXGetProcAddressARB((const GLubyte*)name);
    };

#define FROST_LOAD_FUNC(NAME, RET, ARGS) \
    NAME = (RET (*) ARGS)load("gl" #NAME); \
    if (!NAME) { std::fprintf(stderr, "[GL] missing entry point: gl%s\n", #NAME); return false; }
    FROST_GL_FUNCS(FROST_LOAD_FUNC)
#undef FROST_LOAD_FUNC

    return true;
}

const char* errorString(GLenum err) {
    switch (err) {
        case GL_NO_ERROR: return "no error";
        case GL_INVALID_ENUM: return "invalid enum";
        case GL_INVALID_VALUE: return "invalid value";
        case GL_INVALID_OPERATION: return "invalid operation";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "invalid framebuffer operation";
        case GL_OUT_OF_MEMORY: return "out of memory";
        case GL_STACK_UNDERFLOW: return "stack underflow";
        case GL_STACK_OVERFLOW: return "stack overflow";
        default: return "unknown";
    }
}

}
}

#ifdef BUILD_LINUX

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#elif BUILD_MACOS

#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>

#else

#error "missing define for gl.h"
#endif

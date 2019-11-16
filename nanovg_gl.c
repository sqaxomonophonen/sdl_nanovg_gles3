#include "gl.h"
#include "nanovg.h"

#ifdef BUILD_LINUX
#define NANOVG_GLES3_IMPLEMENTATION
#elif BUILD_MACOS
#define NANOVG_GL3_IMPLEMENTATION
#else
#error "missing BUILD_* define"
#endif

#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

NVGcontext* nanovg_create_context()
{
	const int flags = NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG;
	#ifdef BUILD_LINUX
		return nvgCreateGLES3(flags);
	#elif BUILD_MACOS
		return nvgCreateGL3(flags);
	#else
	#error "missing BUILD_* define"
	#endif
}

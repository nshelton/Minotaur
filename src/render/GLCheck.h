#pragma once

#include <glad/glad.h>

#ifndef NDEBUG
#include <glog/logging.h>

// Drain the GL error queue and log every pending error with file/line.
// Debug-only; compiles to nothing in release builds.
#define CHECK_GL()                                                             \
	do                                                                         \
	{                                                                          \
		for (GLenum _glErr = glGetError(); _glErr != GL_NO_ERROR;              \
		     _glErr = glGetError())                                            \
		{                                                                      \
			LOG(ERROR) << "GL error " << _glErr << " at " << __FILE__ << ":"   \
			           << __LINE__;                                            \
		}                                                                      \
	} while (0)
#else
#define CHECK_GL() ((void)0)
#endif

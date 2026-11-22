#pragma once

#include <glad/gl.h>

#include <string>
#include <utility>
#include <vector>

// Reads a complete text file, reporting an error and returning an empty string on failure.
// @param path File path to read.
// @return Complete file contents, or an empty string if opening fails.
std::string readFile(const std::string& path);

// Compiles one shader and returns zero after reporting compiler diagnostics on failure.
// @param type OpenGL shader stage.
// @param source GLSL source text.
// @param label Human-readable stage label for diagnostics.
// @return Compiled shader handle, or zero on failure.
GLuint compileShader(GLenum type, const std::string& source, const char* label);

// Compiles and links the supplied shader sources into a program, or returns zero.
// @param shaders Shader stage/source pairs to attach and link.
// @return Linked program handle, or zero if compilation or linking fails.
GLuint createProgram(const std::vector<std::pair<GLenum, std::string>>& shaders);

// Creates an R32UI texture initialized from row-major cell states.
// @param width Texture width in cells.
// @param height Texture height in cells.
// @param initialState Row-major initial cell values.
// @return OpenGL texture handle.
GLuint createStateTexture(int width, int height,
                          const std::vector<unsigned int>& initialState);

// Binds an R32UI texture as an image for shader image load/store operations.
// @param unit Image unit to bind.
// @param texture OpenGL texture handle.
// @param access Image access mode such as GL_READ_ONLY or GL_READ_WRITE.
// @return No value; updates the OpenGL image binding.
void bindImageTexture(GLuint unit, GLuint texture, GLenum access);

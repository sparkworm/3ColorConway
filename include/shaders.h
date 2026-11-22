#pragma once

#include <glad/gl.h>

#include <string>
#include <utility>
#include <vector>

std::string readFile(const std::string& path);
GLuint compileShader(GLenum type, const std::string& source, const char* label);
GLuint createProgram(const std::vector<std::pair<GLenum, std::string>>& shaders);
GLuint createStateTexture(int width, int height,
                          const std::vector<unsigned int>& initialState);
void bindImageTexture(GLuint unit, GLuint texture, GLenum access);

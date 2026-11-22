#include "shaders.h"

#include <fstream>
#include <iostream>
#include <iterator>

// Loads shader source as text and reports a readable path when opening fails.
// @param path File path containing shader source.
// @return Complete source text, or an empty string if the file cannot be opened.
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Could not open shader file: " << path << '\n';
        return {};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Compiles shader source and returns zero after printing the driver's error log.
// @param type OpenGL shader stage.
// @param source GLSL source text to compile.
// @param label Stage label included in diagnostics.
// @return Compiled shader handle, or zero on failure.
GLuint compileShader(GLenum type, const std::string& source, const char* label) {
    const GLuint shader = glCreateShader(type);
    const char* sourceText = source.c_str();
    glShaderSource(shader, 1, &sourceText, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(logLength), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    std::cerr << "Failed to compile " << label << " shader:\n" << log << '\n';
    glDeleteShader(shader);
    return 0;
}

// Builds a linked program, releasing intermediate shader objects on every path.
// @param shaders Shader stage/source pairs to compile and link.
// @return Linked program handle, or zero if compilation or linking fails.
GLuint createProgram(const std::vector<std::pair<GLenum, std::string>>& shaders) {
    GLuint program = glCreateProgram();
    std::vector<GLuint> compiledShaders;

    for (const auto& [type, source] : shaders) {
        const GLuint shader = compileShader(type, source, type == GL_COMPUTE_SHADER
                                                              ? "compute"
                                                              : type == GL_VERTEX_SHADER
                                                                    ? "vertex"
                                                                    : "fragment");
        if (shader == 0) {
            for (const GLuint compiled : compiledShaders) {
                glDeleteShader(compiled);
            }
            glDeleteProgram(program);
            return 0;
        }

        glAttachShader(program, shader);
        compiledShaders.push_back(shader);
    }

    glLinkProgram(program);
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    for (const GLuint shader : compiledShaders) {
        glDeleteShader(shader);
    }

    if (success == GL_TRUE) {
        return program;
    }

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(logLength), '\0');
    glGetProgramInfoLog(program, logLength, nullptr, log.data());
    std::cerr << "Failed to link shader program:\n" << log << '\n';
    glDeleteProgram(program);
    return 0;
}

// Allocates an integer texture for a row-major cell grid and initializes its contents.
// @param width Texture width in cells.
// @param height Texture height in cells.
// @param initialState Row-major cell values used to initialize the texture.
// @return OpenGL texture handle.
GLuint createStateTexture(int width, int height,
                          const std::vector<unsigned int>& initialState) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_INT, initialState.data());
    return texture;
}

// Exposes a texture as an R32UI shader image with the requested access mode.
// @param unit Image unit to bind.
// @param texture OpenGL texture handle to bind.
// @param access OpenGL image access mode.
// @return No value; changes the current image-unit binding.
void bindImageTexture(GLuint unit, GLuint texture, GLenum access) {
    glBindImageTexture(unit, texture, 0, GL_FALSE, 0, access, GL_R32UI);
}

#include <glad/gl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

// The simulation is a fixed 256-by-192 grid. These dimensions are also sent
// to the shaders so they can work in grid coordinates instead of pixels.
constexpr int GridColumns = 256;
constexpr int GridRows = 192;
// Cell data stays in integer states. Rendering maps only this live state to
// white and every other state to black.
constexpr unsigned int DeadState = 0b0000;
constexpr unsigned int WhiteState = 0b0111;
constexpr auto SimulationStep = std::chrono::milliseconds(100);

// Read a shader source file as text. OpenGL's shader functions receive source
// code as text and do not open files themselves.
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Could not open shader file: " << path << '\n';
        return {};
    }

    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Create and compile one shader stage. A return value of zero means the
// compilation failed; otherwise the number is an OpenGL shader object handle.
GLuint compileShader(GLenum type, const std::string& source, const char* label) {
    // glCreateShader creates an empty shader object of the requested stage.
    const GLuint shader = glCreateShader(type);
    const char* sourceText = source.c_str();
    // Upload the source text, then ask the driver to compile it.
    glShaderSource(shader, 1, &sourceText, nullptr);
    glCompileShader(shader);

    // Shader objects have queryable properties; here we check compile status.
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

// Combine vertex/fragment or compute shader stages into one usable program.
GLuint createProgram(const std::vector<std::pair<GLenum, std::string>>& shaders) {
    // A program is a linkable collection of compiled shader stages.
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

    // Linking resolves stage interfaces and prepares the program for execution.
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

// Allocate a 2D integer texture that stores one unsigned integer per cell.
// Textures are GPU objects whose lifetime and contents are managed by OpenGL.
GLuint createStateTexture(int width, int height,
                          const std::vector<unsigned int>& initialState) {
    GLuint texture = 0;
    // A generated texture handle is the identifier used by later gl* calls.
    glGenTextures(1, &texture);
    // Binding makes this texture the active target of subsequent texture calls.
    glBindTexture(GL_TEXTURE_2D, texture);
    // Nearest filtering keeps the grid cells crisp when the texture is enlarged.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Clamp coordinates at the edge; the simulation shader handles neighbors
    // itself, so off-grid reads should not wrap around the grid.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // GL_R32UI means red channel, 32-bit unsigned integer. Uploading CPU memory
    // performs the one-time copy from std::vector into GPU texture storage.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_INT, initialState.data());
    return texture;
}

// Bind a texture as a shader image. Compute shaders use image units for reads
// and writes; the access mode documents whether this pass can load or store.
void bindImageTexture(GLuint unit, GLuint texture, GLenum access) {
    glBindImageTexture(unit, texture, 0, GL_FALSE, 0, access, GL_R32UI);
}

} // namespace

int main() {
    // GLFW creates a window and an operating-system graphics context. OpenGL
    // calls cannot start until this initialization and context creation succeed.
    if (glfwInit() == GLFW_FALSE) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    // Hints must be set before glfwCreateWindow. Compute shaders and image
    // load/store require OpenGL 4.3; core profile excludes legacy fixed-function APIs.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "3ColorConway", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create an OpenGL 4.3 window. Check that your graphics driver supports OpenGL 4.3.\n";
        glfwTerminate();
        return 1;
    }

    // All subsequent OpenGL calls operate on this window's current context.
    glfwMakeContextCurrent(window);
    // GLAD finds the OpenGL function pointers exposed by the graphics driver.
    // Calling modern OpenGL functions without loading them this way is invalid.
    if (gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)) == 0) {
        std::cerr << "Failed to load OpenGL functions\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << '\n';

    // Compile separate programs for generation updates, mouse painting, and display.
    // Each program owns GPU shader state and is activated with glUseProgram.
    const std::string shaderDirectory = SHADER_DIRECTORY;
    const GLuint updateProgram = createProgram({
        {GL_COMPUTE_SHADER, readFile(shaderDirectory + "/update.comp")},
    });
    const GLuint paintProgram = createProgram({
        {GL_COMPUTE_SHADER, readFile(shaderDirectory + "/paint.comp")},
    });
    const GLuint displayProgram = createProgram({
        {GL_VERTEX_SHADER, readFile(shaderDirectory + "/display.vert")},
        {GL_FRAGMENT_SHADER, readFile(shaderDirectory + "/display.frag")},
    });

    if (updateProgram == 0 || paintProgram == 0 || displayProgram == 0) {
        if (updateProgram != 0) glDeleteProgram(updateProgram);
        if (paintProgram != 0) glDeleteProgram(paintProgram);
        if (displayProgram != 0) glDeleteProgram(displayProgram);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Ping-pong textures let one pass read the current generation while writing the next.
    std::vector<unsigned int> initialState(GridColumns * GridRows, DeadState);
    GLuint currentTexture = createStateTexture(GridColumns, GridRows, initialState);
    GLuint nextTexture = createStateTexture(GridColumns, GridRows, initialState);
    // This 1x1 GPU texture remembers whether the active mouse stroke paints alive or dead.
    const std::vector<unsigned int> initialStrokeState{DeadState};
    const GLuint strokeTexture = createStateTexture(1, 1, initialStrokeState);

    // A vertex array object tracks vertex-stage configuration. This app does not
    // supply a vertex buffer: display.vert creates a triangle from gl_VertexID.
    // The generated VAO handle is still required before glDrawArrays is called.
    GLuint vertexArray = 0;
    glGenVertexArrays(1, &vertexArray);

    // Uniforms are per-program input values set from the CPU. Querying locations
    // once and later calling glUniform... is how data is sent into GLSL variables.
    glUseProgram(updateProgram);
    glUniform2i(glGetUniformLocation(updateProgram, "gridSize"), GridColumns, GridRows);
    glUniform1ui(glGetUniformLocation(updateProgram, "deadState"), DeadState);
    glUniform1ui(glGetUniformLocation(updateProgram, "aliveState"), WhiteState);

    glUseProgram(paintProgram);
    glUniform1ui(glGetUniformLocation(paintProgram, "deadState"), DeadState);
    glUniform1ui(glGetUniformLocation(paintProgram, "aliveState"), WhiteState);

    glUseProgram(displayProgram);
    glUniform2i(glGetUniformLocation(displayProgram, "gridSize"), GridColumns, GridRows);
    glUniform1i(glGetUniformLocation(displayProgram, "stateTexture"), 0);
    glUniform1ui(glGetUniformLocation(displayProgram, "aliveState"), WhiteState);

    // These variables track input transitions and the previous mouse position.
    // wasMouseDown detects a press; haveLastCell prevents a new stroke segment
    // from being incorrectly extended from an old cursor location.
    bool wasMouseDown = false;
    bool strokeInitialized = false;
    bool haveLastCell = false;
    int lastColumn = 0;
    int lastRow = 0;
    bool spaceWasDown = false;
    bool paused = false;
    auto nextUpdate = std::chrono::steady_clock::now() + SimulationStep;

    // Cache uniform locations used on every paint dispatch.
    const GLint fromCellLocation = glGetUniformLocation(paintProgram, "fromCell");
    const GLint toCellLocation = glGetUniformLocation(paintProgram, "toCell");
    const GLint cellCountLocation = glGetUniformLocation(paintProgram, "cellCount");
    const GLint startStrokeLocation = glGetUniformLocation(paintProgram, "startStroke");

    std::cout << "Left-drag to paint cells. Space pauses or resumes; Escape quits.\n";

    // One iteration of this loop handles input, optionally runs GPU passes,
    // redraws the window, presents the image, and reads the next input events.
    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        const bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceDown && !spaceWasDown) {
            paused = !paused;
            std::cout << (paused ? "Paused\n" : "Running\n");
        }
        spaceWasDown = spaceDown;

        const bool mouseDown =
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (mouseDown && !wasMouseDown) {
            // A new drag chooses its paint/erase state on the first cell it reaches.
            strokeInitialized = false;
            haveLastCell = false;
        }
        wasMouseDown = mouseDown;

        if (mouseDown) {
            double cursorX = 0.0;
            double cursorY = 0.0;
            int windowWidth = 0;
            int windowHeight = 0;
            glfwGetCursorPos(window, &cursorX, &cursorY);
            glfwGetWindowSize(window, &windowWidth, &windowHeight);

            if (windowWidth > 0 && windowHeight > 0 &&
                cursorX >= 0.0 && cursorX < windowWidth &&
                cursorY >= 0.0 && cursorY < windowHeight) {
                // Convert GLFW's window-space cursor position to grid coordinates.
                const int column = static_cast<int>(cursorX * GridColumns / windowWidth);
                const int row = static_cast<int>(cursorY * GridRows / windowHeight);

                glUseProgram(paintProgram);
                bindImageTexture(0, currentTexture, GL_READ_WRITE);
                bindImageTexture(1, strokeTexture, GL_READ_WRITE);
                if (!strokeInitialized) {
                    // The paint shader reads this cell to choose alive or dead for the stroke.
                    glUniform2i(fromCellLocation, column, row);
                    glUniform2i(toCellLocation, column, row);
                    glUniform1ui(cellCountLocation, 1U);
                    glUniform1ui(startStrokeLocation, 1U);
                    // glDispatchCompute launches the compute program on the GPU.
                    glDispatchCompute(1, 1, 1);
                    // Wait until this image write is visible before later invocations use it.
                    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                    strokeInitialized = true;
                    haveLastCell = true;
                    lastColumn = column;
                    lastRow = row;
                } else {
                    // Interpolate between sampled cursor positions so fast drags don't skip cells.
                    const int fromColumn = haveLastCell ? lastColumn : column;
                    const int fromRow = haveLastCell ? lastRow : row;
                    const int steps = std::max(std::abs(column - fromColumn),
                                               std::abs(row - fromRow));
                    const unsigned int cellCount = static_cast<unsigned int>(steps + 1);
                    glUniform2i(fromCellLocation, fromColumn, fromRow);
                    glUniform2i(toCellLocation, column, row);
                    glUniform1ui(cellCountLocation, cellCount);
                    glUniform1ui(startStrokeLocation, 0U);
                    glDispatchCompute((cellCount + 63U) / 64U, 1, 1);
                    haveLastCell = true;
                    lastColumn = column;
                    lastRow = row;
                }
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                                GL_TEXTURE_FETCH_BARRIER_BIT);
            } else {
                haveLastCell = false;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (paused) {
            // Keep the timer current so resuming doesn't trigger a burst of missed generations.
            nextUpdate = now + SimulationStep;
        } else if (now >= nextUpdate) {
            // Each 16x16 workgroup updates its assigned cells; the shader bounds-checks edges.
            glUseProgram(updateProgram);
            bindImageTexture(0, currentTexture, GL_READ_ONLY);
            bindImageTexture(1, nextTexture, GL_WRITE_ONLY);
            // Rounded-up grid dimensions launch enough workgroups to cover every cell.
            glDispatchCompute((GridColumns + 15) / 16, (GridRows + 15) / 16, 1);
            // Make the compute writes finish before swapping textures or sampling them.
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                            GL_TEXTURE_FETCH_BARRIER_BIT);
            // The completed output becomes the input for the next generation and display.
            std::swap(currentTexture, nextTexture);
            nextUpdate = now + SimulationStep;
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        // Framebuffer pixels can differ from window pixels on high-DPI displays.
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        // glViewport defines the output rectangle where normalized device
        // coordinates are mapped onto actual framebuffer pixels.
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        // Clear the current framebuffer before drawing the next frame.
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw the current GPU state directly; no full-grid readback to the CPU is needed.
        glUseProgram(displayProgram);
        // Texture unit 0 receives the state texture sampled by display.frag.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTexture);
        glBindVertexArray(vertexArray);
        // The oversized triangle has three vertices and produces fragments for the
        // whole viewport; display.frag then looks up the corresponding grid cell.
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // The window has a front/back framebuffer pair. Swap makes this rendered
        // frame visible, while glfwPollEvents processes keyboard and mouse input.
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Release GPU objects before destroying the OpenGL context.
    glDeleteVertexArrays(1, &vertexArray);
    glDeleteTextures(1, &currentTexture);
    glDeleteTextures(1, &nextTexture);
    glDeleteTextures(1, &strokeTexture);
    glDeleteProgram(updateProgram);
    glDeleteProgram(paintProgram);
    glDeleteProgram(displayProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

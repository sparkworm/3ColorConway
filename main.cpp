#include <glad/gl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
constexpr float MinZoom = 0.5F;
constexpr float MaxZoom = 8.0F;
// Multiplicative zoom keeps wheel increments consistent at every zoom level.
constexpr float ZoomStep = 1.2F;
// Cell data stays in integer states. Rendering maps only this live state to
// white and every other state to black.
constexpr unsigned int DeadState = 0b0000;
constexpr unsigned int WhiteState = 0b0111;
constexpr auto SimulationStep = std::chrono::milliseconds(100);

struct Camera {
    // The camera is centered in grid coordinates so input and rendering share
    // the same mapping regardless of window size or framebuffer scale.
    float centerX = GridColumns * 0.5F;
    float centerY = GridRows * 0.5F;
    float zoom = 1.0F;
    // GLFW callbacks run during event polling; accumulate wheel movement here
    // and apply it in the main loop alongside keyboard input.
    double scrollDelta = 0.0;
};

struct ViewSize {
    float width;
    float height;
};

ViewSize getViewSize(int framebufferWidth, int framebufferHeight, float zoom) {
    // Fit the whole grid without stretching it, then scale the visible
    // grid-space dimensions inversely with zoom.
    const float fitScale = std::min(
        static_cast<float>(framebufferWidth) / GridColumns,
        static_cast<float>(framebufferHeight) / GridRows);
    return {
        static_cast<float>(framebufferWidth) / (fitScale * zoom),
        static_cast<float>(framebufferHeight) / (fitScale * zoom),
    };
}

void clampCamera(Camera& camera) {
    // Keeping the center within the finite grid guarantees that some part of
    // the grid remains visible, even when the viewport is larger than it.
    camera.centerX = std::clamp(camera.centerX, 0.0F, static_cast<float>(GridColumns));
    camera.centerY = std::clamp(camera.centerY, 0.0F, static_cast<float>(GridRows));
}

void scrollCallback(GLFWwindow* window, double, double yOffset) {
    // Let ImGui consume wheel input while a panel control is under the cursor.
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    auto* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    camera->scrollDelta += yOffset;
}

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
    // These are the current simulation rules; the panel edits them directly.
    std::array<int, 4> thresholds{1, 3, 3, 4};
    Camera camera;
    glfwSetWindowUserPointer(window, &camera);
    glfwSetScrollCallback(window, scrollCallback);
    // GLAD finds the OpenGL function pointers exposed by the graphics driver.
    // Calling modern OpenGL functions without loading them this way is invalid.
    if (gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)) == 0) {
        std::cerr << "Failed to load OpenGL functions\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Create ImGui after loading OpenGL so its GLFW/OpenGL backends can use the context.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        std::cerr << "Failed to initialize Dear ImGui GLFW backend\n";
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 430")) {
        std::cerr << "Failed to initialize Dear ImGui OpenGL backend\n";
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
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
    const GLint maxUnderpopulationLocation =
        glGetUniformLocation(updateProgram, "maxUnderpopulation");
    const GLint minBirthLocation = glGetUniformLocation(updateProgram, "minBirth");
    const GLint maxBirthLocation = glGetUniformLocation(updateProgram, "maxBirth");
    const GLint minOverpopulationLocation =
        glGetUniformLocation(updateProgram, "minOverpopulation");

    glUseProgram(paintProgram);
    glUniform1ui(glGetUniformLocation(paintProgram, "deadState"), DeadState);
    glUniform1ui(glGetUniformLocation(paintProgram, "aliveState"), WhiteState);

    glUseProgram(displayProgram);
    glUniform2i(glGetUniformLocation(displayProgram, "gridSize"), GridColumns, GridRows);
    glUniform1i(glGetUniformLocation(displayProgram, "stateTexture"), 0);
    glUniform1ui(glGetUniformLocation(displayProgram, "aliveState"), WhiteState);
    const GLint viewOriginLocation = glGetUniformLocation(displayProgram, "viewOrigin");
    const GLint viewSizeLocation = glGetUniformLocation(displayProgram, "viewSize");

    // These variables track input transitions and the previous mouse position.
    // wasMouseDown detects a press; haveLastCell prevents a new stroke segment
    // from being incorrectly extended from an old cursor location.
    bool wasMouseDown = false;
    bool strokeInitialized = false;
    bool haveLastCell = false;
    int lastColumn = 0;
    int lastRow = 0;
    bool spaceWasDown = false;
    bool zoomInWasDown = false;
    bool zoomOutWasDown = false;
    bool paused = false;
    auto nextUpdate = std::chrono::steady_clock::now() + SimulationStep;
    auto lastFrame = std::chrono::steady_clock::now();

    // Cache uniform locations used on every paint dispatch.
    const GLint fromCellLocation = glGetUniformLocation(paintProgram, "fromCell");
    const GLint toCellLocation = glGetUniformLocation(paintProgram, "toCell");
    const GLint cellCountLocation = glGetUniformLocation(paintProgram, "cellCount");
    const GLint startStrokeLocation = glGetUniformLocation(paintProgram, "startStroke");

    std::cout << "Left-drag to paint. Adjust rules in the parameter panel. "
                 "Arrow/WASD keys pan, scroll or +/- zoom. Space pauses; Escape quits.\n";

    // One iteration of this loop handles input, optionally runs GPU passes,
    // redraws the window, presents the image, and reads the next input events.
    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        // Start a fresh UI frame, then declare the controls that should appear this frame.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(12.0F, 12.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(320.0F, 410.0F), ImGuiCond_Always);
        // Keep the compact settings panel fixed over the upper-left of the grid.
        ImGui::Begin("Life parameters", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoCollapse);
        ImGui::TextUnformatted("Neighbor thresholds (0-8)");
        ImGui::Separator();
        // Each slider edits one rule threshold immediately; hidden IDs keep the visible labels separate.
        ImGui::TextUnformatted("Max to die of underpopulation");
        ImGui::SliderInt("##underpopulation", &thresholds[0], 0, 8);
        ImGui::TextUnformatted("Min to come alive");
        ImGui::SliderInt("##minBirth", &thresholds[1], 0, 8);
        ImGui::TextUnformatted("Max to come alive");
        ImGui::SliderInt("##maxBirth", &thresholds[2], 0, 8);
        ImGui::TextUnformatted("Min to die of overpopulation");
        ImGui::SliderInt("##overpopulation", &thresholds[3], 0, 8);
        ImGui::Separator();
        ImGui::TextDisabled("Standard Conway: 1, 3, 3, 4");
        // These warnings describe bounds that make birth or survival impossible.
        ImGui::TextWrapped(
            "Birth uses its inclusive bounds; live cells survive strictly between the death thresholds.");
        if (thresholds[1] > thresholds[2]) {
            ImGui::TextColored(ImVec4(1.0F, 0.65F, 0.2F, 1.0F),
                               "Warning: no cells can be born.");
        }
        if (thresholds[0] + 1 >= thresholds[3]) {
            ImGui::TextColored(ImVec4(1.0F, 0.65F, 0.2F, 1.0F),
                               "Warning: no live cells can survive.");
        }
        ImGui::End();
        // Avoid interpreting clicks on the settings panel as cell-painting input.
        const bool captureMouse = ImGui::GetIO().WantCaptureMouse;

        const auto frameStart = std::chrono::steady_clock::now();
        const float deltaSeconds = std::min(
            std::chrono::duration<float>(frameStart - lastFrame).count(), 0.1F);
        lastFrame = frameStart;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        const bool zoomInDown =
            glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS;
        const bool zoomOutDown =
            glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS;
        // Key zoom is edge-triggered so holding a key doesn't repeatedly zoom.
        if (zoomInDown && !zoomInWasDown) {
            camera.zoom = std::min(camera.zoom * ZoomStep, MaxZoom);
        }
        if (zoomOutDown && !zoomOutWasDown) {
            camera.zoom = std::max(camera.zoom / ZoomStep, MinZoom);
        }
        zoomInWasDown = zoomInDown;
        zoomOutWasDown = zoomOutDown;

        if (camera.scrollDelta != 0.0) {
            // Exponentiating supports both wheel directions and multi-notch events.
            camera.zoom = std::clamp(
                camera.zoom * std::pow(ZoomStep, static_cast<float>(camera.scrollDelta)),
                MinZoom, MaxZoom);
            camera.scrollDelta = 0.0;
        }

        const float panX =
            static_cast<float>(glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS ||
                               glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) -
            static_cast<float>(glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS ||
                               glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
        const float panY =
            static_cast<float>(glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS ||
                               glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) -
            static_cast<float>(glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS ||
                               glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS);
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth > 0 && framebufferHeight > 0) {
            const ViewSize viewSize = getViewSize(framebufferWidth, framebufferHeight, camera.zoom);
            // Scale movement to the visible area and elapsed time so panning
            // feels consistent across zoom levels and frame rates.
            const float panSpeed = 0.8F * deltaSeconds;
            camera.centerX += panX * viewSize.width * panSpeed;
            camera.centerY += panY * viewSize.height * panSpeed;
            clampCamera(camera);
        }

        const bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceDown && !spaceWasDown) {
            paused = !paused;
            std::cout << (paused ? "Paused\n" : "Running\n");
        }
        spaceWasDown = spaceDown;

        const bool mouseDown = !captureMouse &&
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
                int currentFramebufferWidth = 0;
                int currentFramebufferHeight = 0;
                glfwGetFramebufferSize(window, &currentFramebufferWidth,
                                       &currentFramebufferHeight);
                bool insideGrid = false;
                int column = 0;
                int row = 0;
                if (currentFramebufferWidth > 0 && currentFramebufferHeight > 0) {
                    const ViewSize viewSize = getViewSize(
                        currentFramebufferWidth, currentFramebufferHeight, camera.zoom);
                    const float originX = camera.centerX - viewSize.width * 0.5F;
                    const float originY = camera.centerY - viewSize.height * 0.5F;
                    // Convert window-space cursor coordinates through the camera
                    // view; framebuffer/window ratios cancel for high-DPI displays.
                    const float gridX = originX +
                        static_cast<float>(cursorX / windowWidth) * viewSize.width;
                    const float gridY = originY +
                        static_cast<float>(cursorY / windowHeight) * viewSize.height;
                    insideGrid = gridX >= 0.0F && gridX < GridColumns &&
                                 gridY >= 0.0F && gridY < GridRows;
                    if (insideGrid) {
                        column = static_cast<int>(gridX);
                        row = static_cast<int>(gridY);
                    }
                }
                if (insideGrid) {
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
                    // Don't connect a later in-grid cell to the last cell before
                    // the cursor crossed the visible exterior.
                    haveLastCell = false;
                }
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
            // Upload the current panel values when dispatching the next generation.
            glUniform1ui(maxUnderpopulationLocation,
                         static_cast<unsigned int>(thresholds[0]));
            glUniform1ui(minBirthLocation,
                         static_cast<unsigned int>(thresholds[1]));
            glUniform1ui(maxBirthLocation,
                         static_cast<unsigned int>(thresholds[2]));
            glUniform1ui(minOverpopulationLocation,
                         static_cast<unsigned int>(thresholds[3]));
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

        // Framebuffer pixels can differ from window pixels on high-DPI displays.
        // glViewport defines the output rectangle where normalized device
        // coordinates are mapped onto actual framebuffer pixels.
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        // Clear the current framebuffer before drawing the next frame.
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw the current GPU state directly; no full-grid readback to the CPU is needed.
        glUseProgram(displayProgram);
        if (framebufferWidth > 0 && framebufferHeight > 0) {
            const ViewSize viewSize = getViewSize(
                framebufferWidth, framebufferHeight, camera.zoom);
            // The shader maps each fragment through this grid-space rectangle.
            glUniform2f(viewOriginLocation,
                        camera.centerX - viewSize.width * 0.5F,
                        camera.centerY - viewSize.height * 0.5F);
            glUniform2f(viewSizeLocation, viewSize.width, viewSize.height);
        }
        // Texture unit 0 receives the state texture sampled by display.frag.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTexture);
        glBindVertexArray(vertexArray);
        // The oversized triangle has three vertices and produces fragments for the
        // whole viewport; display.frag then looks up the corresponding grid cell.
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Draw the controls after the grid so the panel appears as an overlay.
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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
    // Shut down both backends and the context before destroying the GLFW window.
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

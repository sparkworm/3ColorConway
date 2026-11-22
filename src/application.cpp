#include "application.h"

#include "camera.h"
#include "persistence.h"
#include "shaders.h"
#include "simulation.h"
#include "ui.h"

#include <glad/gl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

int runApplication() {
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

    GLFWwindow* window = glfwCreateWindow(1280, 720, "3ColorConway", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create an OpenGL 4.3 window. Check that your graphics driver supports OpenGL 4.3.\n";
        glfwTerminate();
        return 1;
    }

    // All subsequent OpenGL calls operate on this window's current context.
    glfwMakeContextCurrent(window);
    // These are the current simulation rules; the panel edits them directly.
    RuleSet rules = conwayRules();
    int paintColorIndex = 6;
    Camera camera;
    int gridColumns = GridColumns;
    int gridRows = GridRows;
    glfwSetWindowUserPointer(window, &camera);
    glfwSetScrollCallback(window, cameraScrollCallback);
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
    std::vector<unsigned int> initialState(gridColumns * gridRows, DeadState);
    GLuint currentTexture = createStateTexture(gridColumns, gridRows, initialState);
    GLuint nextTexture = createStateTexture(gridColumns, gridRows, initialState);
    // This 1x1 GPU texture remembers whether the active mouse stroke paints or erases.
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
    glUniform2i(glGetUniformLocation(updateProgram, "gridSize"), gridColumns, gridRows);
    const GLint redWeightsLocation = glGetUniformLocation(updateProgram, "redWeights");
    const GLint greenWeightsLocation = glGetUniformLocation(updateProgram, "greenWeights");
    const GLint blueWeightsLocation = glGetUniformLocation(updateProgram, "blueWeights");
    const GLint birthMinimumLocation = glGetUniformLocation(updateProgram, "birthMinimum");
    const GLint birthMaximumLocation = glGetUniformLocation(updateProgram, "birthMaximum");
    const GLint survivalMinimumLocation =
        glGetUniformLocation(updateProgram, "survivalMinimum");
    const GLint survivalMaximumLocation =
        glGetUniformLocation(updateProgram, "survivalMaximum");

    glUseProgram(paintProgram);
    const GLint paintStateLocation = glGetUniformLocation(paintProgram, "selectedState");

    glUseProgram(displayProgram);
    glUniform2i(glGetUniformLocation(displayProgram, "gridSize"), gridColumns, gridRows);
    glUniform1i(glGetUniformLocation(displayProgram, "stateTexture"), 0);
    const GLint viewOriginLocation = glGetUniformLocation(displayProgram, "viewOrigin");
    const GLint viewSizeLocation = glGetUniformLocation(displayProgram, "viewSize");

    auto resizeGrid = [&](int width, int height,
                          const std::vector<unsigned int>* cells) {
        glDeleteTextures(1, &currentTexture);
        glDeleteTextures(1, &nextTexture);
        gridColumns = width;
        gridRows = height;
        std::vector<unsigned int> state(
            static_cast<std::size_t>(gridColumns) * gridRows, DeadState);
        if (cells != nullptr) {
            state = *cells;
        }
        currentTexture = createStateTexture(gridColumns, gridRows, state);
        nextTexture = createStateTexture(gridColumns, gridRows, state);
        glUseProgram(updateProgram);
        glUniform2i(glGetUniformLocation(updateProgram, "gridSize"),
                    gridColumns, gridRows);
        glUseProgram(displayProgram);
        glUniform2i(glGetUniformLocation(displayProgram, "gridSize"),
                    gridColumns, gridRows);
        camera.centerX = gridColumns * 0.5F;
        camera.centerY = gridRows * 0.5F;
        camera.zoom = 1.0F;
    };

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
    bool escapeWasDown = false;
    bool paused = true;
    bool started = false;
    bool hasSimulation = false;
    bool pausedBeforeMenu = false;
    std::array<char, 512> configurationPath{};
    std::array<char, 512> boardPath{};
    const std::string defaultConfigurationPath = "rules.json";
    const std::string defaultBoardPath = "board.3ccb";
    std::copy(defaultConfigurationPath.begin(), defaultConfigurationPath.end(),
              configurationPath.begin());
    std::copy(defaultBoardPath.begin(), defaultBoardPath.end(),
              boardPath.begin());
    std::string fileStatus;
    bool fileStatusIsError = false;
    auto nextUpdate = std::chrono::steady_clock::now() + SimulationStep;
    auto lastFrame = std::chrono::steady_clock::now();

    // Cache uniform locations used on every paint dispatch.
    const GLint fromCellLocation = glGetUniformLocation(paintProgram, "fromCell");
    const GLint toCellLocation = glGetUniformLocation(paintProgram, "toCell");
    const GLint cellCountLocation = glGetUniformLocation(paintProgram, "cellCount");
    const GLint startStrokeLocation = glGetUniformLocation(paintProgram, "startStroke");

    std::cout << "Left-drag to paint. Adjust rules in the parameter panel. "
                 "Arrow/WASD keys pan, scroll or +/- zoom. Space pauses; Escape returns to the menu.\n";

    // One iteration of this loop handles input, optionally runs GPU passes,
    // redraws the window, presents the image, and reads the next input events.
    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        // Start a fresh UI frame, then declare the controls that should appear this frame.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        const bool mainMenuOpen = !started;
        ControlPanelResult panel;
        bool captureMouse = false;
        bool captureKeyboard = false;
        if (mainMenuOpen) {
            const MainMenuResult menu = drawMainMenu();
            captureMouse = menu.captureMouse;
            captureKeyboard = menu.captureKeyboard;
            if (menu.startDefault) {
                resizeGrid(menu.width, menu.height, nullptr);
                rules = conwayRules();
                paused = false;
                started = true;
                hasSimulation = true;
                pausedBeforeMenu = false;
            }
            if (menu.loadBoard) {
                std::vector<unsigned int> cells;
                RuleSet loadedRules{};
                int loadedWidth = 0;
                int loadedHeight = 0;
                std::string loadError;
                if (loadBoard(menu.boardPath.data(), cells, loadedRules,
                              loadedWidth, loadedHeight, loadError)) {
                    resizeGrid(loadedWidth, loadedHeight, &cells);
                    rules = loadedRules;
                    paused = true;
                    started = true;
                    hasSimulation = true;
                    pausedBeforeMenu = true;
                } else {
                    std::cerr << "Board load failed: " << loadError << '\n';
                }
            }
            if (menu.quit) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        } else {
            panel = drawControlPanel(
                rules, paintColorIndex, configurationPath, boardPath,
                fileStatus, fileStatusIsError);
            captureMouse = panel.captureMouse;
            captureKeyboard = panel.captureKeyboard;
        }
        bool boardLoadedThisFrame = false;
        std::string fileError;
        switch (panel.fileAction) {
        case FileAction::ResetBoard: {
            const std::vector<unsigned int> emptyCells(
                static_cast<std::size_t>(gridColumns) * gridRows, DeadState);
            glBindTexture(GL_TEXTURE_2D, currentTexture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gridColumns, gridRows,
                            GL_RED_INTEGER, GL_UNSIGNED_INT, emptyCells.data());
            glBindTexture(GL_TEXTURE_2D, nextTexture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gridColumns, gridRows,
                            GL_RED_INTEGER, GL_UNSIGNED_INT, emptyCells.data());
            paused = true;
            pausedBeforeMenu = true;
            nextUpdate = std::chrono::steady_clock::now() + SimulationStep;
            strokeInitialized = false;
            haveLastCell = false;
            wasMouseDown = false;
            boardLoadedThisFrame = true;
            fileStatus = "Board cleared; board size and rules preserved.";
            fileStatusIsError = false;
            break;
        }
        case FileAction::SaveConfiguration:
            fileStatusIsError =
                !saveConfiguration(panel.filePath, rules, fileError);
            fileStatus = fileStatusIsError
                             ? "Configuration save failed: " + fileError
                             : "Configuration saved: " + panel.filePath;
            break;
        case FileAction::LoadConfiguration: {
            RuleSet loadedRules{};
            fileStatusIsError =
                !loadConfiguration(panel.filePath, loadedRules, fileError);
            if (fileStatusIsError) {
                fileStatus = "Configuration load failed: " + fileError;
            } else {
                rules = loadedRules;
                fileStatus = "Configuration loaded: " + panel.filePath;
            }
            break;
        }
        case FileAction::SaveBoard: {
            std::vector<unsigned int> cells(
                static_cast<std::size_t>(gridColumns) * gridRows);
            glBindTexture(GL_TEXTURE_2D, currentTexture);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_INT,
                          cells.data());
            fileStatusIsError =
                !saveBoard(panel.filePath, cells, rules, gridColumns, gridRows,
                           fileError);
            fileStatus = fileStatusIsError
                             ? "Board save failed: " + fileError
                             : "Board and rules saved: " + panel.filePath;
            break;
        }
        case FileAction::LoadBoard: {
            std::vector<unsigned int> cells;
            RuleSet loadedRules{};
            int loadedWidth = 0;
            int loadedHeight = 0;
            fileStatusIsError =
                !loadBoard(panel.filePath, cells, loadedRules,
                           loadedWidth, loadedHeight, fileError);
            if (fileStatusIsError) {
                fileStatus = "Board load failed: " + fileError;
            } else {
                rules = loadedRules;
                resizeGrid(loadedWidth, loadedHeight, &cells);
                glBindTexture(GL_TEXTURE_2D, currentTexture);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, loadedWidth,
                                loadedHeight, GL_RED_INTEGER, GL_UNSIGNED_INT,
                                cells.data());
                glBindTexture(GL_TEXTURE_2D, nextTexture);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, loadedWidth,
                                loadedHeight, GL_RED_INTEGER, GL_UNSIGNED_INT,
                                cells.data());
                paused = true;
                hasSimulation = true;
                pausedBeforeMenu = true;
                nextUpdate = std::chrono::steady_clock::now() + SimulationStep;
                strokeInitialized = false;
                haveLastCell = false;
                wasMouseDown = false;
                boardLoadedThisFrame = true;
                fileStatus = "Board and rules loaded; simulation paused: " +
                             panel.filePath;
            }
            break;
        }
        case FileAction::None:
            break;
        }
        const auto frameStart = std::chrono::steady_clock::now();
        const float deltaSeconds = std::min(
            std::chrono::duration<float>(frameStart - lastFrame).count(), 0.1F);
        lastFrame = frameStart;

        const bool escapeDown =
            glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escapeDown && !escapeWasDown) {
            if (started) {
                pausedBeforeMenu = paused;
                started = false;
                paused = true;
                wasMouseDown = false;
                strokeInitialized = false;
                haveLastCell = false;
                camera.scrollDelta = 0.0;
            } else {
                if (hasSimulation) {
                    started = true;
                    paused = pausedBeforeMenu;
                } else {
                    resizeGrid(256, 192, nullptr);
                    rules = conwayRules();
                    started = true;
                    paused = false;
                    hasSimulation = true;
                    pausedBeforeMenu = false;
                }
            }
        }
        escapeWasDown = escapeDown;

        const bool zoomInDown =
            glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS;
        const bool zoomOutDown =
            glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS;
        // Key zoom is edge-triggered so holding a key doesn't repeatedly zoom.
        int zoomFramebufferWidth = 0;
        int zoomFramebufferHeight = 0;
        int zoomWindowWidth = 0;
        int zoomWindowHeight = 0;
        double zoomCursorX = 0.0;
        double zoomCursorY = 0.0;
        glfwGetFramebufferSize(window, &zoomFramebufferWidth,
                               &zoomFramebufferHeight);
        glfwGetWindowSize(window, &zoomWindowWidth, &zoomWindowHeight);
        glfwGetCursorPos(window, &zoomCursorX, &zoomCursorY);
        if (started && !captureKeyboard && !captureMouse &&
            zoomInDown && !zoomInWasDown) {
            zoomCameraAtCursor(
                camera, std::min(camera.zoom * ZoomStep, MaxZoom),
                gridColumns, gridRows,
                zoomFramebufferWidth, zoomFramebufferHeight,
                zoomWindowWidth, zoomWindowHeight,
                zoomCursorX, zoomCursorY);
        }
        if (started && !captureKeyboard && !captureMouse &&
            zoomOutDown && !zoomOutWasDown) {
            zoomCameraAtCursor(
                camera, std::max(camera.zoom / ZoomStep,
                                 getMinimumZoom(gridColumns, gridRows)),
                gridColumns, gridRows,
                zoomFramebufferWidth, zoomFramebufferHeight,
                zoomWindowWidth, zoomWindowHeight,
                zoomCursorX, zoomCursorY);
        }
        zoomInWasDown = zoomInDown;
        zoomOutWasDown = zoomOutDown;

        if (started && !captureMouse && camera.scrollDelta != 0.0) {
            // Exponentiating supports both wheel directions and multi-notch events.
            const float newZoom = std::clamp(
                camera.zoom * std::pow(ZoomStep, static_cast<float>(camera.scrollDelta)),
                getMinimumZoom(gridColumns, gridRows), MaxZoom);
            zoomCameraAtCursor(
                camera, newZoom, gridColumns, gridRows,
                zoomFramebufferWidth, zoomFramebufferHeight,
                zoomWindowWidth, zoomWindowHeight,
                zoomCursorX, zoomCursorY);
            camera.scrollDelta = 0.0;
        }

        const bool allowCameraInput =
            started && !captureKeyboard && !captureMouse;
        const float panX = allowCameraInput
            ? static_cast<float>(glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS ||
                                 glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) -
              static_cast<float>(glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS ||
                                 glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            : 0.0F;
        const float panY = allowCameraInput
            ? static_cast<float>(glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS ||
                                 glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) -
              static_cast<float>(glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS ||
                                 glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            : 0.0F;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth > 0 && framebufferHeight > 0) {
            const ViewSize viewSize = getViewSize(
                framebufferWidth, framebufferHeight, camera.zoom);
            // Scale movement to the visible area and elapsed time so panning
            // feels consistent across zoom levels and frame rates.
            const float panSpeed = 0.8F * deltaSeconds;
            camera.centerX += panX * viewSize.width * panSpeed;
            camera.centerY += panY * viewSize.height * panSpeed;
            clampCamera(camera, gridColumns, gridRows);
        }

        const bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (boardLoadedThisFrame) {
            paused = true;
        } else if (started && !captureKeyboard && !captureMouse &&
                   spaceDown && !spaceWasDown) {
            paused = !paused;
            std::cout << (paused ? "Paused\n" : "Running\n");
        }
        spaceWasDown = spaceDown;

        const bool mouseDown = started && !boardLoadedThisFrame && !captureMouse &&
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
                        currentFramebufferWidth, currentFramebufferHeight,
                        camera.zoom);
                    const float originX = camera.centerX - viewSize.width * 0.5F;
                    const float originY = camera.centerY - viewSize.height * 0.5F;
                    // Convert window-space cursor coordinates through the camera
                    // view; framebuffer/window ratios cancel for high-DPI displays.
                    const float gridX = originX +
                        static_cast<float>(cursorX / windowWidth) * viewSize.width;
                    const float gridY = originY +
                        static_cast<float>(cursorY / windowHeight) * viewSize.height;
                    insideGrid = gridX >= 0.0F && gridX < gridColumns &&
                                 gridY >= 0.0F && gridY < gridRows;
                    if (insideGrid) {
                        column = static_cast<int>(gridX);
                        row = static_cast<int>(gridY);
                    }
                }
                if (insideGrid) {
                    glUseProgram(paintProgram);
                    glUniform1ui(paintStateLocation, getPaintState(paintColorIndex));
                    bindImageTexture(0, currentTexture, GL_READ_WRITE);
                    bindImageTexture(1, strokeTexture, GL_READ_WRITE);
                    if (!strokeInitialized) {
                        // The paint shader reads this cell to choose paint or erase for the stroke.
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
            for (int channel = 0; channel < 3; ++channel) {
                const ChannelRule& rule = rules[channel];
                const GLint weightsLocation = channel == 0 ? redWeightsLocation
                                                : channel == 1 ? greenWeightsLocation
                                                               : blueWeightsLocation;
                glUniform3i(weightsLocation, rule.weights[0], rule.weights[1],
                            rule.weights[2]);
            }
            glUniform3i(birthMinimumLocation, rules[0].birthMin,
                        rules[1].birthMin, rules[2].birthMin);
            glUniform3i(birthMaximumLocation, rules[0].birthMax,
                        rules[1].birthMax, rules[2].birthMax);
            glUniform3i(survivalMinimumLocation, rules[0].survivalMin,
                        rules[1].survivalMin, rules[2].survivalMin);
            glUniform3i(survivalMaximumLocation, rules[0].survivalMax,
                        rules[1].survivalMax, rules[2].survivalMax);
            bindImageTexture(0, currentTexture, GL_READ_ONLY);
            bindImageTexture(1, nextTexture, GL_WRITE_ONLY);
            // Rounded-up grid dimensions launch enough workgroups to cover every cell.
            glDispatchCompute((gridColumns + 15) / 16,
                              (gridRows + 15) / 16, 1);
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

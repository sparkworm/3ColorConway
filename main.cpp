#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <iostream>

constexpr int GridColumns = 32;
constexpr int GridRows = 24;
constexpr int DeadState = 0b0000;
constexpr int WhiteState = 0b0111;

// Update the OpenGL drawing area when the window's framebuffer changes size.
void resizeFramebuffer(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

int main() {
    // Initialize GLFW, which manages the window and OpenGL context.
    if (glfwInit() == GLFW_FALSE) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    // Request an OpenGL 3.3 core-profile context.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create a window and its associated OpenGL context.
    GLFWwindow* window = glfwCreateWindow(800, 600, "OpenGL starter", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create an OpenGL window\n";
        glfwTerminate();
        return 1;
    }

    // Make this window's context current so OpenGL calls affect it.
    glfwMakeContextCurrent(window);
    // Keep the OpenGL viewport matched to the window's framebuffer size.
    glfwSetFramebufferSizeCallback(window, resizeFramebuffer);

    // Set the initial viewport; the resize callback handles later changes.
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    resizeFramebuffer(window, width, height);

    // Cell state is stored on the CPU as an integer, independently of its display color.
    std::array<int, GridColumns * GridRows> cells{};
    bool wasMouseDown = false;
    bool strokeInitialized = false;
    int strokeState = DeadState;

    // Draw frames and handle window input until the user closes the window.
    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        // Let Escape close the window.
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // Choose whether this drag paints cells alive or dead based on its first cell.
        const bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (mouseDown && !wasMouseDown) {
            strokeInitialized = false;
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
                const int column = static_cast<int>(cursorX * GridColumns / windowWidth);
                const int row = static_cast<int>(cursorY * GridRows / windowHeight);
                const int index = row * GridColumns + column;

                if (!strokeInitialized) {
                    strokeState = cells[index] == DeadState ? WhiteState : DeadState;
                    strokeInitialized = true;
                }
                cells[index] = strokeState;
            }
        }

        // Read the framebuffer size so the grid scales correctly on resize and HiDPI displays.
        glfwGetFramebufferSize(window, &width, &height);

        // Fill the screen with the chosen dark blue-gray color.
        glClearColor(0.00F, 0.00F, 0.00F, 1.0F);
        glDisable(GL_SCISSOR_TEST);
        glClear(GL_COLOR_BUFFER_BIT);

        if (width > 0 && height > 0) {
            // Draw each live cell as a small scissored clear, leaving a one-pixel grid line.
            glEnable(GL_SCISSOR_TEST);
            glClearColor(1.0F, 1.0F, 1.0F, 1.0F);

            for (int row = 0; row < GridRows; ++row) {
                for (int column = 0; column < GridColumns; ++column) {
                    const int index = row * GridColumns + column;
                    if (cells[index] == DeadState) {
                        continue;
                    }

                    const int left = column * width / GridColumns + 1;
                    const int right = (column + 1) * width / GridColumns - 1;
                    const int bottom = height - (row + 1) * height / GridRows + 1;
                    const int top = height - row * height / GridRows - 1;
                    glScissor(left, bottom, std::max(0, right - left), std::max(0, top - bottom));
                    glClear(GL_COLOR_BUFFER_BIT);
                }
            }

            glDisable(GL_SCISSOR_TEST);
        }

        // Present the completed frame, then process window events.
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Release the window and shut down GLFW before exiting.
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

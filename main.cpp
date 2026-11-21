#include <GLFW/glfw3.h>

#include <iostream>

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

    // Draw frames and handle window input until the user closes the window.
    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        // Let Escape close the window.
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // Fill the screen with the chosen dark blue-gray color.
        glClearColor(0.08F, 0.12F, 0.18F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        // Present the completed frame, then process window events.
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Release the window and shut down GLFW before exiting.
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

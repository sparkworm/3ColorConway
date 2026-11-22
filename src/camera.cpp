#include "camera.h"

#include <imgui.h>

#include <algorithm>

// Converts framebuffer size and zoom into the visible rectangle in grid units.
// @param framebufferWidth Framebuffer width in pixels.
// @param framebufferHeight Framebuffer height in pixels.
// @param zoom Current camera magnification.
// @return Visible width and height in grid coordinates.
ViewSize getViewSize(int framebufferWidth, int framebufferHeight, float zoom) {
    const float fitScale = std::min(
        static_cast<float>(framebufferWidth) / ReferenceGridColumns,
        static_cast<float>(framebufferHeight) / ReferenceGridRows);
    return {
        static_cast<float>(framebufferWidth) / (fitScale * zoom),
        static_cast<float>(framebufferHeight) / (fitScale * zoom),
    };
}

// Allows zooming out far enough to fit boards larger than the reference grid.
// @param gridColumns Number of columns in the board.
// @param gridRows Number of rows in the board.
// @return Smallest zoom that fits the board, bounded by the normal minimum.
float getMinimumZoom(int gridColumns, int gridRows) {
    const float fitForWidth =
        ReferenceGridColumns / static_cast<float>(gridColumns);
    const float fitForHeight =
        ReferenceGridRows / static_cast<float>(gridRows);
    const float fullBoardZoom = std::min(fitForWidth, fitForHeight);
    return std::min(MinZoom, fullBoardZoom);
}

// Keeps the camera center within the finite board coordinate bounds.
// @param camera Camera state whose center is clamped.
// @param gridColumns Number of columns in the board.
// @param gridRows Number of rows in the board.
// @return No value; modifies camera in place.
void clampCamera(Camera& camera, int gridColumns, int gridRows) {
    camera.centerX = std::clamp(camera.centerX, 0.0F, static_cast<float>(gridColumns));
    camera.centerY = std::clamp(camera.centerY, 0.0F, static_cast<float>(gridRows));
}

// Updates zoom around a cursor anchor, preserving that grid point when it is on-window.
// @param camera Camera state to update.
// @param newZoom Magnification to apply.
// @param gridColumns Number of columns in the board.
// @param gridRows Number of rows in the board.
// @param framebufferWidth Framebuffer width in pixels.
// @param framebufferHeight Framebuffer height in pixels.
// @param windowWidth Window width in screen coordinates.
// @param windowHeight Window height in screen coordinates.
// @param cursorX Cursor horizontal coordinate in the window.
// @param cursorY Cursor vertical coordinate in the window.
// @return No value; modifies camera zoom and center in place.
void zoomCameraAtCursor(Camera& camera, float newZoom,
                        int gridColumns, int gridRows,
                        int framebufferWidth, int framebufferHeight,
                        int windowWidth, int windowHeight,
                        double cursorX, double cursorY) {
    if (windowWidth <= 0 || windowHeight <= 0 ||
        framebufferWidth <= 0 || framebufferHeight <= 0 ||
        cursorX < 0.0 || cursorX >= windowWidth ||
        cursorY < 0.0 || cursorY >= windowHeight) {
        camera.zoom = newZoom;
        return;
    }

    const ViewSize oldView = getViewSize(
        framebufferWidth, framebufferHeight, camera.zoom);
    const ViewSize newView = getViewSize(
        framebufferWidth, framebufferHeight, newZoom);
    const float normalizedX = static_cast<float>(cursorX / windowWidth);
    const float normalizedY = static_cast<float>(cursorY / windowHeight);
    const float worldX = camera.centerX +
        (normalizedX - 0.5F) * oldView.width;
    const float worldY = camera.centerY +
        (normalizedY - 0.5F) * oldView.height;

    camera.zoom = newZoom;
    camera.centerX = worldX - (normalizedX - 0.5F) * newView.width;
    camera.centerY = worldY - (normalizedY - 0.5F) * newView.height;
    clampCamera(camera, gridColumns, gridRows);
}

// GLFW callback that accumulates wheel movement for processing in the main loop.
// @param window GLFW window whose user pointer refers to a Camera.
// @param xOffset Horizontal wheel offset, unused by this callback.
// @param yOffset Vertical wheel offset to accumulate.
// @return No value; updates the camera's pending scroll delta when UI does not capture input.
void cameraScrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
    (void)xOffset;
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    auto* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    camera->scrollDelta += yOffset;
}

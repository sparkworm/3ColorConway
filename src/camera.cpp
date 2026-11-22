#include "camera.h"

#include <imgui.h>

#include <algorithm>

ViewSize getViewSize(int framebufferWidth, int framebufferHeight, float zoom) {
    const float fitScale = std::min(
        static_cast<float>(framebufferWidth) / ReferenceGridColumns,
        static_cast<float>(framebufferHeight) / ReferenceGridRows);
    return {
        static_cast<float>(framebufferWidth) / (fitScale * zoom),
        static_cast<float>(framebufferHeight) / (fitScale * zoom),
    };
}

float getMinimumZoom(int gridColumns, int gridRows) {
    const float fitForWidth =
        ReferenceGridColumns / static_cast<float>(gridColumns);
    const float fitForHeight =
        ReferenceGridRows / static_cast<float>(gridRows);
    const float fullBoardZoom = std::min(fitForWidth, fitForHeight);
    return std::min(MinZoom, fullBoardZoom);
}

void clampCamera(Camera& camera, int gridColumns, int gridRows) {
    camera.centerX = std::clamp(camera.centerX, 0.0F, static_cast<float>(gridColumns));
    camera.centerY = std::clamp(camera.centerY, 0.0F, static_cast<float>(gridRows));
}

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

void cameraScrollCallback(GLFWwindow* window, double, double yOffset) {
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    auto* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    camera->scrollDelta += yOffset;
}

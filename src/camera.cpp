#include "camera.h"

#include <imgui.h>

#include <algorithm>

ViewSize getViewSize(int framebufferWidth, int framebufferHeight, float zoom) {
    const float fitScale = std::min(
        static_cast<float>(framebufferWidth) / GridColumns,
        static_cast<float>(framebufferHeight) / GridRows);
    return {
        static_cast<float>(framebufferWidth) / (fitScale * zoom),
        static_cast<float>(framebufferHeight) / (fitScale * zoom),
    };
}

void clampCamera(Camera& camera) {
    camera.centerX = std::clamp(camera.centerX, 0.0F, static_cast<float>(GridColumns));
    camera.centerY = std::clamp(camera.centerY, 0.0F, static_cast<float>(GridRows));
}

void cameraScrollCallback(GLFWwindow* window, double, double yOffset) {
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    auto* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
    camera->scrollDelta += yOffset;
}

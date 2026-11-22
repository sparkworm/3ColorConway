#pragma once

#include "simulation.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>

constexpr float MinZoom = 0.5F;
constexpr float MaxZoom = 8.0F;
constexpr float ZoomStep = 1.2F;

struct Camera {
    float centerX = GridColumns * 0.5F;
    float centerY = GridRows * 0.5F;
    float zoom = 1.0F;
    double scrollDelta = 0.0;
};

struct ViewSize {
    float width;
    float height;
};

ViewSize getViewSize(int framebufferWidth, int framebufferHeight, float zoom);
void clampCamera(Camera& camera);
void cameraScrollCallback(GLFWwindow* window, double xOffset, double yOffset);

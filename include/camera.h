#pragma once

#include "simulation.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>

// Zoom limits and increments; reference dimensions keep scale consistent across boards.
constexpr float MinZoom = 0.5F;
constexpr float MaxZoom = 8.0F;
constexpr float ZoomStep = 1.2F;
constexpr float ReferenceGridColumns = 256.0F;
constexpr float ReferenceGridRows = 192.0F;

// Camera position, magnification, and accumulated scroll input in grid space.
struct Camera {
    float centerX = GridColumns * 0.5F;
    float centerY = GridRows * 0.5F;
    float zoom = 1.0F;
    double scrollDelta = 0.0;
};

// Dimensions of the grid-space rectangle visible through the camera.
struct ViewSize {
    float width;
    float height;
};

// Computes the visible grid-space dimensions for a framebuffer and zoom level.
// @param framebufferWidth Framebuffer width in pixels.
// @param framebufferHeight Framebuffer height in pixels.
// @param zoom Current camera magnification.
// @return Width and height of the visible rectangle in grid units.
ViewSize getViewSize(int framebufferWidth, int framebufferHeight, float zoom);

// Returns the smallest zoom that still allows the entire board to fit onscreen.
// @param gridColumns Number of board columns.
// @param gridRows Number of board rows.
// @return Minimum zoom permitted for this board's dimensions.
float getMinimumZoom(int gridColumns, int gridRows);

// Constrains the camera center to the finite board's bounds.
// @param camera Camera state to update.
// @param gridColumns Number of board columns.
// @param gridRows Number of board rows.
// @return No value; updates the camera center in place.
void clampCamera(Camera& camera, int gridColumns, int gridRows);

// Changes zoom while keeping the grid point under the cursor stationary when possible.
// @param camera Camera state to update.
// @param newZoom Magnification to apply.
// @param gridColumns Number of board columns.
// @param gridRows Number of board rows.
// @param framebufferWidth Framebuffer width in pixels.
// @param framebufferHeight Framebuffer height in pixels.
// @param windowWidth Window width in screen coordinates.
// @param windowHeight Window height in screen coordinates.
// @param cursorX Cursor horizontal position in window coordinates.
// @param cursorY Cursor vertical position in window coordinates.
// @return No value; updates the camera zoom and center in place.
void zoomCameraAtCursor(Camera& camera, float newZoom,
                        int gridColumns, int gridRows,
                        int framebufferWidth, int framebufferHeight,
                        int windowWidth, int windowHeight,
                        double cursorX, double cursorY);

// Accumulates wheel input on the Camera stored as the GLFW window user pointer.
// @param window GLFW window whose user pointer refers to a Camera.
// @param xOffset Horizontal wheel offset, unused by this camera.
// @param yOffset Vertical wheel offset to accumulate.
// @return No value; adds yOffset to the camera's pending scroll delta.
void cameraScrollCallback(GLFWwindow* window, double xOffset, double yOffset);

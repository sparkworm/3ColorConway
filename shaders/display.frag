#version 430 core

in vec2 textureCoordinate;
out vec4 fragmentColor;

// Unsigned-integer texture containing the RGB bit mask for each cell.
uniform usampler2D stateTexture;

// Grid dimensions in columns and rows.
uniform ivec2 gridSize;

// Top-left corner and dimensions of the grid-space camera view.
uniform vec2 viewOrigin;
uniform vec2 viewSize;

// Displays the RGB bits of the grid cell mapped to this framebuffer fragment.
// @return No value; writes the display color for this fragment.
void main() {
    // Map the viewport to the camera's grid-space rectangle, flipping Y to
    // preserve the convention that row zero is at the top of the window.
    vec2 gridCoordinate = viewOrigin +
        vec2(textureCoordinate.x, 1.0 - textureCoordinate.y) * viewSize;

    // The contrasting outside color makes the finite grid boundary visible.
    if (any(lessThan(gridCoordinate, vec2(0.0))) ||
        any(greaterThanEqual(gridCoordinate, vec2(gridSize)))) {
        fragmentColor = vec4(0.12, 0.20, 0.30, 1.0);
        return;
    }

    // Integer texel coordinates identify the cell under this fragment.
    ivec2 cell = ivec2(gridCoordinate);
    uint state = texelFetch(stateTexture, cell, 0).r;

    // Perform bitwise logic to extract the colors
    vec3 color = vec3(
        float(state & 1u),
        float((state >> 1u) & 1u),
        float((state >> 2u) & 1u));
    fragmentColor = vec4(color, 1.0);
}

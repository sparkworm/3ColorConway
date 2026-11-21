#version 430 core

in vec2 textureCoordinate;
out vec4 fragmentColor;

// CPU-bound unsigned-integer texture containing the two-state cell grid.
uniform usampler2D stateTexture;

// Grid dimensions in columns and rows.
uniform ivec2 gridSize;

// Top-left corner and dimensions of the grid-space camera view.
uniform vec2 viewOrigin;
uniform vec2 viewSize;

// The value used for live/white cells in the simulation data.
uniform uint aliveState;

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

    // Live cells are white; dead cells are black.
    if (state == aliveState) {
        fragmentColor = vec4(1.0);
    } else {
        fragmentColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}

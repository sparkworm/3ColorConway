#version 430 core

in vec2 textureCoordinate;
out vec4 fragmentColor;

// CPU-bound unsigned-integer texture containing the two-state cell grid.
uniform usampler2D stateTexture;

// Grid dimensions in columns and rows.
uniform ivec2 gridSize;

// The value used for live/white cells in the simulation data.
uniform uint aliveState;

void main() {
    // Flip the vertical axis to match the CPU grid convention: row zero is
    // treated as the bottom of the window.
    vec2 gridCoordinate = vec2(textureCoordinate.x, 1.0 - textureCoordinate.y)
                        * vec2(gridSize);

    // Integer texel coordinates identify the cell under this fragment. Clamping
    // also keeps edge fragments from sampling outside the texture.
    ivec2 cell = min(ivec2(gridCoordinate), gridSize - ivec2(1));
    uint state = texelFetch(stateTexture, cell, 0).r;

    // Live cells are white; dead cells are black.
    if (state == aliveState) {
        fragmentColor = vec4(1.0);
    } else {
        fragmentColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}

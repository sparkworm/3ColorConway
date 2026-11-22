#version 430 core

// Texture coordinates for display.frag; interpolated across the triangle.
out vec2 textureCoordinate;

// Generates the full-screen display triangle and its texture coordinates.
// @return No value; writes this vertex's clip position and texture coordinate.
void main() {
    // Generate a single oversized triangle from gl_VertexID, so no vertex buffer
    // or attribute setup is needed. It covers every pixel in the viewport.
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );

    vec2 position = positions[gl_VertexID];
    gl_Position = vec4(position, 0.0, 1.0);

    // Convert clip-space coordinates to the [0, 1] texture-coordinate range.
    textureCoordinate = position * 0.5 + 0.5;
}

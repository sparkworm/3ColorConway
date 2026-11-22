# 3ColorConway

## Build on Arch Linux

Install the compiler, CMake, GLFW, and Mesa OpenGL development files:

```sh
sudo pacman -S --needed base-devel cmake glfw mesa
```

Configure and build from the project directory:

```sh
cmake -S . -B build
cmake --build build
./build/3ColorConway
```

CMake fetches GLAD and Dear ImGui the first time it configures the project, so
that step needs an internet connection. The graphics driver must support OpenGL
4.3.

## Controls

- Left-drag paints cells. The first cell determines whether the drag paints
  alive (`7`) or dead (`0`).
- Mouse-wheel scrolling or `+`/`-` zooms in and out; zoom ranges from 0.5x to 8x.
- Arrow keys or `W`/`A`/`S`/`D` pan the view. The camera stays centered over the
  grid, and the blue-gray area outside the grid marks its edges.
- Use the left-side parameter panel to adjust the four rule thresholds. Values
  range from 0 to 8. The defaults are `1`, `3`, `3`, and `4`, respectively.
  Death thresholds are inclusive; birth occurs inside the inclusive min/max
  range, and live cells survive between the two death thresholds. The panel
  warns when the birth or survival bounds leave no possible neighbor counts.
- Space pauses or resumes the simulation.
- Escape exits.

The grid is stored as two `R32UI` textures on the GPU. `shaders/update.comp`
applies the adjustable Game of Life rules (off-grid neighbors count as dead), and the
textures swap after each generation. `shaders/paint.comp` handles mouse edits;
`shaders/display.vert` and `shaders/display.frag` draw the current grid.
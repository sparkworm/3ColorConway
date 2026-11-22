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

- Choose a paint color in the left panel, then left-drag to paint or erase it.
  Cells store red, green, and blue as three independent bits (`0` is empty;
  `7` is white).
- Mouse-wheel scrolling or `+`/`-` zooms in and out; zoom ranges from 0.5x to 8x.
- Arrow keys or `W`/`A`/`S`/`D` pan the view. The camera stays centered over the
  grid, and the blue-gray area outside the grid marks its edges.
- Use the left-side panel to configure each output channel independently.
  Each channel has integer weights from -2 to 2 for red, green, and blue
  neighbor counts, plus inclusive birth and survival score ranges from -48 to
  48. A channel is evaluated from its own weighted score; the other channel
  bits in the cell evolve independently. The panel shows how many of the 729
  possible red/green/blue neighbor-count combinations that match each range.
  Conway's preset uses a weight of 1 for the channel itself, 0 for the others,
  birth score 3, and survival scores 2-3, reproducing Conway's Life when cells
  are empty or white.
- Space pauses or resumes the simulation.
- Escape exits.

The grid is stored as two `R32UI` textures on the GPU. `shaders/update.comp`
applies the adjustable per-channel rules (off-grid neighbors count as empty),
and the textures swap after each generation. `shaders/paint.comp` handles
mouse edits; `shaders/display.vert` and `shaders/display.frag` render the RGB
bit masks as colors.
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

- On startup, choose **Start Conway** to begin with the default Conway rules
  and an adjustable board width and height, or load an existing `.3ccb` board
  with **Load board + rules**.
- Choose a paint color in the left panel, then left-drag to paint or erase it.
  Cells store red, green, and blue as three independent bits (`0` is empty;
  `7` is white).
- Mouse-wheel scrolling or `+`/`-` zooms in and out, with a maximum of 8x.
  The minimum automatically lowers for larger boards so the full board remains
  visible. Zooming keeps the grid point under the mouse pointer stationary.
  Zoom levels use a fixed reference grid, so magnification is consistent across
  board sizes.
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
- Expand **Save / load files** in the control panel to save or load rule
  configurations (`.json`) independently, or save/load a board together with
  its associated rules (`.3ccb`). Loading a board validates the file before
  applying it and pauses the simulation; press Space to resume.
- Escape returns to the main menu; press Escape again from the main menu to
  exit the application.

The grid is stored as two `R32UI` textures on the GPU. `shaders/update.comp`
applies the adjustable per-channel rules (off-grid neighbors count as empty),
and the textures swap after each generation. `shaders/paint.comp` handles
mouse edits; `shaders/display.vert` and `shaders/display.frag` render the RGB
bit masks as colors. Board files use a versioned binary format with dimensions
and rules in the header, followed by row-major three-bit cell states. Boards
of different dimensions are supported.
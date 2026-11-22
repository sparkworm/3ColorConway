#pragma once

#include "simulation.h"

#include <imgui.h>

#include <array>
#include <string>

// Actions the control panel can request from the application loop.
enum class FileAction {
    None,
    ResetBoard,
    SaveConfiguration,
    LoadConfiguration,
    SaveBoard,
    LoadBoard,
};

// Per-frame state and requested operation returned by the rule/file panel.
struct ControlPanelResult {
    bool captureMouse = false;
    bool captureKeyboard = false;
    FileAction fileAction = FileAction::None;
    std::string filePath;
};

// User selections returned by the startup menu.
struct MainMenuResult {
    bool startDefault = false;
    bool loadBoard = false;
    bool quit = false;
    bool captureMouse = false;
    bool captureKeyboard = false;
    int width = 256;
    int height = 192;
    std::array<char, 512> boardPath{};
};

// Pause-button interaction and its mouse-capture state for this frame.
struct PauseButtonResult {
    bool toggled = false;
    bool captureMouse = false;
};

// Draws the new-board/load/quit menu and reports selected actions.
// @return Menu selections and ImGui input-capture state for this frame.
MainMenuResult drawMainMenu();

// Draws the pause control and reports whether its state was toggled.
// @param paused Current simulation pause state, used to choose the button label.
// @return Whether the button toggled and whether the UI captures the mouse.
PauseButtonResult drawPauseButton(bool paused);

// Draws rule, paint, reset, and persistence controls, returning requested actions.
// @param rules Editable channel rules, modified by the rule controls.
// @param paintColorIndex Selected paint-color index, modified by the color picker.
// @param configurationPath Editable path buffer for rule configuration files.
// @param boardPath Editable path buffer for board files.
// @param fileStatus Current save/load status text to display.
// @param fileStatusIsError Whether fileStatus should be rendered as an error.
// @return Requested file/reset action and ImGui input-capture state.
ControlPanelResult drawControlPanel(
    RuleSet& rules, int& paintColorIndex,
    std::array<char, 512>& configurationPath,
    std::array<char, 512>& boardPath, const std::string& fileStatus,
    bool fileStatusIsError);

// Maps a color-picker index to its three-bit RGB cell state.
// @param paintColorIndex Index into the color picker's supported paint colors.
// @return Three-bit mask representing the selected color.
unsigned int getPaintState(int paintColorIndex);

#pragma once

#include "simulation.h"

#include <imgui.h>

#include <array>
#include <string>

enum class FileAction {
    None,
    ResetBoard,
    SaveConfiguration,
    LoadConfiguration,
    SaveBoard,
    LoadBoard,
};

struct ControlPanelResult {
    bool captureMouse = false;
    bool captureKeyboard = false;
    FileAction fileAction = FileAction::None;
    std::string filePath;
};

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

struct PauseButtonResult {
    bool toggled = false;
    bool captureMouse = false;
};

MainMenuResult drawMainMenu();
PauseButtonResult drawPauseButton(bool paused);

ControlPanelResult drawControlPanel(
    RuleSet& rules, int& paintColorIndex,
    std::array<char, 512>& configurationPath,
    std::array<char, 512>& boardPath, const std::string& fileStatus,
    bool fileStatusIsError);
unsigned int getPaintState(int paintColorIndex);

#pragma once

#include "simulation.h"

#include <imgui.h>

#include <array>
#include <string>

enum class FileAction {
    None,
    SaveConfiguration,
    LoadConfiguration,
    SaveBoard,
    LoadBoard,
};

struct ControlPanelResult {
    bool captureMouse = false;
    FileAction fileAction = FileAction::None;
    std::string filePath;
};

ControlPanelResult drawControlPanel(
    RuleSet& rules, int& paintColorIndex,
    std::array<char, 512>& configurationPath,
    std::array<char, 512>& boardPath, const std::string& fileStatus,
    bool fileStatusIsError);
unsigned int getPaintState(int paintColorIndex);

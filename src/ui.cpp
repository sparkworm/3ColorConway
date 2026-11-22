#include "ui.h"

#include <algorithm>
#include <array>
#include <string>

namespace {
constexpr std::array<const char*, 3> channelNames{"Red", "Green", "Blue"};
constexpr std::array<const char*, 7> paintColorNames{
    "Red", "Green", "Blue", "Yellow", "Magenta", "Cyan", "White"};
constexpr std::array<unsigned int, 7> paintColorStates{
    0b001, 0b010, 0b100, 0b011, 0b101, 0b110, WhiteState};
}

MainMenuResult drawMainMenu() {
    MainMenuResult result;
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    ImGui::Begin("3ColorConway", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::SetCursorPosY(140.0F);
    ImGui::TextUnformatted("New project");
    ImGui::Separator();
    static int width = 256;
    static int height = 192;
    ImGui::InputInt("Width", &width, 0, 0);
    ImGui::InputInt("Height", &height, 0, 0);
    width = std::clamp(width, 1, 16384);
    height = std::clamp(height, 1, 16384);
    result.width = width;
    result.height = height;
    if (ImGui::Button("Start Conway")) {
        result.startDefault = true;
    }

    ImGui::Dummy(ImVec2(0.0F, 30.0F));
    ImGui::TextUnformatted("Existing project");
    ImGui::Separator();
    static std::array<char, 512> boardPath{};
    if (boardPath[0] == '\0') {
        const char defaultPath[] = "board.3ccb";
        std::copy(defaultPath, defaultPath + sizeof(defaultPath),
                  boardPath.begin());
    }
    ImGui::InputText("Board file", boardPath.data(), boardPath.size());
    if (ImGui::Button("Load board + rules")) {
        result.loadBoard = true;
    }
    ImGui::Dummy(ImVec2(0.0F, 30.0F));
    if (ImGui::Button("Quit")) {
        result.quit = true;
    }
    result.boardPath = boardPath;
    result.captureMouse = ImGui::GetIO().WantCaptureMouse;
    result.captureKeyboard = ImGui::GetIO().WantCaptureKeyboard;

    ImGui::End();
    return result;
}

unsigned int getPaintState(int paintColorIndex) {
    return paintColorStates[paintColorIndex];
}

ControlPanelResult drawControlPanel(
    RuleSet& rules, int& paintColorIndex,
    std::array<char, 512>& configurationPath,
    std::array<char, 512>& boardPath, const std::string& fileStatus,
    bool fileStatusIsError) {
    ControlPanelResult result;
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float panelWidth = std::min(420.0F, std::max(240.0F, displaySize.x - 24.0F));
    const float panelHeight = std::max(200.0F, displaySize.y - 24.0F);
    ImGui::SetNextWindowPos(ImVec2(12.0F, 12.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
    ImGui::Begin("Color Life rules", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Paint color");
    ImGui::Combo("##paintColor", &paintColorIndex, paintColorNames.data(),
                 static_cast<int>(paintColorNames.size()));
    ImGui::TextWrapped("Left-drag paints the selected color; dragging on it erases it.");
    ImGui::Separator();
    
    if (ImGui::Button("Reset board")) {
        result.fileAction = FileAction::ResetBoard;
    }
    ImGui::SameLine();
    if (ImGui::Button("Conway preset")) {
        rules = conwayRules();
    }

    ImGui::Separator();

    ImGui::TextWrapped(
        "Each output channel scores red, green, and blue neighbor counts with integer weights (-2 to 2).");
    ImGui::TextDisabled("Each count is 0-8; score thresholds stay in -48 to 48.");
    const char* sourceNames[] = {"Red neighbors", "Green neighbors", "Blue neighbors"};
    for (int channel = 0; channel < 3; ++channel) {
        ChannelRule& rule = rules[channel];
        ImGui::PushID(channel);
        if (ImGui::CollapsingHeader((std::string(channelNames[channel]) +
                                     " output rule").c_str(),
                                    channel == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            ImGui::TextUnformatted("Source weights");
            for (int source = 0; source < 3; ++source) {
                ImGui::SliderInt(sourceNames[source], &rule.weights[source],
                                 MinWeight, MaxWeight);
            }
            ImGui::TextUnformatted("Birth score range");
            ImGui::SliderInt("Birth min", &rule.birthMin, -MaxRuleScore, MaxRuleScore);
            ImGui::SliderInt("Birth max", &rule.birthMax, -MaxRuleScore, MaxRuleScore);
            ImGui::TextUnformatted("Survival score range");
            ImGui::SliderInt("Survival min", &rule.survivalMin,
                             -MaxRuleScore, MaxRuleScore);
            ImGui::SliderInt("Survival max", &rule.survivalMax,
                             -MaxRuleScore, MaxRuleScore);
            const int birthMatches =
                countMatchingNeighborhoods(rule, rule.birthMin, rule.birthMax);
            const int survivalMatches =
                countMatchingNeighborhoods(rule, rule.survivalMin, rule.survivalMax);
            ImGui::Text("Birth: %d / 729 count patterns", birthMatches);
            ImGui::Text("Survival: %d / 729 count patterns", survivalMatches);
            if (rule.birthMin > rule.birthMax) {
                ImGui::TextColored(ImVec4(1.0F, 0.65F, 0.2F, 1.0F),
                                   "Birth range is empty.");
            }
            if (rule.survivalMin > rule.survivalMax) {
                ImGui::TextColored(ImVec4(1.0F, 0.65F, 0.2F, 1.0F),
                                   "Survival range is empty.");
            }
        }
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Save / load files")) {
        ImGui::InputText("Configuration file", configurationPath.data(),
                         configurationPath.size());
        if (ImGui::Button("Save configuration")) {
            result.fileAction = FileAction::SaveConfiguration;
            result.filePath = configurationPath.data();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load configuration")) {
            result.fileAction = FileAction::LoadConfiguration;
            result.filePath = configurationPath.data();
        }

        ImGui::InputText("Board file", boardPath.data(), boardPath.size());
        if (ImGui::Button("Save board + rules")) {
            result.fileAction = FileAction::SaveBoard;
            result.filePath = boardPath.data();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load board + rules")) {
            result.fileAction = FileAction::LoadBoard;
            result.filePath = boardPath.data();
        }
        if (!fileStatus.empty()) {
            if (fileStatusIsError) {
                ImGui::TextWrapped("%s", fileStatus.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.55F, 0.9F, 0.55F, 1.0F), "%s",
                                   fileStatus.c_str());
            }
        }
    }
    ImGui::End();
    result.captureMouse = ImGui::GetIO().WantCaptureMouse;
    result.captureKeyboard = ImGui::GetIO().WantCaptureKeyboard;
    return result;
}

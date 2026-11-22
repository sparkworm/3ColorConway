#include "ui.h"

#include <array>
#include <string>

namespace {
constexpr std::array<const char*, 3> channelNames{"Red", "Green", "Blue"};
constexpr std::array<const char*, 7> paintColorNames{
    "Red", "Green", "Blue", "Yellow", "Magenta", "Cyan", "White"};
constexpr std::array<unsigned int, 7> paintColorStates{
    0b001, 0b010, 0b100, 0b011, 0b101, 0b110, WhiteState};
}

unsigned int getPaintState(int paintColorIndex) {
    return paintColorStates[paintColorIndex];
}

bool drawControlPanel(RuleSet& rules, int& paintColorIndex) {
    ImGui::SetNextWindowPos(ImVec2(12.0F, 12.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(420.0F, 570.0F), ImGuiCond_Always);
    ImGui::Begin("Color Life rules", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Paint color");
    ImGui::Combo("##paintColor", &paintColorIndex, paintColorNames.data(),
                 static_cast<int>(paintColorNames.size()));
    ImGui::TextWrapped("Left-drag paints the selected color; dragging on it erases it.");
    ImGui::Separator();
    if (ImGui::Button("Conway preset")) {
        rules = conwayRules();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Independent B3/S23 channels");
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
    ImGui::End();
    return ImGui::GetIO().WantCaptureMouse;
}

#pragma once

#include <array>
#include <chrono>

constexpr int GridColumns = 256;
constexpr int GridRows = 192;
constexpr int MinWeight = -2;
constexpr int MaxWeight = 2;
constexpr int MaxRuleScore = 8 * 3 * MaxWeight;
constexpr unsigned int DeadState = 0;
constexpr unsigned int WhiteState = 0b0111;
constexpr auto SimulationStep = std::chrono::milliseconds(100);

struct ChannelRule {
    std::array<int, 3> weights{};
    int birthMin = 3;
    int birthMax = 3;
    int survivalMin = 2;
    int survivalMax = 3;
};

using RuleSet = std::array<ChannelRule, 3>;

RuleSet conwayRules();
int countMatchingNeighborhoods(const ChannelRule& rule, int minimum, int maximum);

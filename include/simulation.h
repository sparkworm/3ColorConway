#pragma once

#include <array>
#include <chrono>

// Default dimensions used when creating a Conway board without custom sizing.
constexpr int GridColumns = 256;
constexpr int GridRows = 192;
// Supported per-neighbor weights and the largest possible weighted score.
constexpr int MinWeight = -2;
constexpr int MaxWeight = 2;
constexpr int MaxRuleScore = 8 * 3 * MaxWeight;
// Cell states are three independent RGB bits; zero is empty and seven is white.
constexpr unsigned int DeadState = 0;
constexpr unsigned int WhiteState = 0b0111;
// Time interval between simulation generations while running.
constexpr auto SimulationStep = std::chrono::milliseconds(100);

// Defines weighted birth and survival score ranges for one output channel.
struct ChannelRule {
    std::array<int, 3> weights{};
    int birthMin = 3;
    int birthMax = 3;
    int survivalMin = 2;
    int survivalMax = 3;
};

// Rules for red, green, and blue output channels, in that order.
using RuleSet = std::array<ChannelRule, 3>;

// Builds independent Conway Life rules for the three output channels.
// @return Rule set with each channel responding only to its own neighbors.
RuleSet conwayRules();

// Counts the RGB neighbor-count triples whose weighted score is in an inclusive range.
// @param rule Channel weights used to compute each triple's score.
// @param minimum Inclusive lower bound of the accepted score range.
// @param maximum Inclusive upper bound of the accepted score range.
// @return Number of matching triples from the 729 possible combinations.
int countMatchingNeighborhoods(const ChannelRule& rule, int minimum, int maximum);

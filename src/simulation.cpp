#include "simulation.h"

RuleSet conwayRules() {
    RuleSet rules{};
    for (int channel = 0; channel < 3; ++channel) {
        rules[channel].weights[channel] = 1;
    }
    return rules;
}

int countMatchingNeighborhoods(const ChannelRule& rule, int minimum, int maximum) {
    int matches = 0;
    for (int red = 0; red <= 8; ++red) {
        for (int green = 0; green <= 8; ++green) {
            for (int blue = 0; blue <= 8; ++blue) {
                const int score = rule.weights[0] * red +
                                  rule.weights[1] * green +
                                  rule.weights[2] * blue;
                if (score >= minimum && score <= maximum) {
                    ++matches;
                }
            }
        }
    }
    return matches;
}

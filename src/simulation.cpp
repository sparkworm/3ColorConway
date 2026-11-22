#include "simulation.h"

// Produces the standard Conway rule independently for each color channel.
// @return Rule set where each output channel responds only to its matching channel.
RuleSet conwayRules() {
    RuleSet rules{};
    for (int channel = 0; channel < 3; ++channel) {
        rules[channel].weights[channel] = 1;
    }
    return rules;
}

// Enumerates all 9^3 possible RGB neighbor-count triples for the given score range.
// @param rule Weights used to compute each triple's score.
// @param minimum Inclusive lower bound of the accepted score range.
// @param maximum Inclusive upper bound of the accepted score range.
// @return Number of neighbor-count triples whose score is within the range.
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

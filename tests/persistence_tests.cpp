#include "persistence.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

bool sameRules(const RuleSet& first, const RuleSet& second) {
    for (std::size_t channel = 0; channel < first.size(); ++channel) {
        if (first[channel].weights != second[channel].weights ||
            first[channel].birthMin != second[channel].birthMin ||
            first[channel].birthMax != second[channel].birthMax ||
            first[channel].survivalMin != second[channel].survivalMin ||
            first[channel].survivalMax != second[channel].survivalMax) {
            return false;
        }
    }
    return true;
}
}

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "3colorconway-persistence-tests";
    std::filesystem::create_directories(directory);
    const std::filesystem::path configurationPath = directory / "rules.json";
    const std::filesystem::path boardPath = directory / "board.3ccb";
    const std::filesystem::path malformedPath = directory / "malformed.json";
    const std::filesystem::path truncatedPath = directory / "truncated.3ccb";

    RuleSet expectedRules = conwayRules();
    expectedRules[0].weights = {-2, 1, 2};
    expectedRules[0].birthMin = -48;
    expectedRules[0].birthMax = 48;
    expectedRules[1].weights = {2, -1, 0};
    expectedRules[1].survivalMin = 5;
    expectedRules[1].survivalMax = 2;
    expectedRules[2].weights = {0, 2, -2};
    expectedRules[2].birthMin = 0;
    expectedRules[2].birthMax = 0;

    RuleSet loadedRules{};
    std::string error;
    if (!require(saveConfiguration(configurationPath.string(), expectedRules,
                                   error),
                 "Could not save configuration: ") ||
        !require(loadConfiguration(configurationPath.string(), loadedRules,
                                   error),
                 "Could not load configuration: ") ||
        !require(sameRules(loadedRules, expectedRules),
                 "Configuration rules did not round-trip.")) {
        return 1;
    }

    std::vector<unsigned int> expectedCells(
        static_cast<std::size_t>(GridColumns) * GridRows);
    for (std::size_t index = 0; index < expectedCells.size(); ++index) {
        expectedCells[index] = static_cast<unsigned int>(index % 8);
    }
    if (!require(saveBoard(boardPath.string(), expectedCells, expectedRules,
                           error),
                 "Could not save board: ")) {
        return 1;
    }
    const std::uintmax_t expectedSize =
        51 + (static_cast<std::uintmax_t>(GridColumns) * GridRows * 3 + 7) / 8;
    if (!require(std::filesystem::file_size(boardPath) == expectedSize,
                 "Board file is not the expected packed size.")) {
        return 1;
    }

    std::vector<unsigned int> loadedCells;
    loadedRules = conwayRules();
    if (!require(loadBoard(boardPath.string(), loadedCells, loadedRules, error),
                 "Could not load board: ") ||
        !require(loadedCells == expectedCells,
                 "Board cell states did not round-trip.") ||
        !require(sameRules(loadedRules, expectedRules),
                 "Board-associated rules did not round-trip.")) {
        return 1;
    }

    const std::filesystem::path dynamicBoardPath = directory / "dynamic.3ccb";
    const int dynamicWidth = 17;
    const int dynamicHeight = 11;
    std::vector<unsigned int> dynamicCells(
        static_cast<std::size_t>(dynamicWidth) * dynamicHeight);
    for (std::size_t index = 0; index < dynamicCells.size(); ++index) {
        dynamicCells[index] = static_cast<unsigned int>(index % 8);
    }
    if (!require(saveBoard(dynamicBoardPath.string(), dynamicCells,
                           expectedRules, dynamicWidth, dynamicHeight, error),
                 "Could not save dynamic board: ")) {
        return 1;
    }
    std::vector<unsigned int> loadedDynamicCells;
    int loadedWidth = 0;
    int loadedHeight = 0;
    loadedRules = conwayRules();
    if (!require(loadBoard(dynamicBoardPath.string(), loadedDynamicCells,
                           loadedRules, loadedWidth, loadedHeight, error),
                 "Could not load dynamic board: ") ||
        !require(loadedWidth == dynamicWidth && loadedHeight == dynamicHeight,
                 "Dynamic board dimensions did not round-trip.") ||
        !require(loadedDynamicCells == dynamicCells,
                 "Dynamic board cells did not round-trip.")) {
        return 1;
    }

    {
        std::ofstream malformed(malformedPath);
        malformed << R"({"format":"3colorconway-rules","version":1,"channels":[]})";
    }
    RuleSet preservedRules = conwayRules();
    const RuleSet originalRules = preservedRules;
    if (!require(!loadConfiguration(malformedPath.string(), preservedRules,
                                    error),
                 "Malformed configuration unexpectedly loaded.") ||
        !require(sameRules(preservedRules, originalRules),
                 "Failed configuration load changed active rules.")) {
        return 1;
    }

    {
        std::ifstream source(boardPath, std::ios::binary);
        std::ofstream truncated(truncatedPath, std::ios::binary);
        char byte = 0;
        source.read(&byte, 1);
        truncated.write(&byte, 1);
    }
    std::vector<unsigned int> preservedCells{1, 2, 3};
    const std::vector<unsigned int> originalCells = preservedCells;
    preservedRules = originalRules;
    if (!require(!loadBoard(truncatedPath.string(), preservedCells,
                            preservedRules, error),
                 "Truncated board unexpectedly loaded.") ||
        !require(preservedCells == originalCells &&
                     sameRules(preservedRules, originalRules),
                 "Failed board load changed active state.")) {
        return 1;
    }

    std::filesystem::remove(configurationPath);
    std::filesystem::remove(boardPath);
    std::filesystem::remove(dynamicBoardPath);
    std::filesystem::remove(malformedPath);
    std::filesystem::remove(truncatedPath);
    std::filesystem::remove(directory);
    std::cout << "Persistence round-trip tests passed.\n";
    return 0;
}

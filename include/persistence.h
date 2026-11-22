#pragma once

#include "simulation.h"

#include <string>
#include <vector>

// Saves a validated rule set as versioned JSON.
// @param path Destination JSON file path.
// @param rules Rule set to validate and serialize.
// @param error Receives a failure reason; cleared when the save succeeds.
// @return true when the file is written, otherwise false.
bool saveConfiguration(const std::string& path, const RuleSet& rules,
                       std::string& error);

// Loads and validates a rule set without changing rules when loading fails.
// @param path Source JSON file path.
// @param rules Receives the loaded rules on success.
// @param error Receives a failure reason; cleared when the load succeeds.
// @return true when a supported configuration is loaded, otherwise false.
bool loadConfiguration(const std::string& path, RuleSet& rules,
                       std::string& error);

// Saves a board using the default dimensions together with its rules.
// @param path Destination board file path.
// @param cells Row-major cells encoded as three-bit RGB masks.
// @param rules Rule set to store with the board.
// @param error Receives a failure reason; cleared when the save succeeds.
// @return true when the board is written, otherwise false.
bool saveBoard(const std::string& path,
               const std::vector<unsigned int>& cells, const RuleSet& rules,
               std::string& error);

// Saves a dimensioned board and its rules in the versioned binary format.
// @param path Destination board file path.
// @param cells Row-major cells encoded as three-bit RGB masks.
// @param rules Rule set to store with the board.
// @param width Board width in columns.
// @param height Board height in rows.
// @param error Receives a failure reason; cleared when the save succeeds.
// @return true when the board is written, otherwise false.
bool saveBoard(const std::string& path,
               const std::vector<unsigned int>& cells, const RuleSet& rules,
               int width, int height, std::string& error);

// Loads a board only if it matches the default dimensions; outputs are unchanged on failure.
// @param path Source board file path.
// @param cells Receives row-major decoded cells on success.
// @param rules Receives the associated rules on success.
// @param error Receives a failure reason; cleared when the load succeeds.
// @return true when the board is loaded and matches the default dimensions.
bool loadBoard(const std::string& path, std::vector<unsigned int>& cells,
               RuleSet& rules, std::string& error);

// Loads board data, rules, and stored dimensions; outputs change only on success.
// @param path Source board file path.
// @param cells Receives row-major decoded cells on success.
// @param rules Receives the associated rules on success.
// @param width Receives the stored board width in columns on success.
// @param height Receives the stored board height in rows on success.
// @param error Receives a failure reason; cleared when the load succeeds.
// @return true when the complete board is loaded, otherwise false.
bool loadBoard(const std::string& path, std::vector<unsigned int>& cells,
               RuleSet& rules, int& width, int& height, std::string& error);

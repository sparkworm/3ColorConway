#pragma once

#include "simulation.h"

#include <string>
#include <vector>

bool saveConfiguration(const std::string& path, const RuleSet& rules,
                       std::string& error);
bool loadConfiguration(const std::string& path, RuleSet& rules,
                       std::string& error);
bool saveBoard(const std::string& path,
               const std::vector<unsigned int>& cells, const RuleSet& rules,
               std::string& error);
bool saveBoard(const std::string& path,
               const std::vector<unsigned int>& cells, const RuleSet& rules,
               int width, int height, std::string& error);
bool loadBoard(const std::string& path, std::vector<unsigned int>& cells,
               RuleSet& rules, std::string& error);
bool loadBoard(const std::string& path, std::vector<unsigned int>& cells,
               RuleSet& rules, int& width, int& height, std::string& error);

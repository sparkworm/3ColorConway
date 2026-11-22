#pragma once

#include "simulation.h"

#include <imgui.h>

bool drawControlPanel(RuleSet& rules, int& paintColorIndex);
unsigned int getPaintState(int paintColorIndex);

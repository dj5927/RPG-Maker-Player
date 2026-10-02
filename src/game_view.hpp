#pragma once

#include "catalog.hpp"
#include "game_scanner.hpp"

#include <string>
#include <vector>

std::vector<GameInfo> buildGameView(const std::vector<GameInfo>& allGames,
                                    const LauncherCatalog& catalog,
                                    const std::string& engineFilter,
                                    const std::string& sortMode,
                                    const std::string& searchQuery = {});

int gameEngineOrder(RgssEngine engine);


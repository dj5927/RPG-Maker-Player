#pragma once

#include "game_scanner.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct LauncherCatalog {
  std::map<std::string, std::string> displayNames;
  std::filesystem::path listPath;
  std::filesystem::path imageRoot;
  bool valid = true;
};

LauncherCatalog prepareCatalog(const std::filesystem::path& gameRoot,
                               const std::vector<GameInfo>& games,
                               std::string& diagnostic);
std::string displayNameFor(const GameInfo& game, const LauncherCatalog& catalog);
bool setDisplayName(const GameInfo& game,
                    LauncherCatalog& catalog,
                    const std::string& displayName,
                    std::string& diagnostic);

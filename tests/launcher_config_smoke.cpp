#include "launcher_config.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
  const fs::path root = fs::temp_directory_path() / "mkxp_launcher_config_smoke";
  const fs::path games = root / "외부 게임 폴더";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(games, ec);
  if (ec) return 1;

  std::string diagnostic;
  auto missing = loadLauncherConfig(root, diagnostic);
  if (missing.hasGameRoot || diagnostic != "config missing") return 2;

  LauncherConfig config;
  config.gameRoot = games;
  config.hasGameRoot = true;
  config.engineFilter = "vxace";
  config.sortMode = "type";
  config.viewMode = "list";
  if (!saveLauncherConfig(root, config, diagnostic)) return 3;

  auto loaded = loadLauncherConfig(root, diagnostic);
  if (!loaded.hasGameRoot) return 4;
  if (fs::absolute(loaded.gameRoot).lexically_normal() != fs::absolute(games).lexically_normal()) return 5;
  if (loaded.engineFilter != "vxace" || loaded.sortMode != "type" || loaded.viewMode != "list") return 7;

  const fs::path catalogA = catalogStorageRoot(root, games);
  const fs::path catalogB = catalogStorageRoot(root, root / "another_games");
  if (catalogA == catalogB || catalogA.parent_path() != root / "config/catalogs") return 6;

  std::cout << "MKXP launcher config smoke PASS | root=" << loaded.gameRoot << '\n';
  fs::remove_all(root, ec);
  return 0;
}


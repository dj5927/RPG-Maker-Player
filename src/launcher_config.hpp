#pragma once

#include <filesystem>
#include <string>

struct LauncherConfig {
  std::filesystem::path gameRoot;
  bool hasGameRoot = false;
  std::string engineFilter = "all";
  std::string sortMode = "name";
  std::string viewMode = "grid";
};

LauncherConfig loadLauncherConfig(const std::filesystem::path& launcherRoot,
                                  std::string& diagnostic);
bool saveLauncherConfig(const std::filesystem::path& launcherRoot,
                        const LauncherConfig& config,
                        std::string& diagnostic);
std::filesystem::path catalogStorageRoot(const std::filesystem::path& launcherRoot,
                                         const std::filesystem::path& gameRoot);


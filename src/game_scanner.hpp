#pragma once

#include <filesystem>
#include <string>
#include <vector>

enum class RgssEngine {
  Unknown,
  EasyRPG,
  WolfRPG,
  XP,
  VX,
  VXAce,
  MV,
  MZ,
};

struct GameInfo {
  std::string folderName;
  std::filesystem::path path;
  std::filesystem::path scriptArchive;
  std::filesystem::path webRoot;
  std::filesystem::path easyRpgProjectPath;
  RgssEngine engine = RgssEngine::Unknown;
  std::string detectionSource;
  std::string rubyRuntime = "auto";
  std::string rubyDetectionSource;
  std::string nwjsRuntime = "auto";
  std::string nwjsDetectionSource;
};

bool isRgssEngine(RgssEngine engine);
bool isWebEngine(RgssEngine engine);
bool isEasyRpgEngine(RgssEngine engine);
bool isWolfRpgEngine(RgssEngine engine);
std::string engineLabel(RgssEngine engine);
GameInfo inspectGame(const std::filesystem::path& gamePath);
std::vector<GameInfo> scanGames(const std::filesystem::path& gameRoot);

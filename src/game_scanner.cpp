#include "game_scanner.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string lowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

std::optional<fs::path> findChildCaseInsensitive(const fs::path& parent,
                                                  const std::string& wanted,
                                                  bool directory) {
  std::error_code ec;
  if (!fs::is_directory(parent, ec)) return std::nullopt;
  const std::string target = lowerAscii(wanted);
  for (const auto& entry : fs::directory_iterator(parent, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    const bool typeOk = directory ? entry.is_directory(ec) : entry.is_regular_file(ec);
    if (ec || !typeOk) continue;
    if (lowerAscii(entry.path().filename().string()) == target) return entry.path();
  }
  return std::nullopt;
}

bool hasFileCI(const fs::path& parent, const std::string& name) {
  return findChildCaseInsensitive(parent, name, false).has_value();
}

std::string readSmallText(const fs::path& path, std::size_t maxBytes = 128 * 1024) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::string out;
  out.resize(maxBytes);
  in.read(out.data(), static_cast<std::streamsize>(out.size()));
  out.resize(static_cast<std::size_t>(in.gcount()));
  return out;
}

RgssEngine detectFromIni(const fs::path& gamePath) {
  const auto iniPath = findChildCaseInsensitive(gamePath, "Game.ini", false);
  if (!iniPath) return RgssEngine::Unknown;
  const std::string text = lowerAscii(readSmallText(*iniPath));
  if (text.find("rgss3") != std::string::npos || text.find("vxace") != std::string::npos) return RgssEngine::VXAce;
  if (text.find("rgss2") != std::string::npos) return RgssEngine::VX;
  if (text.find("rgss1") != std::string::npos) return RgssEngine::XP;
  return RgssEngine::Unknown;
}

RgssEngine detectFromArchive(const fs::path& gamePath) {
  std::error_code ec;
  if (!fs::is_directory(gamePath, ec)) return RgssEngine::Unknown;
  bool xp = false;
  bool vx = false;
  bool ace = false;
  for (const auto& entry : fs::directory_iterator(gamePath, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    const std::string ext = lowerAscii(entry.path().extension().string());
    if (ext == ".rgss3a") ace = true;
    else if (ext == ".rgss2a") vx = true;
    else if (ext == ".rgssad") xp = true;
  }
  if (ace) return RgssEngine::VXAce;
  if (vx) return RgssEngine::VX;
  if (xp) return RgssEngine::XP;
  return RgssEngine::Unknown;
}

RgssEngine detectFromScripts(const fs::path& gamePath, std::string& source, fs::path& archivePath) {
  const auto dataDir = findChildCaseInsensitive(gamePath, "Data", true);
  if (!dataDir) return RgssEngine::Unknown;

  const auto acePath = findChildCaseInsensitive(*dataDir, "Scripts.rvdata2", false);
  const auto vxPath = findChildCaseInsensitive(*dataDir, "Scripts.rvdata", false);
  const auto xpPath = findChildCaseInsensitive(*dataDir, "Scripts.rxdata", false);
  const bool ace = acePath.has_value();
  const bool vx = vxPath.has_value();
  const bool xp = xpPath.has_value();
  const int count = static_cast<int>(ace) + static_cast<int>(vx) + static_cast<int>(xp);
  if (count == 0) return RgssEngine::Unknown;
  if (count > 1) source = "scripts-conflict";
  else source = "scripts";

  // In a malformed/migrated folder with more than one Scripts archive,
  // prefer the newest data format. Game.ini remains diagnostic only here.
  if (ace) {
    archivePath = *acePath;
    return RgssEngine::VXAce;
  }
  if (vx) {
    archivePath = *vxPath;
    return RgssEngine::VX;
  }
  archivePath = *xpPath;
  return RgssEngine::XP;
}

RgssEngine detectWebEngine(const fs::path& gamePath, std::string& source, fs::path& webRoot) {
  const auto packageJson = findChildCaseInsensitive(gamePath, "package.json", false);
  if (!packageJson) return RgssEngine::Unknown;

  const auto wwwDir = findChildCaseInsensitive(gamePath, "www", true);
  if (wwwDir) {
    const auto jsDir = findChildCaseInsensitive(*wwwDir, "js", true);
    if (jsDir && hasFileCI(*jsDir, "plugins.js") && hasFileCI(*jsDir, "rpg_core.js")) {
      webRoot = *wwwDir;
      source = "mv:www/package.json+js/rpg_core.js";
      return RgssEngine::MV;
    }
  }

  const auto jsDir = findChildCaseInsensitive(gamePath, "js", true);
  const auto dataDir = findChildCaseInsensitive(gamePath, "data", true);
  if (jsDir && dataDir && hasFileCI(*jsDir, "plugins.js") && hasFileCI(*jsDir, "rmmz_core.js")) {
    webRoot = gamePath;
    source = "mz:package.json+js/rmmz_core.js";
    return RgssEngine::MZ;
  }

  return RgssEngine::Unknown;
}

RgssEngine detectEasyRpgEngine(const fs::path& gamePath, std::string& source, fs::path& projectPath) {
  if (hasFileCI(gamePath, "RPG_RT.ldb") && hasFileCI(gamePath, "RPG_RT.lmt")) {
    projectPath = gamePath;
    source = "easyrpg:rpg_rt.ldb+rpg_rt.lmt";
    return RgssEngine::EasyRPG;
  }

  // Standalone EasyRPG distribution: a wrapper folder containing one
  // .easyrpg or .zip project. Launcher-owned mkxp-*.txt files and RPG_RT.ini
  // are metadata, not additional game payload, so they must not break archive
  // detection after the user changes locale/font/SoundFont settings.
  std::error_code ec;
  std::vector<fs::path> meaningful;
  if (fs::is_directory(gamePath, ec)) {
    for (const auto& entry : fs::directory_iterator(gamePath, fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      const std::string name = entry.path().filename().string();
      if (!name.empty() && name[0] == '.') continue;
      const std::string lowerName = lowerAscii(name);
      if (lowerName.rfind("mkxp-", 0) == 0 && lowerAscii(entry.path().extension().string()) == ".txt") continue;
      if (lowerName == "rpg_rt.ini") continue;
      meaningful.push_back(entry.path());
    }
  }
  if (meaningful.size() == 1) {
    const fs::path candidate = meaningful.front();
    const std::string ext = lowerAscii(candidate.extension().string());
    if (ext == ".easyrpg" || ext == ".zip") {
      projectPath = candidate;
      source = ext == ".easyrpg" ? "easyrpg:standalone.easyrpg"
                                  : "easyrpg:standalone.zip";
      return RgssEngine::EasyRPG;
    }
  }
  return RgssEngine::Unknown;
}

RgssEngine detectWolfRpgEngine(const fs::path& gamePath, std::string& source) {
  const bool hasGame = hasFileCI(gamePath, "Game.exe");
  const bool hasGamePro = hasFileCI(gamePath, "GamePro.exe");
  if (!hasGame && !hasGamePro) return RgssEngine::Unknown;

  bool signature = hasFileCI(gamePath, "Config.exe") || hasFileCI(gamePath, "Game.ini");
  const auto dataDir = findChildCaseInsensitive(gamePath, "Data", true);
  if (dataDir) {
    const auto basicData = findChildCaseInsensitive(*dataDir, "BasicData", true);
    if (basicData) signature = true;
  }

  std::error_code ec;
  if (!signature && fs::is_directory(gamePath, ec)) {
    for (const auto& entry : fs::directory_iterator(gamePath, fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      if (!entry.is_regular_file(ec)) continue;
      if (lowerAscii(entry.path().extension().string()) == ".wolf") {
        signature = true;
        break;
      }
    }
  }

  if (!signature) return RgssEngine::Unknown;
  source = hasGamePro ? "wolf:gamepro.exe+signature" : "wolf:game.exe+signature";
  return RgssEngine::WolfRPG;
}

} // namespace

bool isRgssEngine(RgssEngine engine) {
  return engine == RgssEngine::XP || engine == RgssEngine::VX || engine == RgssEngine::VXAce;
}

bool isWebEngine(RgssEngine engine) {
  return engine == RgssEngine::MV || engine == RgssEngine::MZ;
}

bool isEasyRpgEngine(RgssEngine engine) {
  return engine == RgssEngine::EasyRPG;
}

bool isWolfRpgEngine(RgssEngine engine) {
  return engine == RgssEngine::WolfRPG;
}

std::string engineLabel(RgssEngine engine) {
  switch (engine) {
    case RgssEngine::EasyRPG: return "RPG 2000/2003";
    case RgssEngine::WolfRPG: return "WOLF RPG";
    case RgssEngine::XP: return "XP";
    case RgssEngine::VX: return "VX";
    case RgssEngine::VXAce: return "VX Ace";
    case RgssEngine::MV: return "MV";
    case RgssEngine::MZ: return "MZ";
    default: return "Unknown";
  }
}

GameInfo inspectGame(const fs::path& gamePath) {
  GameInfo info;
  info.path = gamePath;
  info.folderName = gamePath.filename().string();

  info.engine = detectWebEngine(gamePath, info.detectionSource, info.webRoot);
  if (info.engine != RgssEngine::Unknown) return info;

  info.engine = detectEasyRpgEngine(gamePath, info.detectionSource, info.easyRpgProjectPath);
  if (info.engine != RgssEngine::Unknown) return info;

  std::string scriptSource;
  info.engine = detectFromScripts(gamePath, scriptSource, info.scriptArchive);
  if (info.engine != RgssEngine::Unknown) {
    info.detectionSource = scriptSource;
    return info;
  }

  info.engine = detectFromArchive(gamePath);
  if (info.engine != RgssEngine::Unknown) {
    info.detectionSource = "archive";
    return info;
  }

  info.engine = detectFromIni(gamePath);
  if (info.engine != RgssEngine::Unknown) {
    info.detectionSource = "game.ini";
    return info;
  }

  // WOLF often ships Game.exe and may also have a Game.ini. RGSS games use the
  // same generic filenames, so only classify WOLF after all positive RGSS
  // signatures (Scripts archive, encrypted archive, RGSS Library= line) fail.
  info.engine = detectWolfRpgEngine(gamePath, info.detectionSource);
  if (info.engine != RgssEngine::Unknown) return info;

  info.detectionSource = "unknown";
  return info;
}

std::vector<GameInfo> scanGames(const fs::path& gameRoot) {
  std::vector<GameInfo> games;
  std::error_code ec;
  if (!fs::is_directory(gameRoot, ec)) return games;

  for (const auto& entry : fs::directory_iterator(gameRoot, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    if (!entry.is_directory(ec)) continue;
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] == '_' || name[0] == '.') continue;
    games.push_back(inspectGame(entry.path()));
  }

  std::sort(games.begin(), games.end(), [](const GameInfo& a, const GameInfo& b) {
    return lowerAscii(a.folderName) < lowerAscii(b.folderName);
  });
  return games;
}

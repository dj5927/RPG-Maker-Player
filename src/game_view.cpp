#include "game_view.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

bool matchesFilter(RgssEngine engine, const std::string& filter) {
  if (filter == "all") return true;
  if (filter == "easyrpg") return engine == RgssEngine::EasyRPG;
  if (filter == "wolf") return engine == RgssEngine::WolfRPG;
  if (filter == "xp") return engine == RgssEngine::XP;
  if (filter == "vx") return engine == RgssEngine::VX;
  if (filter == "vxace") return engine == RgssEngine::VXAce;
  if (filter == "mv") return engine == RgssEngine::MV;
  if (filter == "mz") return engine == RgssEngine::MZ;
  return true;
}

std::vector<std::uint32_t> nameKey(const std::string& text) {
  std::vector<std::uint32_t> out;
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  std::size_t i = 0;
  while (i < text.size()) {
    std::uint32_t cp = bytes[i++];
    if (cp >= 0xC2 && cp <= 0xDF && i < text.size()) {
      cp = ((cp & 0x1F) << 6) | (bytes[i++] & 0x3F);
    } else if (cp >= 0xE0 && cp <= 0xEF && i + 1 < text.size()) {
      cp = ((cp & 0x0F) << 12) | ((bytes[i] & 0x3F) << 6) | (bytes[i + 1] & 0x3F);
      i += 2;
    } else if (cp >= 0xF0 && cp <= 0xF4 && i + 2 < text.size()) {
      cp = ((cp & 0x07) << 18) | ((bytes[i] & 0x3F) << 12) |
           ((bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F);
      i += 3;
    }

    // ASCII is compared case-insensitively.
    if (cp >= 'A' && cp <= 'Z') cp += ('a' - 'A');
    // Fold full-width ASCII into regular ASCII.
    if (cp >= 0xFF01 && cp <= 0xFF5E) cp -= 0xFEE0;
    // Fold katakana to hiragana so mixed Japanese spellings group naturally.
    if (cp >= 0x30A1 && cp <= 0x30F6) cp -= 0x60;
    out.push_back(cp);
  }
  return out;
}

bool containsKey(const std::string& text, const std::vector<std::uint32_t>& queryKey) {
  if (queryKey.size() < 2) return true;
  const auto textKey = nameKey(text);
  return std::search(textKey.begin(), textKey.end(), queryKey.begin(), queryKey.end()) != textKey.end();
}

bool matchesSearch(const GameInfo& game,
                   const LauncherCatalog& catalog,
                   const std::vector<std::uint32_t>& queryKey) {
  if (queryKey.size() < 2) return true;
  if (containsKey(displayNameFor(game, catalog), queryKey)) return true;
  return containsKey(game.folderName, queryKey);
}

bool nameLess(const GameInfo& a, const GameInfo& b, const LauncherCatalog& catalog) {
  const std::string aName = displayNameFor(a, catalog);
  const std::string bName = displayNameFor(b, catalog);
  const auto aKey = nameKey(aName);
  const auto bKey = nameKey(bName);
  if (aKey != bKey) return aKey < bKey;
  if (aName != bName) return aName < bName;
  return a.folderName < b.folderName;
}

} // namespace

int gameEngineOrder(RgssEngine engine) {
  switch (engine) {
    case RgssEngine::EasyRPG: return 0;
    case RgssEngine::WolfRPG: return 1;
    case RgssEngine::XP: return 2;
    case RgssEngine::VX: return 3;
    case RgssEngine::VXAce: return 4;
    case RgssEngine::MV: return 5;
    case RgssEngine::MZ: return 6;
    default: return 7;
  }
}

std::vector<GameInfo> buildGameView(const std::vector<GameInfo>& allGames,
                                    const LauncherCatalog& catalog,
                                    const std::string& engineFilter,
                                    const std::string& sortMode,
                                    const std::string& searchQuery) {
  std::vector<GameInfo> result;
  result.reserve(allGames.size());
  const auto queryKey = nameKey(searchQuery);
  for (const auto& game : allGames) {
    if (matchesFilter(game.engine, engineFilter) && matchesSearch(game, catalog, queryKey)) {
      result.push_back(game);
    }
  }

  if (sortMode == "type") {
    std::stable_sort(result.begin(), result.end(), [&](const GameInfo& a, const GameInfo& b) {
      const int ar = gameEngineOrder(a.engine);
      const int br = gameEngineOrder(b.engine);
      if (ar != br) return ar < br;
      return nameLess(a, b, catalog);
    });
  } else {
    std::stable_sort(result.begin(), result.end(), [&](const GameInfo& a, const GameInfo& b) {
      return nameLess(a, b, catalog);
    });
  }
  return result;
}


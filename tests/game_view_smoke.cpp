#include "game_view.hpp"

#include <iostream>

namespace {

GameInfo game(const char* folder, RgssEngine engine) {
  GameInfo value;
  value.folderName = folder;
  value.engine = engine;
  return value;
}

bool foldersEqual(const std::vector<GameInfo>& games, const std::vector<std::string>& expected) {
  if (games.size() != expected.size()) return false;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    if (games[i].folderName != expected[i]) return false;
  }
  return true;
}

} // namespace

int main() {
  std::vector<GameInfo> all{
    game("easyA", RgssEngine::EasyRPG),
    game("wolfA", RgssEngine::WolfRPG),
    game("xpB", RgssEngine::XP),
    game("mvA", RgssEngine::MV),
    game("vxA", RgssEngine::VX),
    game("aceA", RgssEngine::VXAce),
    game("mzA", RgssEngine::MZ),
    game("xpA", RgssEngine::XP),
  };
  LauncherCatalog catalog;
  catalog.displayNames = {
    {"easyA", "2000게임"},
    {"wolfA", "울프게임"},
    {"xpB", "나비"},
    {"xpA", "가나다"},
    {"vxA", "banana"},
    {"aceA", "Apple"},
    {"mvA", "アカ"},
    {"mzA", "あお"},
  };

  const auto xp = buildGameView(all, catalog, "xp", "name");
  if (!foldersEqual(xp, {"xpA", "xpB"})) return 1;

  const auto easy = buildGameView(all, catalog, "easyrpg", "name");
  if (!foldersEqual(easy, {"easyA"})) return 2;

  const auto wolf = buildGameView(all, catalog, "wolf", "name");
  if (!foldersEqual(wolf, {"wolfA"})) return 3;

  const auto type = buildGameView(all, catalog, "all", "type");
  if (!foldersEqual(type, {"easyA", "wolfA", "xpA", "xpB", "vxA", "aceA", "mvA", "mzA"})) return 4;

  const auto name = buildGameView(all, catalog, "all", "name");
  if (name.size() != all.size()) return 5;

  catalog.displayNames["xpA"] = "재미있는게임";
  catalog.displayNames["xpB"] = "안재미있는게임";
  const auto oneChar = buildGameView(all, catalog, "all", "name", "재");
  if (oneChar.size() != all.size()) return 6;
  const auto liveKorean = buildGameView(all, catalog, "all", "name", "재미");
  if (!foldersEqual(liveKorean, {"xpB", "xpA"})) return 7;
  const auto partialEnglish = buildGameView(all, catalog, "all", "name", "NAN");
  if (!foldersEqual(partialEnglish, {"vxA"})) return 8;
  const auto folderFallback = buildGameView(all, catalog, "mv", "name", "mv");
  if (!foldersEqual(folderFallback, {"mvA"})) return 9;

  std::cout << "MKXP game view smoke PASS | sort/filter + 2-char live substring search PASS\n";
  return 0;
}


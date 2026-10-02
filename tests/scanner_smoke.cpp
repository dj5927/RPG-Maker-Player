#include "catalog.hpp"
#include "game_scanner.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static void touch(const fs::path& path, const std::string& text = "") {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << text;
}

int main() {
  const fs::path root = fs::temp_directory_path() / "mkxp_launcher_scanner_smoke";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / "game");

  touch(root / "game/GameXP/Data/Scripts.rxdata");
  touch(root / "game/GameXP/Game.exe");
  touch(root / "game/GameXP/Game.ini", "[Game]\nLibrary=RGSS104E.dll\n");
  touch(root / "game/Game2K/RPG_RT.ldb");
  touch(root / "game/Game2K/RPG_RT.lmt");
  touch(root / "game/Game2KZip/game.zip", "PK\x03\x04");
  touch(root / "game/Game2KZip/mkxp-locale.txt", "ko\n");
  touch(root / "game/Game2KZip/mkxp-easyrpg-font1.txt", "auto\n");
  touch(root / "game/Game2KZip/mkxp-easyrpg-font2.txt", "auto\n");
  touch(root / "game/Game2KZip/mkxp-easyrpg-soundfont.txt", "auto\n");
  touch(root / "game/Game2KZip/RPG_RT.ini", "[EasyRPG]\nEncoding=949\n");
  touch(root / "game/Game2KEasyRpg/game.easyrpg", "PK\x03\x04");
  touch(root / "game/Game2KEasyRpg/mkxp-locale.txt", "ja\n");
  touch(root / "game/GameWolf/Game.exe");
  touch(root / "game/GameWolf/Config.exe");
  touch(root / "game/GameWolf/Data/BasicData/CommonEvent.dat");
  touch(root / "game/GameVX/data/Scripts.rvdata");
  touch(root / "game/GameVX/Game.exe");
  touch(root / "game/GameVX/Game.ini", "[Game]\nLibrary=RGSS202E.dll\n");
  touch(root / "game/GameAce/Data/Scripts.rvdata2");
  touch(root / "game/GameAce/Game.exe");
  touch(root / "game/GameAce/Game.ini", "[Game]\nLibrary=RGSS301.dll\n");
  touch(root / "game/GameArchive/Game.rgss3a");
  touch(root / "game/GameIni/Game.ini", "[Game]\nLibrary=RGSS202E.dll\n");
  touch(root / "game/GameMV/package.json", "{\"name\":\"mv\"}\n");
  touch(root / "game/GameMV/www/index.html", "<html></html>\n");
  touch(root / "game/GameMV/www/js/plugins.js", "var $plugins = [];\n");
  touch(root / "game/GameMV/www/js/rpg_core.js", "Utils.RPGMAKER_NAME = 'MV';\n");
  touch(root / "game/GameMZ/package.json", "{\"name\":\"mz\"}\n");
  touch(root / "game/GameMZ/index.html", "<html></html>\n");
  touch(root / "game/GameMZ/js/plugins.js", "var $plugins = [];\n");
  touch(root / "game/GameMZ/js/rmmz_core.js", "Utils.RPGMAKER_NAME = 'MZ';\n");
  fs::create_directories(root / "game/GameMZ/data");
  fs::create_directories(root / "game/_image");

  const auto games = scanGames(root / "game");
  if (games.size() != 11) {
    std::cerr << "expected 11 games, got " << games.size() << '\n';
    return 1;
  }

  int easy = 0;
  int wolf = 0;
  int xp = 0;
  int vx = 0;
  int ace = 0;
  int mv = 0;
  int mz = 0;
  for (const auto& game : games) {
    if (game.engine == RgssEngine::EasyRPG) {
      ++easy;
      if ((game.folderName == "Game2KZip" || game.folderName == "Game2KEasyRpg") &&
          game.easyRpgProjectPath.empty()) {
        std::cerr << "EasyRPG archive project path missing for " << game.folderName << '\n';
        return 2;
      }
    }
    if (game.engine == RgssEngine::WolfRPG) ++wolf;
    if (game.engine == RgssEngine::XP) ++xp;
    if (game.engine == RgssEngine::VX) ++vx;
    if (game.engine == RgssEngine::VXAce) ++ace;
    if (game.engine == RgssEngine::MV) {
      ++mv;
      if (game.webRoot.filename() != "www") {
        std::cerr << "MV web root mismatch: " << game.webRoot << '\n';
        return 2;
      }
    }
    if (game.engine == RgssEngine::MZ) {
      ++mz;
      if (game.webRoot != game.path) {
        std::cerr << "MZ web root mismatch: " << game.webRoot << '\n';
        return 2;
      }
    }
    if (game.rubyRuntime != "auto") {
      std::cerr << "engine detection must not force Ruby runtime\n";
      return 2;
    }
  }
  if (easy != 3 || wolf != 1 || xp != 1 || vx != 2 || ace != 2 || mv != 1 || mz != 1) {
    std::cerr << "engine counts mismatch EasyRPG=" << easy << " WOLF=" << wolf << " XP=" << xp << " VX=" << vx << " ACE=" << ace
              << " MV=" << mv << " MZ=" << mz << '\n';
    return 3;
  }

  std::string diag;
  auto catalog = prepareCatalog(root / "game", games, diag);
  if (!catalog.valid || !fs::exists(root / "game/gamelist.json") || !fs::exists(root / "game/_image")) {
    std::cerr << "catalog setup failed: " << diag << '\n';
    return 4;
  }

  const auto aceIt = std::find_if(games.begin(), games.end(), [](const GameInfo& game) {
    return game.folderName == "GameAce";
  });
  const auto mzIt = std::find_if(games.begin(), games.end(), [](const GameInfo& game) {
    return game.folderName == "GameMZ";
  });
  if (aceIt == games.end() || mzIt == games.end()) return 5;
  if (!setDisplayName(*aceIt, catalog, "한글 게임 이름", diag)) return 6;
  if (!setDisplayName(*mzIt, catalog, "日本語ゲーム名", diag)) return 7;

  auto reloadedCatalog = prepareCatalog(root / "game", games, diag);
  if (displayNameFor(*aceIt, reloadedCatalog) != "한글 게임 이름") return 8;
  if (displayNameFor(*mzIt, reloadedCatalog) != "日本語ゲーム名") return 9;

  std::cout << "MKXP scanner smoke PASS | games=" << games.size() << " EasyRPG=" << easy << " WOLF=" << wolf << " XP=" << xp
            << " VX=" << vx << " VXAce=" << ace << " MV=" << mv << " MZ=" << mz << '\n';
  fs::remove_all(root, ec);
  return 0;
}

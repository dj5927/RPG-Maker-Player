#include "catalog.hpp"
#include "game_scanner.hpp"
#include "game_view.hpp"
#include "launcher_config.hpp"
#include "ruby_detector.hpp"
#include "ui_sound.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__linux__)
#include <dlfcn.h>
#include <fcntl.h>
#include <iconv.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr int WIDTH = 1280;
constexpr int HEIGHT = 720;
constexpr int WIDE_HEIGHT = 800;
constexpr double ASPECT_16_10 = 1.60;
constexpr double ASPECT_16_10_TOLERANCE = 0.07;
constexpr Uint64 EXIT_COMBO_HOLD_MS = 300;
constexpr Uint64 GAME_EXIT_COMBO_HOLD_MS = 1500;
constexpr Uint64 GAME_KEYBOARD_COMBO_HOLD_MS = 50;
constexpr const char* APP_VERSION = "1.3";
const SDL_Color BG{16, 21, 29, 255};
const SDL_Color WHITE{255, 255, 255, 255};
const SDL_Color MUTED{170, 180, 194, 255};
const SDL_Color MUTED2{127, 139, 155, 255};
const SDL_Color SELECT_BG{42, 126, 201, 255};
const SDL_Color SELECT_TEXT{255, 255, 255, 255};
const SDL_Color CARD{18, 45, 72, 255};
const SDL_Color CARD_INNER{27, 59, 89, 255};
const SDL_Color BORDER{58, 94, 125, 255};
const SDL_Color CURSOR_BORDER{255, 190, 72, 255};
const SDL_Color UI_NAVY{8, 27, 47, 255};
const SDL_Color UI_NAVY_2{12, 38, 65, 255};
const SDL_Color UI_PANEL{15, 39, 65, 242};
const SDL_Color UI_PANEL_SOFT{22, 53, 84, 224};
const SDL_Color UI_BLUE{65, 166, 255, 255};
const SDL_Color UI_BLUE_SOFT{120, 201, 255, 255};
const SDL_Color UI_GREEN{64, 214, 143, 255};
const SDL_Color UI_RED{255, 92, 108, 255};
const SDL_Color UI_YELLOW{250, 205, 72, 255};


struct DisplayLayout {
  int displayWidth = WIDTH;
  int displayHeight = HEIGHT;
  int logicalWidth = WIDTH;
  int logicalHeight = HEIGHT;
  int uiOffsetY = 0;
  bool wide16x10 = false;
};

DisplayLayout detectDisplayLayout() {
  DisplayLayout layout;
  SDL_DisplayMode mode{};
  if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0) {
    layout.displayWidth = mode.w;
    layout.displayHeight = mode.h;
  }
  const double aspect = layout.displayHeight > 0
    ? static_cast<double>(layout.displayWidth) / static_cast<double>(layout.displayHeight)
    : (16.0 / 9.0);
  layout.wide16x10 = std::abs(aspect - ASPECT_16_10) <= ASPECT_16_10_TOLERANCE;
  layout.logicalHeight = layout.wide16x10 ? WIDE_HEIGHT : HEIGHT;
  layout.uiOffsetY = (layout.logicalHeight - HEIGHT) / 2;
  return layout;
}

enum class UiLanguage {
  Korean,
  English,
  Japanese,
};

enum class UiKey {
  ViewGrid,
  ViewList,
  EmptyGames,
  FooterGrid,
  FooterList,
  ExitQuestion,
  Yes,
  No,
  ExitHelp,
  SelectedRubyMissing,
  MkxpForkFailed,
  GameExit0,
  RunFailed,
  GameSignal,
  GameAbnormal,
  LinuxOnly,
  NwjsNotInstalled,
  MvmzRuntimeMissing,
  MvmzWebRootMissing,
  MvmzForkFailed,
  MvmzRunFailed,
  MvmzSignal,
  MvmzAbnormal,
  UnsupportedGame,
  Launching,
  NwjsMissingShort,
  AutoPrefix,
  NwjsSaveFailed,
  NwjsSetting,
  ManualRuntimeUnavailable,
  RubySaveFailed,
  RubySetting,
  CatalogReady,
  CatalogParseFailed,
  CatalogWriteFailed,
  GameFolderButton,
  FolderPickerTitle,
  FolderPickerCurrent,
  FolderPickerUse,
  FolderPickerUp,
  FolderPickerCancel,
  FolderPickerHelp,
  FolderPickerEmpty,
  FolderSaved,
  FolderSaveFailed,
  GameSettingsTitle,
  GameName,
  Engine,
  RubyRuntime,
  Automatic,
  Done,
  SettingsHelp,
  NameEditTitle,
  NameEditHelp,
  NameSaved,
  NameSaveFailed,
  RubySaved,
  FilterSortTitle,
  ShowGames,
  AllGames,
  SortMethod,
  SortByType,
  SortByName,
  Apply,
  FilterSortHelp,
  FilterSortSaved,
  FilterSortSaveFailed,
  NoFilteredGames,
  SearchTitle,
  SearchHelp,
  SearchHint,
  SearchClear,
  SearchClose,
  NoSearchGames,
};

UiLanguage gUiLanguage = UiLanguage::English;

std::optional<UiLanguage> languageFromLocale(const char* raw) {
  if (!raw || !*raw) return std::nullopt;
  std::string value(raw);
  std::size_t begin = 0;
  while (begin <= value.size()) {
    const std::size_t end = value.find(':', begin);
    std::string tag = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.front()))) tag.erase(tag.begin());
    while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back()))) tag.pop_back();
    if (tag == "c" || tag == "c.utf-8" || tag == "c.utf8" || tag == "posix") {
      if (end == std::string::npos) break;
      begin = end + 1;
      continue;
    }
    if (tag.rfind("ko", 0) == 0 || tag == "koreana" || tag == "korean") return UiLanguage::Korean;
    if (tag.rfind("ja", 0) == 0 || tag == "japanese") return UiLanguage::Japanese;
    if (tag.rfind("en", 0) == 0 || tag == "english") return UiLanguage::English;
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return std::nullopt;
}

std::optional<UiLanguage> languageFromConfigText(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  auto inspect = [&](std::size_t pos) -> std::optional<UiLanguage> {
    if (pos == std::string::npos) return std::nullopt;
    const std::string part = value.substr(pos, std::min<std::size_t>(160, value.size() - pos));
    if (part.find("koreana") != std::string::npos || part.find("ko_kr") != std::string::npos ||
        part.find("language=ko") != std::string::npos) return UiLanguage::Korean;
    if (part.find("japanese") != std::string::npos || part.find("ja_jp") != std::string::npos ||
        part.find("language=ja") != std::string::npos) return UiLanguage::Japanese;
    if (part.find("english") != std::string::npos || part.find("en_us") != std::string::npos ||
        part.find("language=en") != std::string::npos) return UiLanguage::English;
    return std::nullopt;
  };

  std::size_t pos = 0;
  while ((pos = value.find("language", pos)) != std::string::npos) {
    if (const auto language = inspect(pos)) return language;
    pos += 8;
  }
  pos = 0;
  while ((pos = value.find("lang=", pos)) != std::string::npos) {
    if (const auto language = inspect(pos)) return language;
    pos += 5;
  }
  return std::nullopt;
}

std::optional<UiLanguage> languageFromFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream text;
  text << in.rdbuf();
  return languageFromConfigText(text.str());
}

UiLanguage detectUiLanguage() {
  if (const auto explicitLanguage = languageFromLocale(std::getenv("MKXP_LANG"))) return *explicitLanguage;

  const char* steamVariables[] = {"SteamLanguage", "STEAM_LANGUAGE", "STEAMUI_LANGUAGE"};
  for (const char* name : steamVariables) {
    if (const auto language = languageFromLocale(std::getenv(name))) return *language;
  }

  const char* home = std::getenv("HOME");
  if (home && *home) {
    const std::array<fs::path, 4> languageFiles{{
      fs::path(home) / ".steam/steam/config/config.vdf",
      fs::path(home) / ".local/share/Steam/config/config.vdf",
      fs::path(home) / ".config/plasma-localerc",
      fs::path(home) / ".config/kdeglobals",
    }};
    std::optional<UiLanguage> fileEnglish;
    for (const auto& path : languageFiles) {
      if (const auto language = languageFromFile(path)) {
        if (*language == UiLanguage::Korean || *language == UiLanguage::Japanese) return *language;
        fileEnglish = *language;
      }
    }
    if (fileEnglish) {
      // Keep looking at process locale for an explicit ko/ja locale before
      // accepting an English Steam/KDE fallback.
    }
  }

  std::optional<UiLanguage> englishFallback;
  const char* variables[] = {"LANGUAGE", "LC_MESSAGES", "LANG", "LC_ALL"};
  for (const char* name : variables) {
    if (const auto language = languageFromLocale(std::getenv(name))) {
      if (*language == UiLanguage::Korean || *language == UiLanguage::Japanese) return *language;
      englishFallback = *language;
    }
  }
  return englishFallback.value_or(UiLanguage::English);
}

const char* uiLanguageCode(UiLanguage language) {
  switch (language) {
    case UiLanguage::Korean: return "ko";
    case UiLanguage::Japanese: return "ja";
    default: return "en";
  }
}

const char* tr(UiKey key) {
  if (gUiLanguage == UiLanguage::Korean) {
    switch (key) {
      case UiKey::ViewGrid: return "썸네일 5 x 2";
      case UiKey::ViewList: return "텍스트 목록";
      case UiKey::EmptyGames: return "선택한 게임 폴더에 2K/2K3 / WOLF / XP / VX / VX Ace / MV / MZ 게임 폴더를 넣어주세요.";
      case UiKey::FooterGrid: return "D-Pad 이동  L1/R1 페이지  A 실행  X 보기  Y 검색  Select 설정  Start+Select 종료";
      case UiKey::FooterList: return "D-Pad 이동  L1/R1 페이지  A 실행  X 보기  Y 검색  Select 설정  Start+Select 종료";
      case UiKey::ExitQuestion: return "종료하시겠습니까?";
      case UiKey::Yes: return "예";
      case UiKey::No: return "아니오";
      case UiKey::ExitHelp: return "←/→ 선택     A 결정     B 취소";
      case UiKey::SelectedRubyMissing: return "선택된 Ruby용 mkxp-z 실행 파일이 없습니다: ";
      case UiKey::MkxpForkFailed: return "mkxp-z 실행 실패: fork 오류";
      case UiKey::GameExit0: return "게임 종료. exit=0";
      case UiKey::RunFailed: return "실행 실패. exit=";
      case UiKey::GameSignal: return "게임 비정상 종료. signal=";
      case UiKey::GameAbnormal: return "게임이 비정상 종료되었습니다. (logs/game_* 확인)";
      case UiKey::LinuxOnly: return "현재 런처 실행은 SteamOS/Linux 전용입니다.";
      case UiKey::NwjsNotInstalled: return "NW.js가 설치되어 있지 않습니다. runtime/nwjs/install_nwjs.sh 실행 필요";
      case UiKey::MvmzRuntimeMissing: return "MV/MZ 런타임이 없습니다: ";
      case UiKey::MvmzWebRootMissing: return "MV/MZ 웹 루트를 찾을 수 없습니다.";
      case UiKey::MvmzForkFailed: return "MV/MZ 실행 실패: fork 오류";
      case UiKey::MvmzRunFailed: return "MV/MZ 실행 실패. exit=";
      case UiKey::MvmzSignal: return "MV/MZ 비정상 종료. signal=";
      case UiKey::MvmzAbnormal: return "MV/MZ 게임이 비정상 종료되었습니다. (logs/game_* 확인)";
      case UiKey::UnsupportedGame: return "지원하지 않는 게임 구조입니다.";
      case UiKey::Launching: return "실행 중: ";
      case UiKey::NwjsMissingShort: return "NWJS 없음";
      case UiKey::AutoPrefix: return "자동->";
      case UiKey::NwjsSaveFailed: return "NW.js 설정 저장 실패: mkxp-nwjs.txt";
      case UiKey::NwjsSetting: return "NW.js 설정: ";
      case UiKey::ManualRuntimeUnavailable: return "이 게임 형식에는 수동 런타임 선택이 없습니다.";
      case UiKey::RubySaveFailed: return "Ruby 설정 저장 실패: mkxp-ruby.txt";
      case UiKey::RubySetting: return "Ruby 설정: ";
      case UiKey::CatalogReady: return "게임 목록 준비 완료";
      case UiKey::CatalogParseFailed: return "gamelist.json 읽기 실패: 기존 파일은 유지됩니다";
      case UiKey::CatalogWriteFailed: return "gamelist.json 저장 실패";
      case UiKey::GameFolderButton: return "게임 폴더 변경";
      case UiKey::FolderPickerTitle: return "게임 폴더 선택";
      case UiKey::FolderPickerCurrent: return "현재 위치";
      case UiKey::FolderPickerUse: return "이 폴더 사용";
      case UiKey::FolderPickerUp: return "상위 폴더";
      case UiKey::FolderPickerCancel: return "취소";
      case UiKey::FolderPickerHelp: return "D-Pad 이동  A 열기  B 상위  X 현재 폴더 선택";
      case UiKey::FolderPickerEmpty: return "하위 폴더가 없습니다.";
      case UiKey::FolderSaved: return "게임 폴더 변경 완료: ";
      case UiKey::FolderSaveFailed: return "게임 폴더 설정 저장 실패";
      case UiKey::GameSettingsTitle: return "게임 설정";
      case UiKey::GameName: return "게임 이름";
      case UiKey::Engine: return "종류";
      case UiKey::RubyRuntime: return "Ruby";
      case UiKey::Automatic: return "자동";
      case UiKey::Done: return "완료";
      case UiKey::SettingsHelp: return "D-Pad 이동  A 선택  ←/→ 런타임 변경  B 닫기";
      case UiKey::NameEditTitle: return "게임 이름 수정";
      case UiKey::NameEditHelp: return "이름 입력 후 Enter/A 저장  B 취소";
      case UiKey::NameSaved: return "게임 이름 저장 완료";
      case UiKey::NameSaveFailed: return "게임 이름 저장 실패";
      case UiKey::RubySaved: return "Ruby 설정 저장 완료: ";
      case UiKey::FilterSortTitle: return "게임 필터 / 정렬";
      case UiKey::ShowGames: return "표시 게임";
      case UiKey::AllGames: return "전체";
      case UiKey::SortMethod: return "정렬 방법";
      case UiKey::SortByType: return "종류순";
      case UiKey::SortByName: return "이름순";
      case UiKey::Apply: return "적용";
      case UiKey::FilterSortHelp: return "D-Pad 선택  A 결정  B 취소";
      case UiKey::FilterSortSaved: return "필터/정렬 적용 완료";
      case UiKey::FilterSortSaveFailed: return "필터/정렬 설정 저장 실패";
      case UiKey::NoFilteredGames: return "선택한 조건에 맞는 게임이 없습니다.";
      case UiKey::SearchTitle: return "게임 검색";
      case UiKey::SearchHelp: return "2글자부터 실시간 검색  X 지우기  A/B/Y 닫기";
      case UiKey::SearchHint: return "게임 이름 일부를 입력하세요";
      case UiKey::SearchClear: return "지우기";
      case UiKey::SearchClose: return "닫기";
      case UiKey::NoSearchGames: return "검색 조건에 맞는 게임이 없습니다.";
    }
  } else if (gUiLanguage == UiLanguage::Japanese) {
    switch (key) {
      case UiKey::ViewGrid: return "サムネイル 5 x 2";
      case UiKey::ViewList: return "テキスト一覧";
      case UiKey::EmptyGames: return "選択したゲームフォルダーに 2K/2K3 / WOLF / XP / VX / VX Ace / MV / MZ のゲームフォルダーを入れてください。";
      case UiKey::FooterGrid: return "D-Pad 移動  L1/R1 ページ  A 起動  X 表示  Y 検索  Select 設定  Start+Select 終了";
      case UiKey::FooterList: return "D-Pad 移動  L1/R1 ページ  A 起動  X 表示  Y 検索  Select 設定  Start+Select 終了";
      case UiKey::ExitQuestion: return "終了しますか？";
      case UiKey::Yes: return "はい";
      case UiKey::No: return "いいえ";
      case UiKey::ExitHelp: return "←/→ 選択     A 決定     B キャンセル";
      case UiKey::SelectedRubyMissing: return "選択した Ruby 用 mkxp-z がありません: ";
      case UiKey::MkxpForkFailed: return "mkxp-z の起動に失敗しました: fork エラー";
      case UiKey::GameExit0: return "ゲーム終了。exit=0";
      case UiKey::RunFailed: return "起動失敗。exit=";
      case UiKey::GameSignal: return "ゲームが異常終了しました。signal=";
      case UiKey::GameAbnormal: return "ゲームが異常終了しました。(logs/game_* を確認)";
      case UiKey::LinuxOnly: return "このランチャーは SteamOS/Linux 専用です。";
      case UiKey::NwjsNotInstalled: return "NW.js がありません。runtime/nwjs/install_nwjs.sh を実行してください";
      case UiKey::MvmzRuntimeMissing: return "MV/MZ ランタイムがありません: ";
      case UiKey::MvmzWebRootMissing: return "MV/MZ の Web ルートが見つかりません。";
      case UiKey::MvmzForkFailed: return "MV/MZ の起動に失敗しました: fork エラー";
      case UiKey::MvmzRunFailed: return "MV/MZ の起動に失敗しました。exit=";
      case UiKey::MvmzSignal: return "MV/MZ が異常終了しました。signal=";
      case UiKey::MvmzAbnormal: return "MV/MZ ゲームが異常終了しました。(logs/game_* を確認)";
      case UiKey::UnsupportedGame: return "対応していないゲーム構成です。";
      case UiKey::Launching: return "起動中: ";
      case UiKey::NwjsMissingShort: return "NWJS なし";
      case UiKey::AutoPrefix: return "自動->";
      case UiKey::NwjsSaveFailed: return "NW.js 設定の保存に失敗しました: mkxp-nwjs.txt";
      case UiKey::NwjsSetting: return "NW.js 設定: ";
      case UiKey::ManualRuntimeUnavailable: return "このゲーム形式では手動ランタイム選択を使用できません。";
      case UiKey::RubySaveFailed: return "Ruby 設定の保存に失敗しました: mkxp-ruby.txt";
      case UiKey::RubySetting: return "Ruby 設定: ";
      case UiKey::CatalogReady: return "ゲーム一覧の準備完了";
      case UiKey::CatalogParseFailed: return "gamelist.json の読み込みに失敗しました。既存ファイルは保持されます";
      case UiKey::CatalogWriteFailed: return "gamelist.json の保存に失敗しました";
      case UiKey::GameFolderButton: return "ゲームフォルダー変更";
      case UiKey::FolderPickerTitle: return "ゲームフォルダーを選択";
      case UiKey::FolderPickerCurrent: return "現在の場所";
      case UiKey::FolderPickerUse: return "このフォルダーを使用";
      case UiKey::FolderPickerUp: return "上のフォルダー";
      case UiKey::FolderPickerCancel: return "キャンセル";
      case UiKey::FolderPickerHelp: return "D-Pad 移動  A 開く  B 上へ  X 現在のフォルダーを選択";
      case UiKey::FolderPickerEmpty: return "サブフォルダーがありません。";
      case UiKey::FolderSaved: return "ゲームフォルダーを変更しました: ";
      case UiKey::FolderSaveFailed: return "ゲームフォルダー設定の保存に失敗しました";
      case UiKey::GameSettingsTitle: return "ゲーム設定";
      case UiKey::GameName: return "ゲーム名";
      case UiKey::Engine: return "種類";
      case UiKey::RubyRuntime: return "Ruby";
      case UiKey::Automatic: return "自動";
      case UiKey::Done: return "完了";
      case UiKey::SettingsHelp: return "D-Pad 移動  A 選択  ←/→ ランタイム変更  B 閉じる";
      case UiKey::NameEditTitle: return "ゲーム名を編集";
      case UiKey::NameEditHelp: return "入力後 Enter/A で保存  B でキャンセル";
      case UiKey::NameSaved: return "ゲーム名を保存しました";
      case UiKey::NameSaveFailed: return "ゲーム名の保存に失敗しました";
      case UiKey::RubySaved: return "Ruby 設定を保存しました: ";
      case UiKey::FilterSortTitle: return "ゲームの絞り込み / 並び替え";
      case UiKey::ShowGames: return "表示するゲーム";
      case UiKey::AllGames: return "すべて";
      case UiKey::SortMethod: return "並び順";
      case UiKey::SortByType: return "種類順";
      case UiKey::SortByName: return "名前順";
      case UiKey::Apply: return "適用";
      case UiKey::FilterSortHelp: return "D-Pad 選択  A 決定  B キャンセル";
      case UiKey::FilterSortSaved: return "絞り込み/並び替えを適用しました";
      case UiKey::FilterSortSaveFailed: return "絞り込み/並び替え設定の保存に失敗しました";
      case UiKey::NoFilteredGames: return "選択した条件に一致するゲームがありません。";
      case UiKey::SearchTitle: return "ゲーム検索";
      case UiKey::SearchHelp: return "2文字からライブ検索  X クリア  A/B/Y 閉じる";
      case UiKey::SearchHint: return "ゲーム名の一部を入力";
      case UiKey::SearchClear: return "クリア";
      case UiKey::SearchClose: return "閉じる";
      case UiKey::NoSearchGames: return "検索条件に一致するゲームがありません。";
    }
  }

  switch (key) {
    case UiKey::ViewGrid: return "THUMBNAIL 5 x 2";
    case UiKey::ViewList: return "TEXT LIST";
    case UiKey::EmptyGames: return "Put XP / VX / VX Ace / MV / MZ game folders in the selected game folder.";
    case UiKey::FooterGrid: return "D-Pad Move  L1/R1 Page  A Launch  X View  Y Search  Select Settings  Start+Select Exit";
    case UiKey::FooterList: return "D-Pad Move  L1/R1 Page  A Launch  X View  Y Search  Select Settings  Start+Select Exit";
    case UiKey::ExitQuestion: return "Exit the launcher?";
    case UiKey::Yes: return "Yes";
    case UiKey::No: return "No";
    case UiKey::ExitHelp: return "←/→ Select     A Confirm     B Cancel";
    case UiKey::SelectedRubyMissing: return "mkxp-z for the selected Ruby runtime is missing: ";
    case UiKey::MkxpForkFailed: return "Failed to launch mkxp-z: fork error";
    case UiKey::GameExit0: return "Game closed. exit=0";
    case UiKey::RunFailed: return "Launch failed. exit=";
    case UiKey::GameSignal: return "Game terminated unexpectedly. signal=";
    case UiKey::GameAbnormal: return "Game terminated unexpectedly. Check logs/game_*.";
    case UiKey::LinuxOnly: return "This launcher is for SteamOS/Linux only.";
    case UiKey::NwjsNotInstalled: return "NW.js is not installed. Run runtime/nwjs/install_nwjs.sh";
    case UiKey::MvmzRuntimeMissing: return "MV/MZ runtime is missing: ";
    case UiKey::MvmzWebRootMissing: return "MV/MZ web root was not found.";
    case UiKey::MvmzForkFailed: return "Failed to launch MV/MZ: fork error";
    case UiKey::MvmzRunFailed: return "MV/MZ launch failed. exit=";
    case UiKey::MvmzSignal: return "MV/MZ terminated unexpectedly. signal=";
    case UiKey::MvmzAbnormal: return "MV/MZ game terminated unexpectedly. Check logs/game_*.";
    case UiKey::UnsupportedGame: return "Unsupported game structure.";
    case UiKey::Launching: return "Launching: ";
    case UiKey::NwjsMissingShort: return "NWJS MISSING";
    case UiKey::AutoPrefix: return "AUTO->";
    case UiKey::NwjsSaveFailed: return "Failed to save NW.js setting: mkxp-nwjs.txt";
    case UiKey::NwjsSetting: return "NW.js setting: ";
    case UiKey::ManualRuntimeUnavailable: return "Manual runtime selection is unavailable for this game type.";
    case UiKey::RubySaveFailed: return "Failed to save Ruby setting: mkxp-ruby.txt";
    case UiKey::RubySetting: return "Ruby setting: ";
    case UiKey::CatalogReady: return "Game catalog ready";
    case UiKey::CatalogParseFailed: return "Failed to parse gamelist.json; existing file was kept";
    case UiKey::CatalogWriteFailed: return "Failed to write gamelist.json";
    case UiKey::GameFolderButton: return "Change Game Folder";
    case UiKey::FolderPickerTitle: return "Select Game Folder";
    case UiKey::FolderPickerCurrent: return "Current location";
    case UiKey::FolderPickerUse: return "Use This Folder";
    case UiKey::FolderPickerUp: return "Parent Folder";
    case UiKey::FolderPickerCancel: return "Cancel";
    case UiKey::FolderPickerHelp: return "D-Pad Move  A Open  B Parent  X Use Current Folder";
    case UiKey::FolderPickerEmpty: return "No subfolders.";
    case UiKey::FolderSaved: return "Game folder changed: ";
    case UiKey::FolderSaveFailed: return "Failed to save game folder setting";
    case UiKey::GameSettingsTitle: return "Game Settings";
    case UiKey::GameName: return "Game Name";
    case UiKey::Engine: return "Type";
    case UiKey::RubyRuntime: return "Ruby";
    case UiKey::Automatic: return "Auto";
    case UiKey::Done: return "Done";
    case UiKey::SettingsHelp: return "D-Pad Move  A Select  ←/→ Change Runtime  B Close";
    case UiKey::NameEditTitle: return "Edit Game Name";
    case UiKey::NameEditHelp: return "Type a name, then Enter/A to save  B to cancel";
    case UiKey::NameSaved: return "Game name saved";
    case UiKey::NameSaveFailed: return "Failed to save game name";
    case UiKey::RubySaved: return "Ruby setting saved: ";
    case UiKey::FilterSortTitle: return "Filter / Sort Games";
    case UiKey::ShowGames: return "Show Games";
    case UiKey::AllGames: return "All";
    case UiKey::SortMethod: return "Sort Method";
    case UiKey::SortByType: return "By Type";
    case UiKey::SortByName: return "By Name";
    case UiKey::Apply: return "Apply";
    case UiKey::FilterSortHelp: return "D-Pad Select  A Confirm  B Cancel";
    case UiKey::FilterSortSaved: return "Filter/sort applied";
    case UiKey::FilterSortSaveFailed: return "Failed to save filter/sort settings";
    case UiKey::NoFilteredGames: return "No games match the selected filter.";
    case UiKey::SearchTitle: return "Search Games";
    case UiKey::SearchHelp: return "Live search from 2 characters  X Clear  A/B/Y Close";
    case UiKey::SearchHint: return "Type any part of a game name";
    case UiKey::SearchClear: return "Clear";
    case UiKey::SearchClose: return "Close";
    case UiKey::NoSearchGames: return "No games match the search.";
  }
  return "";
}

const char* rubyDeepScanLabel() {
  if (gUiLanguage == UiLanguage::Korean) return "자동 Ruby 정밀 스캔";
  if (gUiLanguage == UiLanguage::Japanese) return "Ruby 自動詳細スキャン";
  return "Deep Auto Ruby Scan";
}

const char* rubyDeepScanRunningText() {
  if (gUiLanguage == UiLanguage::Korean) return "Ruby 1.8 / 1.9 / 3.1 정밀 스캔 중...";
  if (gUiLanguage == UiLanguage::Japanese) return "Ruby 1.8 / 1.9 / 3.1 を詳細スキャン中...";
  return "Deep-scanning Ruby 1.8 / 1.9 / 3.1...";
}

std::string rubyDeepScanDoneText(const std::string& runtime) {
  const std::string shortName = runtime == "ruby18" ? "1.8" : (runtime == "ruby19" ? "1.9" : "3.1");
  if (gUiLanguage == UiLanguage::Korean) return "정밀 스캔 완료: Ruby " + shortName;
  if (gUiLanguage == UiLanguage::Japanese) return "詳細スキャン完了: Ruby " + shortName;
  return "Deep scan complete: Ruby " + shortName;
}

const char* nwjsDeepScanLabel() {
  if (gUiLanguage == UiLanguage::Korean) return "NW.js 호환성 정밀 검사";
  if (gUiLanguage == UiLanguage::Japanese) return "NW.js 互換性詳細スキャン";
  return "Deep NW.js Compatibility Scan";
}

const char* nwjsDeepScanRunningText() {
  if (gUiLanguage == UiLanguage::Korean) return "설치된 NW.js 버전 호환성 검사 중...";
  if (gUiLanguage == UiLanguage::Japanese) return "インストール済み NW.js の互換性を検査中...";
  return "Testing installed NW.js versions...";
}

std::string nwjsDeepScanDoneText(const std::string& runtime) {
  if (gUiLanguage == UiLanguage::Korean) return "정밀 검사 완료: NW.js " + runtime;
  if (gUiLanguage == UiLanguage::Japanese) return "詳細スキャン完了: NW.js " + runtime;
  return "Deep scan complete: NW.js " + runtime;
}

std::string gameCountText(std::size_t count) {
  if (gUiLanguage == UiLanguage::Korean) return std::to_string(count) + "개 게임";
  if (gUiLanguage == UiLanguage::Japanese) return std::to_string(count) + " 本のゲーム";
  return std::to_string(count) + " games found";
}

std::string filteredGameCountText(std::size_t visible, std::size_t total) {
  if (visible == total) return gameCountText(visible);
  if (gUiLanguage == UiLanguage::Korean)
    return std::to_string(visible) + " / " + std::to_string(total) + "개 게임";
  if (gUiLanguage == UiLanguage::Japanese)
    return std::to_string(visible) + " / " + std::to_string(total) + " 本のゲーム";
  return std::to_string(visible) + " / " + std::to_string(total) + " games";
}

std::string localizeCatalogDiagnostic(const std::string& diagnostic) {
  if (diagnostic == "catalog ready") return tr(UiKey::CatalogReady);
  if (diagnostic == "gamelist.json parse failed; existing file was not overwritten") return tr(UiKey::CatalogParseFailed);
  if (diagnostic == "gamelist.json write failed") return tr(UiKey::CatalogWriteFailed);
  return diagnostic;
}

struct FontSet {
  TTF_Font* big = nullptr;
  TTF_Font* normal = nullptr;
  TTF_Font* medium = nullptr;
  TTF_Font* small = nullptr;
};

struct TextureCache {
  struct Entry {
    SDL_Texture* texture = nullptr;
    bool exists = false;
    fs::file_time_type modified{};
    std::uintmax_t size = 0;
    Uint64 nextCheck = 0;
  };

  SDL_Renderer* renderer = nullptr;
  std::unordered_map<std::string, Entry> values;
  explicit TextureCache(SDL_Renderer* rendererValue) : renderer(rendererValue) {}
  ~TextureCache() {
    for (auto& [_, entry] : values) {
      if (entry.texture) SDL_DestroyTexture(entry.texture);
    }
  }
  void clear() {
    for (auto& [_, entry] : values) {
      if (entry.texture) SDL_DestroyTexture(entry.texture);
    }
    values.clear();
  }
  SDL_Texture* get(const fs::path& path) {
    const std::string key = path.string();
    const Uint64 now = SDL_GetTicks64();
    auto it = values.find(key);
    if (it != values.end() && now < it->second.nextCheck) return it->second.texture;

    std::error_code ec;
    const bool exists = fs::is_regular_file(path, ec);
    fs::file_time_type modified{};
    std::uintmax_t size = 0;
    if (exists) {
      modified = fs::last_write_time(path, ec);
      if (ec) { ec.clear(); modified = fs::file_time_type{}; }
      size = fs::file_size(path, ec);
      if (ec) { ec.clear(); size = 0; }
    }

    if (it != values.end() && it->second.exists == exists &&
        it->second.modified == modified && it->second.size == size) {
      it->second.nextCheck = now + 750;
      return it->second.texture;
    }

    Entry entry;
    entry.exists = exists;
    entry.modified = modified;
    entry.size = size;
    entry.nextCheck = now + 750;
    if (exists) entry.texture = IMG_LoadTexture(renderer, key.c_str());

    if (it != values.end()) {
      if (it->second.texture) SDL_DestroyTexture(it->second.texture);
      it->second = std::move(entry);
      return it->second.texture;
    }
    auto [inserted, _] = values.emplace(key, std::move(entry));
    return inserted->second.texture;
  }
};

fs::path thumbnailPathFor(const fs::path& gameRoot,
                          const LauncherCatalog& catalog,
                          const GameInfo& game) {
  const fs::path fileName = game.folderName + ".png";
  const fs::path liveOverride = gameRoot / "_image" / fileName;
  std::error_code ec;
  if (fs::is_regular_file(liveOverride, ec)) return liveOverride;
  return catalog.imageRoot / fileName;
}

struct FolderPickerState {
  bool active = false;
  bool mandatory = false;
  fs::path current;
  std::vector<fs::path> children;
  std::size_t selected = 0;
  int scroll = 0;
  std::string error;
};

enum class GameLocale {
  Korean = 0,
  Japanese = 1,
  English = 2,
};

fs::path gameLocaleFile(const GameInfo& game) {
  return game.path / "mkxp-locale.txt";
}

std::string gameLocaleCode(GameLocale locale) {
  switch (locale) {
    case GameLocale::Japanese: return "ja";
    case GameLocale::English: return "en";
    case GameLocale::Korean:
    default: return "ko";
  }
}

std::string gameLocalePosix(GameLocale locale) {
  switch (locale) {
    case GameLocale::Japanese: return "ja_JP.UTF-8";
    case GameLocale::English: return "en_US.UTF-8";
    case GameLocale::Korean:
    default: return "ko_KR.UTF-8";
  }
}

std::string gameLocaleLanguage(GameLocale locale) {
  switch (locale) {
    case GameLocale::Japanese: return "ja_JP:ja";
    case GameLocale::English: return "en_US:en";
    case GameLocale::Korean:
    default: return "ko_KR:ko";
  }
}

GameLocale loadGameLocale(const GameInfo& game) {
  std::ifstream in(gameLocaleFile(game), std::ios::binary);
  if (!in) return GameLocale::Korean;
  std::string value;
  std::getline(in, value);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
  value.erase(0, first);
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (value == "ja" || value == "jp" || value == "japanese" || value.rfind("ja_", 0) == 0)
    return GameLocale::Japanese;
  if (value == "en" || value == "english" || value.rfind("en_", 0) == 0)
    return GameLocale::English;
  return GameLocale::Korean;
}

bool saveGameLocale(const GameInfo& game, GameLocale locale) {
  std::ofstream out(gameLocaleFile(game), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << gameLocaleCode(locale) << '\n';
  return static_cast<bool>(out);
}

void applyGameLocaleEnvironment(const GameInfo& game) {
#if defined(__linux__)
  const GameLocale locale = loadGameLocale(game);
  const std::string posix = gameLocalePosix(locale);
  const std::string language = gameLocaleLanguage(locale);
  const std::string code = gameLocaleCode(locale);
  setenv("LANG", posix.c_str(), 1);
  setenv("LANGUAGE", language.c_str(), 1);
  setenv("LC_ALL", posix.c_str(), 1);
  setenv("LC_CTYPE", posix.c_str(), 1);
  setenv("LC_MESSAGES", posix.c_str(), 1);
  setenv("MKXP_GAME_LOCALE", code.c_str(), 1);
#else
  (void)game;
#endif
}

struct GameSettingsState {
  bool active = false;
  bool editingName = false;
  int row = 0;
  int scrollFirstRow = 1;
  GameLocale locale = GameLocale::Korean;
  std::string editText;
  std::string originalName;
  std::string rubyScanResult;
  std::string nwjsScanResult;
};

struct ControllerRemapEntry {
  int from = -1;
  int to = -1;
};

struct KeyRemapState {
  bool active = false;
  int stage = 0; // 0=menu, 1=waiting source, 2=waiting destination
  int source = -1;
  std::vector<ControllerRemapEntry> entries;
  std::string message;
};

const char* controllerButtonShortName(int button) {
  switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return "A";
    case SDL_CONTROLLER_BUTTON_B: return "B";
    case SDL_CONTROLLER_BUTTON_X: return "X";
    case SDL_CONTROLLER_BUTTON_Y: return "Y";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "L3";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "R3";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "L1";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "R1";
    default: return "?";
  }
}

int controllerButtonFromShortName(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  if (value == "A") return SDL_CONTROLLER_BUTTON_A;
  if (value == "B") return SDL_CONTROLLER_BUTTON_B;
  if (value == "X") return SDL_CONTROLLER_BUTTON_X;
  if (value == "Y") return SDL_CONTROLLER_BUTTON_Y;
  if (value == "L3" || value == "LEFTSTICK") return SDL_CONTROLLER_BUTTON_LEFTSTICK;
  if (value == "R3" || value == "RIGHTSTICK") return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
  if (value == "L1" || value == "LEFTSHOULDER") return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
  if (value == "R1" || value == "RIGHTSHOULDER") return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
  return -1;
}

bool remappableControllerButton(int button) {
  return button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_B ||
         button == SDL_CONTROLLER_BUTTON_X || button == SDL_CONTROLLER_BUTTON_Y ||
         button == SDL_CONTROLLER_BUTTON_LEFTSTICK || button == SDL_CONTROLLER_BUTTON_RIGHTSTICK ||
         button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
}

fs::path controllerRemapFile(const GameInfo& game) {
  return game.path / "mkxp-controller-remap.txt";
}

std::vector<ControllerRemapEntry> loadControllerRemapFile(const GameInfo& game) {
  std::vector<ControllerRemapEntry> out;
  std::ifstream in(controllerRemapFile(game), std::ios::binary);
  if (!in) return out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const int from = controllerButtonFromShortName(line.substr(0, eq));
    const int to = controllerButtonFromShortName(line.substr(eq + 1));
    if (from >= 0 && to >= 0) out.push_back({from, to});
  }
  return out;
}

bool saveControllerRemapFile(const GameInfo& game, const std::vector<ControllerRemapEntry>& entries) {
  std::ofstream out(controllerRemapFile(game), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << "# RPG Maker Player per-game controller remap\n";
  out << "# FROM=TO (A,B,X,Y,L3,R3,L1,R1)\n";
  for (const auto& entry : entries)
    out << controllerButtonShortName(entry.from) << '=' << controllerButtonShortName(entry.to) << '\n';
  return static_cast<bool>(out);
}

struct FilterSortState {
  bool active = false;
  int row = 0;
  int filterIndex = 0;
  int sortIndex = 1;
  int buttonIndex = 0;
};

struct SearchState {
  bool active = false;
  std::string text;
};

enum class MainSection {
  Home = 0,
  Library = 1,
  Settings = 3,
};

struct UiShellState {
  MainSection section = MainSection::Home;
  bool sidebarFocused = false;
  int sidebarIndex = 0;
  float sidebarAnim = 0.0f;
  bool sectionTransitionActive = false;
  MainSection sectionTransitionFrom = MainSection::Home;
  MainSection sectionTransitionTo = MainSection::Home;
  Uint64 sectionTransitionStartedAt = 0;
  bool libraryPageTransitionActive = false;
  int libraryPageTransitionDirection = 0;
  std::size_t libraryPageTransitionFromSelected = 0;
  int libraryPageTransitionFromScroll = 0;
  std::size_t libraryPageTransitionToSelected = 0;
  int libraryPageTransitionToScroll = 0;
  Uint64 libraryPageTransitionStartedAt = 0;
  int homeRow = 0;
  std::size_t homeRecentPos = 0;
  std::size_t homeLibraryPos = 0;
  int settingsRow = 0;
  bool settingsDetailFocused = false;
};

struct UpdateUiState {
  std::string latestVersion;
  std::string message;
  bool checked = false;
  bool available = false;
  bool prompt = false;
  bool promptYes = true;
};

struct PlayHistoryEntry {
  std::int64_t lastPlayed = 0;
  std::uint64_t totalSeconds = 0;
  int launchCount = 0;
};

using PlayHistory = std::unordered_map<std::string, PlayHistoryEntry>;

constexpr std::size_t RECENT_PLAY_LIMIT = 5;

void trimPlayHistory(PlayHistory& history, std::size_t limit = RECENT_PLAY_LIMIT) {
  if (history.size() <= limit) return;
  std::vector<std::pair<std::string, std::int64_t>> ranked;
  ranked.reserve(history.size());
  for (const auto& [key, entry] : history) ranked.push_back({key, entry.lastPlayed});
  std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    return a.second > b.second;
  });
  for (std::size_t i = limit; i < ranked.size(); ++i) history.erase(ranked[i].first);
}

std::string historyGameKey(const GameInfo& game) {
  std::error_code ec;
  const fs::path absolute = fs::absolute(game.path, ec);
  return (ec ? game.path : absolute).lexically_normal().generic_string();
}

fs::path playHistoryPath(const fs::path& root) {
  return root / "cache/ui/play_history.tsv";
}

PlayHistory loadPlayHistory(const fs::path& root) {
  PlayHistory history;
  std::ifstream in(playHistoryPath(root), std::ios::binary);
  std::string key;
  PlayHistoryEntry entry;
  while (in >> std::quoted(key) >> entry.lastPlayed >> entry.totalSeconds >> entry.launchCount) {
    history[key] = entry;
  }
  trimPlayHistory(history);
  return history;
}

void savePlayHistory(const fs::path& root, const PlayHistory& history) {
  std::error_code ec;
  fs::create_directories(playHistoryPath(root).parent_path(), ec);
  std::ofstream out(playHistoryPath(root), std::ios::binary | std::ios::trunc);
  if (!out) return;
  std::vector<std::pair<std::string, PlayHistoryEntry>> entries(history.begin(), history.end());
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    return a.second.lastPlayed > b.second.lastPlayed;
  });
  std::size_t written = 0;
  for (const auto& [key, entry] : entries) {
    if (written >= RECENT_PLAY_LIMIT) break;
    out << std::quoted(key) << ' ' << entry.lastPlayed << ' '
        << entry.totalSeconds << ' ' << entry.launchCount << '\n';
    ++written;
  }
}

std::vector<std::size_t> recentGameIndices(const std::vector<GameInfo>& games,
                                           const PlayHistory& history,
                                           std::size_t limit = RECENT_PLAY_LIMIT) {
  std::vector<std::pair<std::size_t, std::int64_t>> ranked;
  for (std::size_t i = 0; i < games.size(); ++i) {
    const auto it = history.find(historyGameKey(games[i]));
    if (it == history.end() || it->second.lastPlayed <= 0) continue;
    ranked.push_back({i, it->second.lastPlayed});
  }
  std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    return a.second > b.second;
  });
  std::vector<std::size_t> result;
  limit = std::min(limit, RECENT_PLAY_LIMIT);
  for (const auto& [index, _] : ranked) {
    if (result.size() >= limit) break;
    result.push_back(index);
  }
  return result;
}

const char* filterCodeAt(int index) {
  static const char* codes[] = {"all", "easyrpg", "wolf", "xp", "vx", "vxace", "mv", "mz"};
  if (index < 0 || index >= 8) return "all";
  return codes[index];
}

int filterIndexFromCode(const std::string& code) {
  for (int i = 0; i < 8; ++i) {
    if (code == filterCodeAt(i)) return i;
  }
  return 0;
}

std::string filterLabelAt(int index) {
  switch (index) {
    case 1: return "2K/2K3";
    case 2: return "WOLF";
    case 3: return "XP";
    case 4: return "VX";
    case 5: return "VX Ace";
    case 6: return "MV";
    case 7: return "MZ";
    default: return tr(UiKey::AllGames);
  }
}

std::string compactEngineLabel(RgssEngine engine) {
  switch (engine) {
    case RgssEngine::EasyRPG: return "2K/2K3";
    case RgssEngine::WolfRPG: return "WOLF";
    case RgssEngine::XP: return "XP";
    case RgssEngine::VX: return "VX";
    case RgssEngine::VXAce: return "VX Ace";
    case RgssEngine::MV: return "MV";
    case RgssEngine::MZ: return "MZ";
    default: return "?";
  }
}
void eraseLastUtf8Codepoint(std::string& text) {
  if (text.empty()) return;
  std::size_t pos = text.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) --pos;
  text.erase(pos);
}

struct SteamKeyboardRequestResult {
  bool shown = false;
  std::string method;
  std::string diagnostic;
};

SteamKeyboardRequestResult requestSteamOnScreenKeyboard() {
#if defined(__linux__)
  using SteamApiInitFn = bool (*)();
  using SteamUtilsAccessorFn = void* (*)();
  using ShowFloatingKeyboardFn = bool (*)(void*, int, int, int, int, int);

  const char* explicitApi = std::getenv("MKXP_STEAM_API_PATH");
  const std::array<std::string, 4> apiCandidates{{
      explicitApi && *explicitApi ? explicitApi : "",
      "./libsteam_api.so",
      "./lib/libsteam_api.so",
      "libsteam_api.so",
  }};

  for (const auto& candidate : apiCandidates) {
    if (candidate.empty()) continue;
    void* handle = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) continue;
    auto init = reinterpret_cast<SteamApiInitFn>(dlsym(handle, "SteamAPI_Init"));
    auto show = reinterpret_cast<ShowFloatingKeyboardFn>(
        dlsym(handle, "SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput"));
    SteamUtilsAccessorFn utilsAccessor = reinterpret_cast<SteamUtilsAccessorFn>(
        dlsym(handle, "SteamAPI_SteamUtils_v011"));
    if (!utilsAccessor) {
      utilsAccessor = reinterpret_cast<SteamUtilsAccessorFn>(dlsym(handle, "SteamAPI_SteamUtils_v010"));
    }
    if (init && show && utilsAccessor && init()) {
      void* utils = utilsAccessor();
      if (utils && show(utils, 0, 0, 0, 0, 0)) {
        return {true, "steamworks", candidate};
      }
      dlclose(handle);
      return {false, "steamworks", "ShowFloatingGamepadTextInput returned false via " + candidate};
    }
    dlclose(handle);
  }

  static constexpr const char* kSteamDeckKeyboardUrl =
      "steam://open/keyboard?XPosition=0&YPosition=0&Width=0&Height=0&Mode=0";
  // SDL2's Steam Deck X11 backend uses this parameterized deeplink for the
  // floating keyboard. mkxp-z ships an older SDL without SDL_OpenURL, so
  // use the same endpoint directly through the running Steam client.
  const pid_t first = fork();
  if (first == 0) {
    const pid_t second = fork();
    if (second == 0) {
      execlp("steam", "steam", "-ifrunning", kSteamDeckKeyboardUrl, static_cast<char*>(nullptr));
      _exit(127);
    }
    _exit(0);
  }
  if (first > 0) {
    int status = 0;
    waitpid(first, &status, 0);
  }
  return {false, "deeplink-fallback", "Steamworks unavailable or initialization failed"};
#else
  return {false, "unsupported", "non-Linux platform"};
#endif
}

bool pointInRect(float x, float y, const SDL_Rect& rect) {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

std::string asciiFold(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

fs::path nearestExistingDirectory(fs::path path, const fs::path& fallback) {
  std::error_code ec;
  if (path.empty()) path = fallback;
  path = fs::absolute(path, ec).lexically_normal();
  while (!path.empty() && !fs::is_directory(path, ec)) {
    const fs::path parent = path.parent_path();
    if (parent == path || parent.empty()) break;
    path = parent;
  }
  if (fs::is_directory(path, ec)) return path;
  if (fs::is_directory(fallback, ec)) return fs::absolute(fallback, ec).lexically_normal();
  return fs::current_path(ec);
}

void refreshFolderPicker(FolderPickerState& picker) {
  picker.children.clear();
  picker.error.clear();
  std::error_code ec;
  if (!fs::is_directory(picker.current, ec)) {
    picker.error = "not a directory";
    picker.selected = 0;
    picker.scroll = 0;
    return;
  }
  for (const auto& entry : fs::directory_iterator(picker.current, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    std::error_code typeEc;
    if (entry.is_directory(typeEc)) picker.children.push_back(entry.path());
  }
  std::sort(picker.children.begin(), picker.children.end(), [](const fs::path& a, const fs::path& b) {
    const std::string af = asciiFold(a.filename().string());
    const std::string bf = asciiFold(b.filename().string());
    return af == bf ? a.filename().string() < b.filename().string() : af < bf;
  });
  if (picker.selected >= picker.children.size()) picker.selected = picker.children.empty() ? 0 : picker.children.size() - 1;
  picker.scroll = 0;
}

void openFolderPicker(FolderPickerState& picker, const fs::path& start, const fs::path& fallback, bool mandatory) {
  picker.active = true;
  picker.mandatory = mandatory;
  picker.current = nearestExistingDirectory(start, fallback);
  picker.selected = 0;
  picker.scroll = 0;
  refreshFolderPicker(picker);
}

void folderPickerParent(FolderPickerState& picker) {
  if (!picker.active) return;
  const fs::path parent = picker.current.parent_path();
  if (parent.empty() || parent == picker.current) return;
  picker.current = parent;
  picker.selected = 0;
  picker.scroll = 0;
  refreshFolderPicker(picker);
}

void folderPickerEnter(FolderPickerState& picker) {
  if (!picker.active || picker.children.empty() || picker.selected >= picker.children.size()) return;
  picker.current = picker.children[picker.selected];
  picker.selected = 0;
  picker.scroll = 0;
  refreshFolderPicker(picker);
}

bool sameNormalizedPath(const fs::path& a, const fs::path& b) {
  std::error_code ecA;
  std::error_code ecB;
  return fs::absolute(a, ecA).lexically_normal() == fs::absolute(b, ecB).lexically_normal();
}

void migrateLegacyCatalogIfNeeded(const fs::path& launcherRoot,
                                  const fs::path& gameRoot,
                                  const fs::path& storageRoot) {
  if (!sameNormalizedPath(gameRoot, launcherRoot / "game")) return;
  std::error_code ec;
  fs::create_directories(storageRoot / "_image", ec);

  const fs::path legacyList = launcherRoot / "game/gamelist.json";
  const fs::path targetList = storageRoot / "gamelist.json";
  if (fs::is_regular_file(legacyList, ec) && !fs::exists(targetList, ec)) {
    fs::copy_file(legacyList, targetList, fs::copy_options::skip_existing, ec);
  }

  const fs::path legacyImages = launcherRoot / "game/_image";
  if (!fs::is_directory(legacyImages, ec)) return;
  for (const auto& entry : fs::directory_iterator(legacyImages, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    std::error_code typeEc;
    if (!entry.is_regular_file(typeEc)) continue;
    fs::copy_file(entry.path(), storageRoot / "_image" / entry.path().filename(),
                  fs::copy_options::skip_existing, typeEc);
  }
}
fs::path executableRoot() {
#if defined(__linux__)
  char buffer[4096]{};
  const ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (n > 0) {
    buffer[n] = '\0';
    return fs::path(buffer).parent_path();
  }
#endif
  return fs::current_path();
}

std::optional<fs::path> findFont(const fs::path& root) {
  const std::vector<fs::path> candidates = {
    root / "assets/fonts/NotoSansCJKkr-Regular.otf",
    root / "assets/fonts/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/TTF/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto/NotoSansCJKkr-Regular.otf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
  };
  for (const auto& path : candidates) if (fs::exists(path)) return path;
  return std::nullopt;
}

FontSet loadFonts(const fs::path& root) {
  FontSet set;
  const auto fontPath = findFont(root);
  if (!fontPath) return set;
  set.big = TTF_OpenFont(fontPath->string().c_str(), 36);
  set.normal = TTF_OpenFont(fontPath->string().c_str(), 26);
  set.medium = TTF_OpenFont(fontPath->string().c_str(), 20);
  set.small = TTF_OpenFont(fontPath->string().c_str(), 17);
  return set;
}

void closeFonts(FontSet& fonts) {
  if (fonts.big) TTF_CloseFont(fonts.big);
  if (fonts.normal) TTF_CloseFont(fonts.normal);
  if (fonts.medium) TTF_CloseFont(fonts.medium);
  if (fonts.small) TTF_CloseFont(fonts.small);
  fonts = {};
}

int textWidth(TTF_Font* font, const std::string& text) {
  if (!font) return static_cast<int>(text.size()) * 10;
  int w = 0;
  int h = 0;
  if (TTF_SizeUTF8(font, text.c_str(), &w, &h) != 0) return 0;
  return w;
}

int textHeight(TTF_Font* font, const std::string& text = "Ag") {
  if (!font) return 0;
  int w = 0;
  int h = 0;
  if (TTF_SizeUTF8(font, text.empty() ? "Ag" : text.c_str(), &w, &h) != 0) return TTF_FontHeight(font);
  return h;
}

int centeredTextY(TTF_Font* font, const SDL_Rect& rect, const std::string& text = "Ag") {
  return rect.y + std::max(0, (rect.h - textHeight(font, text)) / 2);
}

void popUtf8(std::string& text) {
  if (text.empty()) return;
  std::size_t i = text.size() - 1;
  while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) --i;
  text.erase(i);
}

std::string truncateText(TTF_Font* font, std::string text, int maxWidth) {
  if (textWidth(font, text) <= maxWidth) return text;
  const std::string ellipsis = "…";
  while (!text.empty() && textWidth(font, text + ellipsis) > maxWidth) popUtf8(text);
  return text + ellipsis;
}

void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
              int x, int y, SDL_Color color, bool center = false, bool right = false) {
  if (!font || text.empty()) return;
  SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
  if (!surface) return;
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (!texture) {
    SDL_FreeSurface(surface);
    return;
  }
  SDL_Rect dst{x, y, surface->w, surface->h};
  if (center) dst.x -= dst.w / 2;
  if (right) dst.x -= dst.w;
  SDL_FreeSurface(surface);
  SDL_RenderCopy(renderer, texture, nullptr, &dst);
  SDL_DestroyTexture(texture);
}

void drawMarqueeText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
                     const SDL_Rect& area, SDL_Color color, bool active,
                     const std::string& focusKey) {
  if (!font || text.empty() || area.w <= 0 || area.h <= 0) return;
  static std::string lastFocusKey;
  static Uint64 focusSince = 0;
  const Uint64 now = SDL_GetTicks64();
  if (active && focusKey != lastFocusKey) {
    lastFocusKey = focusKey;
    focusSince = now;
  }

  const int fullWidth = textWidth(font, text);
  if (!active || fullWidth <= area.w) {
    drawText(renderer, font, truncateText(font, text, area.w), area.x, area.y, color);
    return;
  }

  SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
  if (!surface) return;
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (!texture) {
    SDL_FreeSurface(surface);
    return;
  }

  constexpr Uint64 START_PAUSE_MS = 800;
  constexpr Uint64 END_PAUSE_MS = 650;
  constexpr double SPEED_PX_PER_SEC = 42.0;
  const int overflow = std::max(0, surface->w - area.w);
  const Uint64 travelMs = std::max<Uint64>(1, static_cast<Uint64>((overflow / SPEED_PX_PER_SEC) * 1000.0));
  const Uint64 cycleMs = START_PAUSE_MS + travelMs + END_PAUSE_MS + travelMs + END_PAUSE_MS;
  const Uint64 elapsed = now >= focusSince ? now - focusSince : 0;
  const Uint64 phase = cycleMs > 0 ? elapsed % cycleMs : 0;
  double offset = 0.0;
  if (phase < START_PAUSE_MS) {
    offset = 0.0;
  } else if (phase < START_PAUSE_MS + travelMs) {
    offset = overflow * static_cast<double>(phase - START_PAUSE_MS) / travelMs;
  } else if (phase < START_PAUSE_MS + travelMs + END_PAUSE_MS) {
    offset = overflow;
  } else if (phase < START_PAUSE_MS + travelMs + END_PAUSE_MS + travelMs) {
    const Uint64 back = phase - START_PAUSE_MS - travelMs - END_PAUSE_MS;
    offset = overflow * (1.0 - static_cast<double>(back) / travelMs);
  }

  const SDL_bool hadClip = SDL_RenderIsClipEnabled(renderer);
  SDL_Rect oldClip{};
  SDL_RenderGetClipRect(renderer, &oldClip);
  SDL_RenderSetClipRect(renderer, &area);
  SDL_Rect dst{area.x - static_cast<int>(std::lround(offset)), area.y, surface->w, surface->h};
  SDL_FreeSurface(surface);
  SDL_RenderCopy(renderer, texture, nullptr, &dst);
  SDL_DestroyTexture(texture);
  if (hadClip) SDL_RenderSetClipRect(renderer, &oldClip);
  else SDL_RenderSetClipRect(renderer, nullptr);
}

void drawCenteredMarqueeText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
                             const SDL_Rect& area, SDL_Color color, bool active,
                             const std::string& focusKey) {
  if (!font || text.empty() || area.w <= 0 || area.h <= 0) return;
  if (textWidth(font, text) <= area.w) {
    drawText(renderer, font, text, area.x + area.w / 2, centeredTextY(font, area, text), color, true);
    return;
  }
  SDL_Rect marqueeArea = area;
  marqueeArea.y = centeredTextY(font, area, text);
  marqueeArea.h = textHeight(font, text);
  drawMarqueeText(renderer, font, text, marqueeArea, color, active, focusKey);
}

void fillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color c) {
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
  SDL_RenderFillRect(renderer, &rect);
}

int currentUiOffsetY(SDL_Renderer* renderer) {
  int logicalW = WIDTH;
  int logicalH = HEIGHT;
  SDL_RenderGetLogicalSize(renderer, &logicalW, &logicalH);
  return std::max(0, (logicalH - HEIGHT) / 2);
}

void setUiContentViewport(SDL_Renderer* renderer) {
  SDL_Rect viewport{0, currentUiOffsetY(renderer), WIDTH, HEIGHT};
  SDL_RenderSetViewport(renderer, &viewport);
}

int roundedInset(int y, int height, int radius) {
  if (radius <= 0 || height <= 0) return 0;
  radius = std::min(radius, height / 2);
  int edge = -1;
  if (y < radius) edge = radius - 1 - y;
  else if (y >= height - radius) edge = y - (height - radius);
  if (edge < 0) return 0;
  const double rr = static_cast<double>(radius) * radius;
  const double ee = static_cast<double>(edge) * edge;
  return std::max(0, radius - static_cast<int>(std::sqrt(std::max(0.0, rr - ee))));
}

void fillRoundedRect(SDL_Renderer* renderer, const SDL_Rect& rect, int radius, SDL_Color c) {
  if (rect.w <= 0 || rect.h <= 0) return;
  radius = std::max(0, std::min(radius, std::min(rect.w, rect.h) / 2));
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
  if (radius == 0) {
    SDL_RenderFillRect(renderer, &rect);
    return;
  }
  SDL_Rect middle{rect.x, rect.y + radius, rect.w, rect.h - radius * 2};
  if (middle.h > 0) SDL_RenderFillRect(renderer, &middle);
  for (int y = 0; y < radius; ++y) {
    const int inset = roundedInset(y, rect.h, radius);
    SDL_RenderDrawLine(renderer, rect.x + inset, rect.y + y,
                      rect.x + rect.w - inset - 1, rect.y + y);
    const int bottomY = rect.y + rect.h - 1 - y;
    SDL_RenderDrawLine(renderer, rect.x + inset, bottomY,
                      rect.x + rect.w - inset - 1, bottomY);
  }
}

void strokeRoundedRect(SDL_Renderer* renderer, const SDL_Rect& rect, int radius,
                       SDL_Color c, int width = 1) {
  if (rect.w <= 0 || rect.h <= 0) return;
  width = std::max(1, std::min(width, std::min(rect.w, rect.h) / 2));
  radius = std::max(0, std::min(radius, std::min(rect.w, rect.h) / 2));
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);

  const int innerW = rect.w - width * 2;
  const int innerH = rect.h - width * 2;
  const int innerRadius = std::max(0, radius - width);
  for (int y = 0; y < rect.h; ++y) {
    const int outerInset = roundedInset(y, rect.h, radius);
    const int outerL = rect.x + outerInset;
    const int outerR = rect.x + rect.w - outerInset - 1;
    if (outerL > outerR) continue;

    const int innerY = y - width;
    if (innerW <= 0 || innerH <= 0 || innerY < 0 || innerY >= innerH) {
      SDL_RenderDrawLine(renderer, outerL, rect.y + y, outerR, rect.y + y);
      continue;
    }

    const int innerInset = roundedInset(innerY, innerH, innerRadius);
    const int innerL = rect.x + width + innerInset;
    const int innerR = rect.x + rect.w - width - innerInset - 1;
    if (outerL < innerL)
      SDL_RenderDrawLine(renderer, outerL, rect.y + y, innerL - 1, rect.y + y);
    if (innerR < outerR)
      SDL_RenderDrawLine(renderer, innerR + 1, rect.y + y, outerR, rect.y + y);
  }
}
void drawEngineBadge(SDL_Renderer* renderer, TTF_Font* font, RgssEngine engine,
                     const SDL_Rect& imageRect) {
  const std::string label = compactEngineLabel(engine);
  const int badgeH = 24;
  const int badgeW = std::max(44, textWidth(font, label) + 20);
  const SDL_Rect badge{imageRect.x + 8, imageRect.y + imageRect.h - badgeH - 8, badgeW, badgeH};
  fillRoundedRect(renderer, badge, badgeH / 2, SDL_Color{82, 88, 96, 102});
  strokeRoundedRect(renderer, badge, badgeH / 2, SDL_Color{190, 201, 211, 72}, 1);
  drawText(renderer, font, label, badge.x + badge.w / 2,
           centeredTextY(font, badge, label), SDL_Color{225, 239, 249, 255}, true);
}

void fillCircle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color c) {
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
  for (int y = -radius; y <= radius; ++y) {
    const int span = static_cast<int>(std::sqrt(std::max(0, radius * radius - y * y)));
    SDL_RenderDrawLine(renderer, cx - span, cy + y, cx + span, cy + y);
  }
}

void drawPadHint(SDL_Renderer* renderer, const FontSet& fonts, int x, int y,
                 const char* button, SDL_Color buttonColor, const std::string& label) {
  fillCircle(renderer, x, y, 13, buttonColor);
  const SDL_Rect buttonBox{x - 13, y - 13, 26, 26};
  drawText(renderer, fonts.small, button, x, centeredTextY(fonts.small, buttonBox, button), UI_NAVY, true);
  const SDL_Rect labelBox{x + 22, y - 13, textWidth(fonts.small, label), 26};
  drawText(renderer, fonts.small, label, labelBox.x, centeredTextY(fonts.small, labelBox, label), WHITE);
}

std::string uiWord(const char* ko, const char* en, const char* ja) {
  if (gUiLanguage == UiLanguage::Korean) return ko;
  if (gUiLanguage == UiLanguage::Japanese) return ja;
  return en;
}

std::string gameLocaleUiLabel(GameLocale locale) {
  switch (locale) {
    case GameLocale::Japanese: return uiWord("일본어", "Japanese", "日本語");
    case GameLocale::English: return uiWord("영어", "English", "英語");
    case GameLocale::Korean:
    default: return uiWord("한국어", "Korean", "韓国語");
  }
}

void drawNavIcon(SDL_Renderer* renderer, int kind, int cx, int cy, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  if (kind == 0) {
    SDL_RenderDrawLine(renderer, cx - 11, cy + 1, cx, cy - 10);
    SDL_RenderDrawLine(renderer, cx, cy - 10, cx + 11, cy + 1);
    SDL_Rect body{cx - 8, cy, 16, 12};
    SDL_RenderDrawRect(renderer, &body);
  } else if (kind == 1) {
    SDL_Rect a{cx - 12, cy - 10, 6, 20};
    SDL_Rect b{cx - 3, cy - 10, 6, 20};
    SDL_Rect d{cx + 6, cy - 10, 6, 20};
    SDL_RenderDrawRect(renderer, &a);
    SDL_RenderDrawRect(renderer, &b);
    SDL_RenderDrawRect(renderer, &d);
  } else if (kind == 2) {
    const int r = 8;
    for (int a = 0; a < 360; a += 15) {
      const double rad = a * 3.14159265358979323846 / 180.0;
      SDL_RenderDrawPoint(renderer,
                          cx - 3 + static_cast<int>(std::cos(rad) * r),
                          cy - 3 + static_cast<int>(std::sin(rad) * r));
    }
    SDL_RenderDrawLine(renderer, cx + 3, cy + 4, cx + 11, cy + 12);
  } else {
    fillCircle(renderer, cx, cy, 10, color);
    fillCircle(renderer, cx, cy, 4, UI_NAVY);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i < 4; ++i) {
      const int dx = (i % 2 == 0) ? -13 : 10;
      const int dy = (i < 2) ? -3 : 7;
      SDL_Rect tooth{cx + dx, cy + dy, 4, 7};
      SDL_RenderFillRect(renderer, &tooth);
    }
  }
}

void drawBrandLogo(SDL_Renderer* renderer, const SDL_Rect& box) {
  static const std::array<const char*, 20> pixels{{
    ".................BD.",
    "......DDDDDDDDD..B..",
    "......BWWWWWWWDDD...",
    ".....BBWWWWWWWBBBB..",
    "...DDBWWWWWWWWWWBB..",
    "....WWWWWWWWWWWWWBD.",
    "DDBBWWWWWWWWWWWWWBBD",
    "BWWWWWWWWWWWWBBBBBWB",
    "BWWWWWWWWWWWWBBBWWWB",
    "BWWWWWWWWWWBBBWWWWWB",
    "BWWWWWWWWWWBBWBBWWWD",
    "BWWWWWWWBBBBWWBBBWWD",
    ".DBWWWWBBBWWWBWBWBDD",
    "..BWWWBBBWWWWBWBBD..",
    "...BWBBWBBWWWBWBBD..",
    "...DWBBWBWWWWBWBB...",
    "....WBBWWWWWWWWBD...",
    "......BWWWWWWWDB....",
    ".....DBBBBBBBBDB....",
    "....BBBBBBBBBBDBD...",
  }};
  const int cell = std::max(1, std::min(box.w, box.h) / 20);
  const int ox = box.x + (box.w - cell * 20) / 2;
  const int oy = box.y + (box.h - cell * 20) / 2;
  const SDL_Color white{239, 248, 255, 255};
  const SDL_Color cyan{77, 177, 248, 255};
  const SDL_Color blue{38, 104, 194, 255};
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (int y = 0; y < 20; ++y) {
    for (int x = 0; x < 20; ++x) {
      const char px = pixels[static_cast<std::size_t>(y)][x];
      if (px == '.') continue;
      const SDL_Color color = px == 'W' ? white : (px == 'B' ? cyan : blue);
      SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
      const SDL_Rect r{ox + x * cell, oy + y * cell, cell, cell};
      SDL_RenderFillRect(renderer, &r);
    }
  }
}

#if defined(__linux__)
int gSingleInstanceLockFd = -1;

void drawFrontendStandby(SDL_Renderer* renderer) {
  SDL_RenderSetViewport(renderer, nullptr);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  SDL_RenderPresent(renderer);
}

int waitForGameChild(pid_t pid, SDL_Window* window, SDL_Renderer* renderer,
                     const FontSet& fonts, SDL_GameController* controller,
                     const fs::path& exitRequestFile,
                     bool allowSteamKeyboardShortcut = false) {
  (void)window;
  (void)renderer;
  (void)fonts;

  int childStatus = 0;
  bool waitRelease = false;
  Uint64 comboStarted = 0;
  bool keyboardWaitRelease = false;
  Uint64 keyboardComboStarted = 0;
  Uint64 terminateRequestedAt = 0;
  Uint64 nextControllerProbe = 0;
  Uint64 nextEvdevProbe = 0;
  SDL_GameController* fallbackController = nullptr;
  std::vector<int> evdevPads;

  setpgid(pid, pid);

  auto clearExitRequest = [&]() {
    std::ofstream out(exitRequestFile, std::ios::binary | std::ios::trunc);
  };
  clearExitRequest();

  auto logExit = [&](const std::string& message) {
    std::ofstream exitLog(executableRoot() / "logs/MKXP_Launcher.log", std::ios::app);
    if (exitLog) {
      exitLog << message << '\n';
      exitLog.flush();
    }
  };

  auto activeController = [&]() -> SDL_GameController* {
    if (controller && SDL_GameControllerGetAttached(controller)) return controller;
    if (fallbackController && SDL_GameControllerGetAttached(fallbackController)) return fallbackController;

    if (fallbackController) {
      SDL_GameControllerClose(fallbackController);
      fallbackController = nullptr;
    }

    const Uint64 now = SDL_GetTicks64();
    if (now < nextControllerProbe) return nullptr;
    nextControllerProbe = now + 500;
    SDL_JoystickUpdate();
    const int count = SDL_NumJoysticks();
    for (int i = 0; i < count; ++i) {
      if (!SDL_IsGameController(i)) continue;
      fallbackController = SDL_GameControllerOpen(i);
      if (fallbackController) return fallbackController;
    }
    return nullptr;
  };

  auto refreshEvdevPads = [&]() {
    for (const int fd : evdevPads) close(fd);
    evdevPads.clear();
    std::error_code inputEc;
    const fs::path inputRoot("/dev/input");
    if (!fs::is_directory(inputRoot, inputEc)) return;
    for (const auto& entry : fs::directory_iterator(inputRoot, fs::directory_options::skip_permission_denied, inputEc)) {
      if (inputEc) break;
      const std::string name = entry.path().filename().string();
      if (name.rfind("event", 0) != 0) continue;
      const int fd = open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0) continue;
      unsigned long keyBits[(KEY_MAX + (8 * sizeof(unsigned long))) / (8 * sizeof(unsigned long))]{};
      if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) {
        close(fd);
        continue;
      }
      constexpr int bitsPerWord = 8 * static_cast<int>(sizeof(unsigned long));
      const auto hasKey = [&](int code) {
        return (keyBits[code / bitsPerWord] & (1UL << (code % bitsPerWord))) != 0;
      };
      const bool relevant = hasKey(BTN_START) || hasKey(BTN_SELECT) ||
                            hasKey(BTN_NORTH) || hasKey(BTN_WEST) || hasKey(KEY_X);
      if (relevant) evdevPads.push_back(fd);
      else close(fd);
    }
  };

  struct EvdevGameButtons {
    bool exitCombo = false;
    bool keyboardCombo = false;
  };

  auto readEvdevButtons = [&]() -> EvdevGameButtons {
    EvdevGameButtons buttons;
    const Uint64 now = SDL_GetTicks64();
    if (now >= nextEvdevProbe) {
      refreshEvdevPads();
      nextEvdevProbe = now + 2000;
    }
    constexpr int bitsPerWord = 8 * static_cast<int>(sizeof(unsigned long));
    unsigned long keyState[(KEY_MAX + bitsPerWord) / bitsPerWord]{};
    bool start = false;
    bool select = false;
    bool north = false;
    bool west = false;
    bool keyX = false;
    for (const int fd : evdevPads) {
      std::fill(std::begin(keyState), std::end(keyState), 0UL);
      if (ioctl(fd, EVIOCGKEY(sizeof(keyState)), keyState) < 0) continue;
      const auto down = [&](int code) {
        return (keyState[code / bitsPerWord] & (1UL << (code % bitsPerWord))) != 0;
      };
      start = start || down(BTN_START);
      select = select || down(BTN_SELECT);
      // Linux letter aliases, positional virtual pads and Steam keyboard
      // translations do not always expose the Deck face buttons on the same
      // evdev node.  Aggregate all known X candidates across relevant nodes.
      north = north || down(BTN_NORTH);
      west = west || down(BTN_WEST);
      keyX = keyX || down(KEY_X);
    }
    buttons.exitCombo = start && select;
    buttons.keyboardCombo = select && (north || west || keyX) && !start;
    return buttons;
  };

  auto requestDirectExit = [&](const char* source) {
    if (terminateRequestedAt != 0) return;
    clearExitRequest();
    kill(-pid, SIGTERM);
    terminateRequestedAt = SDL_GetTicks64();
    comboStarted = 0;
    waitRelease = true;
    logExit(std::string("exit combo | direct exit ") + source +
            " holdMs=" + std::to_string(GAME_EXIT_COMBO_HOLD_MS) +
            " pid=" + std::to_string(pid));
  };

  while (true) {
    const pid_t result = waitpid(pid, &childStatus, WNOHANG);
    if (result == pid) break;
    if (result < 0) break;

    SDL_PumpEvents();
    SDL_GameControllerUpdate();

    if (terminateRequestedAt == 0) {
      std::error_code requestEc;
      const auto requestSize = fs::file_size(exitRequestFile, requestEc);
      if (!requestEc && requestSize > 0) {
        requestDirectExit("request-file");
      }
    }

    bool comboPressed = false;
    bool keyboardComboPressed = false;
    if (SDL_GameController* pad = activeController()) {
      const bool startPressed = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START) != 0;
      const bool selectPressed = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK) != 0;
      const bool xPressed = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X) != 0;
      comboPressed = startPressed && selectPressed;
      keyboardComboPressed = selectPressed && xPressed && !startPressed;
    }
    const EvdevGameButtons evdevButtons = readEvdevButtons();
    comboPressed = comboPressed || evdevButtons.exitCombo;
    keyboardComboPressed = keyboardComboPressed || evdevButtons.keyboardCombo;

    if (terminateRequestedAt == 0) {
      if (waitRelease) {
        if (!comboPressed) waitRelease = false;
      } else if (comboPressed) {
        if (comboStarted == 0) comboStarted = SDL_GetTicks64();
        if (SDL_GetTicks64() - comboStarted >= GAME_EXIT_COMBO_HOLD_MS) {
          requestDirectExit("parent-input");
        }
      } else {
        comboStarted = 0;
      }
    }

    if (allowSteamKeyboardShortcut && terminateRequestedAt == 0) {
      if (keyboardWaitRelease) {
        if (!keyboardComboPressed) keyboardWaitRelease = false;
      } else if (keyboardComboPressed) {
        if (keyboardComboStarted == 0) keyboardComboStarted = SDL_GetTicks64();
        if (SDL_GetTicks64() - keyboardComboStarted >= GAME_KEYBOARD_COMBO_HOLD_MS) {
          const SteamKeyboardRequestResult keyboardRequest = requestSteamOnScreenKeyboard();
          keyboardComboStarted = 0;
          keyboardWaitRelease = true;
          logExit(std::string("game keyboard | requested source=parent-input combo=Select+X holdMs=") +
                  std::to_string(GAME_KEYBOARD_COMBO_HOLD_MS) +
                  " pid=" + std::to_string(pid) +
                  " action=open" +
                  " method=" + keyboardRequest.method +
                  " shown=" + (keyboardRequest.shown ? "1" : "0") +
                  " diagnostic=" + keyboardRequest.diagnostic);
        }
      } else {
        keyboardComboStarted = 0;
      }
    } else {
      keyboardComboStarted = 0;
      keyboardWaitRelease = false;
    }

    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT && terminateRequestedAt == 0) {
        kill(-pid, SIGTERM);
        terminateRequestedAt = SDL_GetTicks64();
        logExit("exit combo | direct exit window-close pid=" + std::to_string(pid));
      }
    }

    if (terminateRequestedAt != 0 &&
        SDL_GetTicks64() - terminateRequestedAt >= 2500) {
      kill(-pid, SIGKILL);
      terminateRequestedAt = SDL_GetTicks64();
    }

    SDL_Delay(16);
  }

  clearExitRequest();
  if (fallbackController) SDL_GameControllerClose(fallbackController);
  for (const int fd : evdevPads) close(fd);
  return childStatus;
}
#endif

void drawCover(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect& dst) {
  int tw = 1;
  int th = 1;
  SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
  const double targetRatio = static_cast<double>(dst.w) / dst.h;
  const double sourceRatio = static_cast<double>(tw) / th;
  SDL_Rect src{0, 0, tw, th};
  if (sourceRatio > targetRatio) {
    src.w = static_cast<int>(th * targetRatio);
    src.x = (tw - src.w) / 2;
  } else {
    src.h = static_cast<int>(tw / targetRatio);
    src.y = (th - src.h) / 2;
  }
  SDL_RenderCopy(renderer, texture, &src, &dst);
}

void drawCoverRounded(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect& dst, int radius) {
  if (!texture || dst.w <= 0 || dst.h <= 0) return;
  int tw = 1;
  int th = 1;
  SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
  const double targetRatio = static_cast<double>(dst.w) / dst.h;
  const double sourceRatio = static_cast<double>(tw) / th;
  SDL_Rect src{0, 0, tw, th};
  if (sourceRatio > targetRatio) {
    src.w = static_cast<int>(th * targetRatio);
    src.x = (tw - src.w) / 2;
  } else {
    src.h = static_cast<int>(tw / targetRatio);
    src.y = (th - src.h) / 2;
  }
  radius = std::max(0, std::min(radius, std::min(dst.w, dst.h) / 2));
  const SDL_bool hadClip = SDL_RenderIsClipEnabled(renderer);
  SDL_Rect oldClip{};
  SDL_RenderGetClipRect(renderer, &oldClip);
  SDL_Rect middle{dst.x, dst.y + radius, dst.w, dst.h - radius * 2};
  if (middle.h > 0) {
    SDL_RenderSetClipRect(renderer, &middle);
    SDL_RenderCopy(renderer, texture, &src, &dst);
  }
  for (int y = 0; y < radius; ++y) {
    const int inset = roundedInset(y, dst.h, radius);
    SDL_Rect topClip{dst.x + inset, dst.y + y, dst.w - inset * 2, 1};
    SDL_RenderSetClipRect(renderer, &topClip);
    SDL_RenderCopy(renderer, texture, &src, &dst);
    SDL_Rect bottomClip{dst.x + inset, dst.y + dst.h - 1 - y, dst.w - inset * 2, 1};
    SDL_RenderSetClipRect(renderer, &bottomClip);
    SDL_RenderCopy(renderer, texture, &src, &dst);
  }
  if (hadClip) SDL_RenderSetClipRect(renderer, &oldClip);
  else SDL_RenderSetClipRect(renderer, nullptr);
}
fs::path runtimeRootFor(const fs::path& root, const std::string& runtimeName) {
  if (runtimeName == "ruby18") return root / "runtime/ruby18";
  if (runtimeName == "ruby19") return root / "runtime/ruby19";
  return root / "runtime/ruby31";
}

std::optional<fs::path> resolveMkxpBinary(const fs::path& root, const std::string& runtimeName) {
  const fs::path runtimeRoot = runtimeRootFor(root, runtimeName);
  const std::vector<fs::path> candidates = {
    runtimeRoot / "mkxp-z",
    root / "runtime/mkxp-z",
  };
  for (const auto& path : candidates) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) return fs::absolute(path);
  }
  return std::nullopt;
}

std::string portableRubyLoadPath(const fs::path& runtimeRoot, const std::string& runtimeName) {
  std::vector<fs::path> paths;
  if (runtimeName == "ruby18") {
    paths = {
      runtimeRoot / "ruby/lib/ruby/1.8",
      runtimeRoot / "ruby/lib/ruby/1.8/x86_64-linux",
      runtimeRoot / "ruby/lib/ruby/site_ruby/1.8",
      runtimeRoot / "ruby/lib/ruby/site_ruby/1.8/x86_64-linux",
    };
  } else if (runtimeName == "ruby19") {
    paths = {
      runtimeRoot / "nothreaded_ruby/lib/ruby/1.9.1",
      runtimeRoot / "nothreaded_ruby/lib/ruby/1.9.1/x86_64-linux",
      runtimeRoot / "nothreaded_ruby/lib/ruby/site_ruby/1.9.1",
      runtimeRoot / "nothreaded_ruby/lib/ruby/site_ruby/1.9.1/x86_64-linux",
    };
  } else {
    paths = {
      runtimeRoot / "stdlib",
      runtimeRoot / "stdlib/x86_64-linux",
    };
  }

  std::string out;
  for (const auto& path : paths) {
    std::error_code ec;
    if (!fs::exists(path, ec)) continue;
    if (!out.empty()) out += ':';
    out += fs::absolute(path).string();
  }
  return out;
}

std::string rubyShortName(const std::string& runtimeName) {
  if (runtimeName == "ruby18") return "1.8";
  if (runtimeName == "ruby19") return "1.9";
  return "3.1";
}

bool rubyIsManual(const GameInfo& game) {
  return game.rubyDetectionSource.rfind("manual:", 0) == 0;
}

std::string configuredRubyMode(const GameInfo& game) {
  std::ifstream in(game.path / "mkxp-ruby.txt", std::ios::binary);
  if (!in) return "auto";
  std::string mode;
  std::getline(in, mode);
  while (!mode.empty() && (mode.back() == '\r' || mode.back() == '\n' || mode.back() == ' ' || mode.back() == '\t')) mode.pop_back();
  std::size_t start = 0;
  while (start < mode.size() && (mode[start] == ' ' || mode[start] == '\t')) ++start;
  mode = mode.substr(start);
  if (mode == "ruby18" || mode == "ruby19" || mode == "ruby31") return mode;
  return "auto";
}

std::string rubyUiLabel(const GameInfo& game) {
  if (game.rubyDetectionSource == "deferred:auto") return tr(UiKey::Automatic);
  return (rubyIsManual(game) ? "RUBY " : std::string(tr(UiKey::AutoPrefix))) + rubyShortName(game.rubyRuntime);
}

std::string rubySettingText(const GameInfo& game) {
  if (!isRgssEngine(game.engine)) return {};
  if (game.rubyDetectionSource == "deferred:auto") return tr(UiKey::Automatic);
  if (!rubyIsManual(game)) {
    return std::string(tr(UiKey::Automatic)) + " -> Ruby " + rubyShortName(game.rubyRuntime);
  }
  return "Ruby " + rubyShortName(game.rubyRuntime);
}

std::vector<int> versionNumbers(const std::string& text) {
  std::vector<int> out;
  int value = -1;
  for (unsigned char c : text) {
    if (std::isdigit(c)) {
      if (value < 0) value = 0;
      value = value * 10 + (c - '0');
    } else if (value >= 0) {
      out.push_back(value);
      value = -1;
    }
  }
  if (value >= 0) out.push_back(value);
  return out;
}

std::vector<std::string> installedNwjsVersions(const fs::path& root) {
  std::vector<std::string> out;
  const fs::path base = root / "runtime/nwjs";
  std::error_code ec;
  if (!fs::is_directory(base, ec)) return out;
  for (const auto& entry : fs::directory_iterator(base, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    if (!entry.is_directory(ec)) continue;
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] == '_' || name[0] == '.') continue;
    if (fs::is_regular_file(entry.path() / "nw", ec)) out.push_back(name);
  }
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    return versionNumbers(a) > versionNumbers(b);
  });
  return out;
}

std::string normalizeNwjsVersion(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
    return c == '\r' || c == '\n' || c == ' ' || c == '\t';
  }), value.end());
  if (!value.empty() && value[0] != 'v' && std::isdigit(static_cast<unsigned char>(value[0]))) value.insert(value.begin(), 'v');
  return value;
}

std::optional<fs::path> findDirectChildFileCI(const fs::path& parent, const std::string& wanted) {
  std::error_code ec;
  if (!fs::is_directory(parent, ec)) return std::nullopt;
  std::string target = wanted;
  std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  for (const auto& entry : fs::directory_iterator(parent, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    std::string name = entry.path().filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (name == target) return entry.path();
  }
  return std::nullopt;
}

std::string detectNwjsVersionFromDll(const fs::path& gamePath) {
  const auto dll = findDirectChildFileCI(gamePath, "nw.dll");
  if (!dll) return {};
  std::ifstream in(*dll, std::ios::binary);
  if (!in) return {};

  const std::string needle = "process.versions['nw'] = '";
  std::string carry;
  std::vector<char> chunk(1024 * 1024);
  while (in) {
    in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const std::streamsize got = in.gcount();
    if (got <= 0) break;
    std::string data = carry + std::string(chunk.data(), static_cast<std::size_t>(got));
    const auto pos = data.find(needle);
    if (pos != std::string::npos) {
      const std::size_t start = pos + needle.size();
      const std::size_t end = data.find('\'', start);
      if (end != std::string::npos && end > start && end - start < 32) {
        return normalizeNwjsVersion(data.substr(start, end - start));
      }
    }
    const std::size_t keep = std::min<std::size_t>(data.size(), needle.size() + 40);
    carry = data.substr(data.size() - keep);
  }
  return {};
}

int nwjsMinorVersion(const std::string& version) {
  const auto numbers = versionNumbers(version);
  return numbers.size() >= 2 ? numbers[1] : -1;
}

std::string nwjsCacheKey(const GameInfo& game) {
  const std::string input = fs::absolute(game.path).generic_string();
  std::uint64_t hash = 1469598103934665603ULL;
  for (unsigned char c : input) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str();
}

std::string fileStamp(const fs::path& path) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) return "missing";
  const auto size = fs::file_size(path, ec);
  if (ec) return "error";
  const auto stamp = fs::last_write_time(path, ec);
  if (ec) return "error";
  std::ostringstream out;
  out << fs::absolute(path).generic_string() << ':' << size << ':'
      << stamp.time_since_epoch().count();
  return out.str();
}

std::string nwjsCompatibilitySignature(const fs::path& root, const GameInfo& game) {
  std::ostringstream out;
  out << "nwscan-v1|" << fs::absolute(game.path).generic_string()
      << "|engine=" << engineLabel(game.engine);
  if (const auto dll = findDirectChildFileCI(game.path, "nw.dll"))
    out << "|nw=" << fileStamp(*dll);
  const fs::path core = game.engine == RgssEngine::MZ
    ? game.webRoot / "js/rmmz_core.js"
    : game.webRoot / "js/rpg_core.js";
  out << "|core=" << fileStamp(core) << "|installed=";
  const auto versions = installedNwjsVersions(root);
  for (std::size_t i = 0; i < versions.size(); ++i) {
    if (i) out << ',';
    out << versions[i];
  }
  return out.str();
}

bool readNwjsCache(const fs::path& root, const GameInfo& game, std::string& runtime) {
  std::ifstream in(root / "cache/nwjs_runtime" / (nwjsCacheKey(game) + ".txt"), std::ios::binary);
  if (!in) return false;
  std::string signature;
  std::string cachedRuntime;
  std::getline(in, signature);
  std::getline(in, cachedRuntime);
  cachedRuntime = normalizeNwjsVersion(cachedRuntime);
  if (signature != nwjsCompatibilitySignature(root, game)) return false;
  if (!fs::is_regular_file(root / "runtime/nwjs" / cachedRuntime / "nw")) return false;
  runtime = cachedRuntime;
  return true;
}

void writeNwjsCache(const fs::path& root, const GameInfo& game, const std::string& runtime) {
  if (!fs::is_regular_file(root / "runtime/nwjs" / runtime / "nw")) return;
  std::error_code ec;
  const fs::path dir = root / "cache/nwjs_runtime";
  fs::create_directories(dir, ec);
  if (ec) return;
  std::ofstream out(dir / (nwjsCacheKey(game) + ".txt"), std::ios::binary | std::ios::trunc);
  if (!out) return;
  out << nwjsCompatibilitySignature(root, game) << '\n' << runtime << '\n';
}

std::string chooseAutoNwjsRuntime(const fs::path& root, const GameInfo& game, std::string& diagnostic) {
  const auto versions = installedNwjsVersions(root);
  if (versions.empty()) {
    diagnostic = "auto:no-nwjs-installed";
    return {};
  }
  const std::string original = detectNwjsVersionFromDll(game.path);
  if (game.engine == RgssEngine::MV && !original.empty() && nwjsMinorVersion(original) >= 0 &&
      nwjsMinorVersion(original) < 50) {
    for (const auto& version : versions) {
      if (nwjsMinorVersion(version) >= 50) {
        diagnostic = "auto:legacy-mv-modernized:" + original + "->" + version;
        return version;
      }
    }
  }
  if (!original.empty()) {
    if (std::find(versions.begin(), versions.end(), original) != versions.end()) {
      diagnostic = "auto:game-nw.dll:" + original;
      return original;
    }
    diagnostic = "auto:game-nw.dll:" + original + "-not-installed-fallback:" + versions.front();
    return versions.front();
  }
  diagnostic = "auto:newest-installed:" + versions.front();
  return versions.front();
}

std::string detectNwjsRuntime(const fs::path& root, const GameInfo& game, std::string& diagnostic) {
  const fs::path overridePath = game.path / "mkxp-nwjs.txt";
  std::ifstream overrideFile(overridePath, std::ios::binary);
  if (overrideFile) {
    std::string line;
    std::getline(overrideFile, line);
    line = normalizeNwjsVersion(line);
    if (!line.empty() && line != "auto") {
      diagnostic = fs::is_regular_file(root / "runtime/nwjs" / line / "nw")
        ? "manual:" + line
        : "manual:missing:" + line;
      return line;
    }
  }
  std::string cached;
  if (readNwjsCache(root, game, cached)) {
    diagnostic = "cache:" + cached;
    return cached;
  }
  return chooseAutoNwjsRuntime(root, game, diagnostic);
}

bool nwjsIsManual(const GameInfo& game) {
  return game.nwjsDetectionSource.rfind("manual:", 0) == 0;
}

std::string nwjsSettingText(const GameInfo& game) {
  if (!isWebEngine(game.engine)) return {};
  if (game.nwjsRuntime.empty()) return tr(UiKey::NwjsMissingShort);
  if (nwjsIsManual(game)) return "NW.js " + game.nwjsRuntime;
  return std::string(tr(UiKey::Automatic)) + " -> NW.js " + game.nwjsRuntime;
}

struct NwjsProbeResult {
  std::string version;
  std::string state;
  std::string detail;
};

NwjsProbeResult runNwjsCompatibilityProbe(const fs::path& root, const GameInfo& game,
                                          const std::string& version) {
  NwjsProbeResult result{version, "FAIL", "probe unavailable"};
#if defined(__linux__)
  const fs::path script = root / "runtime/mvmz/launch_mvmz.sh";
  const fs::path nw = root / "runtime/nwjs" / version / "nw";
  if (!fs::is_regular_file(script) || !fs::is_regular_file(nw) || game.webRoot.empty()) {
    result.detail = "runtime or webroot missing";
    return result;
  }

  std::error_code ec;
  const fs::path markerDir = root / "cache/nwjs_probe";
  fs::create_directories(markerDir, ec);
  if (ec) {
    result.detail = "probe cache unavailable";
    return result;
  }

  std::string safeVersion = version;
  for (char& c : safeVersion) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') c = '_';
  }
  const std::string key = nwjsCacheKey(game);
  const fs::path marker = markerDir / (key + "_" + safeVersion + ".txt");
  const fs::path probeLog = root / "logs" / ("nwprobe_" + key + "_" + safeVersion + ".log");
  fs::remove(marker, ec);

  const pid_t pid = fork();
  if (pid == 0) {
    setpgid(0, 0);
    const int fd = open(probeLog.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
      close(fd);
    }
    const std::string markerText = marker.string();
    const std::string original = detectNwjsVersionFromDll(game.path);
    setenv("MKXP_MVMZ_COMPAT_PROBE", "1", 1);
    setenv("MKXP_MVMZ_COMPAT_PROBE_FILE", markerText.c_str(), 1);
    if (!original.empty()) setenv("MKXP_MVMZ_ORIGINAL_NWJS", original.c_str(), 1);
    execl("/bin/bash", "bash", script.c_str(), root.c_str(), game.path.c_str(),
          game.webRoot.c_str(), engineLabel(game.engine).c_str(), version.c_str(),
          static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) {
    result.detail = "fork failed";
    return result;
  }
  setpgid(pid, pid);

  int childStatus = 0;
  bool exited = false;
  bool timedOut = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t waited = waitpid(pid, &childStatus, WNOHANG);
    if (waited == pid) {
      exited = true;
      break;
    }
    if (waited < 0) break;
    usleep(50000);
  }
  if (!exited) {
    timedOut = true;
    kill(-pid, SIGTERM);
    usleep(150000);
    if (waitpid(pid, &childStatus, WNOHANG) == 0) kill(-pid, SIGKILL);
    waitpid(pid, &childStatus, 0);
  }

  std::ifstream in(marker, std::ios::binary);
  if (in) {
    std::string line;
    std::getline(in, line);
    const std::size_t split = line.find('|');
    result.state = split == std::string::npos ? line : line.substr(0, split);
    result.detail = split == std::string::npos ? std::string{} : line.substr(split + 1);
    if (result.state.empty()) result.state = "FAIL";
  } else if (timedOut) {
    result.state = "TIMEOUT";
    result.detail = "probe process exceeded 10 seconds";
  } else if (WIFEXITED(childStatus)) {
    result.state = "FAIL";
    result.detail = "probe exited=" + std::to_string(WEXITSTATUS(childStatus));
  } else {
    result.state = "FAIL";
    result.detail = "probe terminated without marker";
  }
#else
  (void)root;
  (void)game;
#endif
  return result;
}

std::string scanNwjsRuntimeDeep(
    const fs::path& root, const GameInfo& game, std::string& diagnostic,
    const std::function<void(const std::string&, int, int)>& progress,
    std::vector<NwjsProbeResult>* outResults = nullptr) {
  const auto versions = installedNwjsVersions(root);
  if (versions.empty()) {
    diagnostic = "deep:no-nwjs-installed";
    return {};
  }

  std::vector<NwjsProbeResult> results;
  std::string bestPass;
  std::string bestPartial;
  for (std::size_t i = 0; i < versions.size(); ++i) {
    if (progress) progress(versions[i], static_cast<int>(i), static_cast<int>(versions.size()));
    NwjsProbeResult probe = runNwjsCompatibilityProbe(root, game, versions[i]);
    if (probe.state == "PASS" && bestPass.empty()) bestPass = probe.version;
    if (probe.state == "PARTIAL" && bestPartial.empty()) bestPartial = probe.version;
    results.push_back(std::move(probe));
  }

  std::string selected = !bestPass.empty() ? bestPass : bestPartial;
  std::string fallbackDiagnostic;
  if (selected.empty()) selected = chooseAutoNwjsRuntime(root, game, fallbackDiagnostic);
  if (!selected.empty()) writeNwjsCache(root, game, selected);

  std::ostringstream out;
  out << "deep[";
  for (std::size_t i = 0; i < results.size(); ++i) {
    if (i) out << ',';
    out << results[i].version << '=' << results[i].state;
  }
  out << "]->" << selected;
  if (!fallbackDiagnostic.empty()) out << "+fallback:" << fallbackDiagnostic;
  diagnostic = out.str();
  if (outResults) *outResults = results;
  return selected;
}

enum class EasyRpgChoiceKind {
  Soundfont,
  Font1,
  Font2,
};

fs::path easyRpgChoiceFile(const GameInfo& game, EasyRpgChoiceKind kind) {
  switch (kind) {
    case EasyRpgChoiceKind::Soundfont: return game.path / "mkxp-easyrpg-soundfont.txt";
    case EasyRpgChoiceKind::Font1: return game.path / "mkxp-easyrpg-font1.txt";
    case EasyRpgChoiceKind::Font2: return game.path / "mkxp-easyrpg-font2.txt";
  }
  return game.path / "mkxp-easyrpg.txt";
}

std::string readEasyRpgChoice(const GameInfo& game, EasyRpgChoiceKind kind) {
  std::ifstream in(easyRpgChoiceFile(game, kind), std::ios::binary);
  if (!in) return "auto";
  std::string value;
  std::getline(in, value);
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
  return value.empty() ? "auto" : value;
}

bool writeEasyRpgChoice(const GameInfo& game, EasyRpgChoiceKind kind, const std::string& value) {
  std::ofstream out(easyRpgChoiceFile(game, kind), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << (value.empty() ? "auto" : value) << '\n';
  return static_cast<bool>(out);
}

std::vector<std::string> easyRpgAssetChoices(const fs::path& root, EasyRpgChoiceKind kind) {
  const bool soundfont = kind == EasyRpgChoiceKind::Soundfont;
  const fs::path dir = root / "runtime/easyrpg" / (soundfont ? "Soundfont" : "Font");
  std::vector<std::string> result{"auto"};
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return result;
  for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    const bool valid = soundfont
      ? ext == ".sf2"
      : (ext == ".fon" || ext == ".fnt" || ext == ".bdf" || ext == ".ttf" ||
         ext == ".ttc" || ext == ".otf" || ext == ".woff" || ext == ".woff2");
    if (valid) result.push_back(entry.path().filename().string());
  }
  std::sort(result.begin() + 1, result.end());
  return result;
}

std::string easyRpgChoiceLabel(const GameInfo& game, EasyRpgChoiceKind kind) {
  const std::string value = readEasyRpgChoice(game, kind);
  if (value == "auto") {
    if (gUiLanguage == UiLanguage::Korean) return "EasyRPG 설정";
    if (gUiLanguage == UiLanguage::Japanese) return "EasyRPG 設定";
    return "EasyRPG Settings";
  }
  return value;
}

fs::path easyRpgEncodingFile(const GameInfo& game) {
  return game.path / "mkxp-easyrpg-encoding.txt";
}

std::string readEasyRpgEncodingChoice(const GameInfo& game) {
  std::ifstream in(easyRpgEncodingFile(game), std::ios::binary);
  if (!in) return "auto";
  std::string value;
  std::getline(in, value);
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
  if (value == "949" || value == "932" || value == "1252") return value;
  return "auto";
}

bool writeEasyRpgEncodingChoice(const GameInfo& game, const std::string& value) {
  std::ofstream out(easyRpgEncodingFile(game), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << ((value == "949" || value == "932" || value == "1252") ? value : "auto") << '\n';
  return static_cast<bool>(out);
}

std::string easyRpgEncodingLabel(const GameInfo& game) {
  const std::string value = readEasyRpgEncodingChoice(game);
  if (value == "949") return uiWord("한국어 949", "Korean 949", "韓国語 949");
  if (value == "932") return uiWord("일본어 932", "Japanese 932", "日本語 932");
  if (value == "1252") return uiWord("영어 1252", "English 1252", "英語 1252");
  return uiWord("자동 판별", "Auto detect", "自動判定");
}

struct ProtonTool {
  std::string id;
  std::string name;
  fs::path path;
  bool installed = false;
  bool experimental = false;
};

std::string lowerAsciiCopy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

void appendUniquePath(std::vector<fs::path>& paths, const fs::path& candidate) {
  if (candidate.empty()) return;
  std::error_code ec;
  fs::path normalized = fs::weakly_canonical(candidate, ec);
  if (ec) normalized = candidate.lexically_normal();
  const std::string key = normalized.generic_string();
  for (const auto& existing : paths) {
    if (existing.generic_string() == key) return;
  }
  paths.push_back(normalized);
}

std::vector<fs::path> steamRoots() {
  std::vector<fs::path> roots;
  if (const char* testRoot = std::getenv("MKXP_TEST_STEAM_ROOT")) appendUniquePath(roots, fs::path(testRoot));
  const char* homeEnv = std::getenv("HOME");
  if (!homeEnv || !*homeEnv) return roots;
  const fs::path home(homeEnv);
  appendUniquePath(roots, home / ".steam/root");
  appendUniquePath(roots, home / ".steam/steam");
  appendUniquePath(roots, home / ".local/share/Steam");
  return roots;
}

std::vector<fs::path> steamLibraries() {
  std::vector<fs::path> libraries;
  for (const auto& root : steamRoots()) {
    appendUniquePath(libraries, root);
    std::ifstream in(root / "steamapps/libraryfolders.vdf", std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
      const std::string lower = lowerAsciiCopy(line);
      if (lower.find("\"path\"") == std::string::npos) continue;
      const std::size_t keyEnd = line.find('"', line.find('"') + 1);
      if (keyEnd == std::string::npos) continue;
      const std::size_t valueStart = line.find('"', keyEnd + 1);
      if (valueStart == std::string::npos) continue;
      const std::size_t valueEnd = line.find('"', valueStart + 1);
      if (valueEnd == std::string::npos) continue;
      std::string value = line.substr(valueStart + 1, valueEnd - valueStart - 1);
      std::string decoded;
      decoded.reserve(value.size());
      for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '\\') {
          decoded.push_back('\\');
          ++i;
        } else {
          decoded.push_back(value[i]);
        }
      }
      appendUniquePath(libraries, fs::path(decoded));
    }
  }
  return libraries;
}

void appendProtonTool(std::vector<ProtonTool>& tools, const fs::path& dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec) || !fs::is_regular_file(dir / "proton", ec)) return;
  const std::string folder = dir.filename().string();
  const std::string lower = lowerAsciiCopy(folder);
  if (lower.find("proton") == std::string::npos) return;
  const bool experimental = lower.find("experimental") != std::string::npos;
  const std::string id = experimental ? "experimental" : "name:" + folder;
  for (auto& tool : tools) {
    if (tool.id == id) {
      if (!tool.installed) {
        tool.path = dir;
        tool.installed = true;
      }
      return;
    }
  }
  tools.push_back(ProtonTool{id, experimental ? "Proton Experimental" : folder, dir, true, experimental});
}

std::vector<ProtonTool> installedProtonTools() {
  static std::vector<ProtonTool> cached;
  static auto cachedAt = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (!cached.empty() && cachedAt.time_since_epoch().count() != 0 &&
      now - cachedAt < std::chrono::seconds(2)) {
    return cached;
  }
  std::vector<ProtonTool> tools;
  tools.push_back(ProtonTool{"experimental", "Proton Experimental", {}, false, true});
  for (const auto& library : steamLibraries()) {
    const fs::path common = library / "steamapps/common";
    std::error_code ec;
    if (!fs::is_directory(common, ec)) continue;
    for (const auto& entry : fs::directory_iterator(common, fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      if (entry.is_directory(ec)) appendProtonTool(tools, entry.path());
    }
  }
  for (const auto& root : steamRoots()) {
    const fs::path custom = root / "compatibilitytools.d";
    std::error_code ec;
    if (!fs::is_directory(custom, ec)) continue;
    for (const auto& entry : fs::directory_iterator(custom, fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      if (entry.is_directory(ec)) appendProtonTool(tools, entry.path());
    }
  }
  if (tools.size() > 1) {
    std::sort(tools.begin() + 1, tools.end(), [](const ProtonTool& a, const ProtonTool& b) {
      return lowerAsciiCopy(a.name) < lowerAsciiCopy(b.name);
    });
  }
  cached = tools;
  cachedAt = now;
  return cached;
}

fs::path wolfProtonChoiceFile(const GameInfo& game) {
  return game.path / "mkxp-wolf-proton.txt";
}

std::string readWolfProtonChoice(const GameInfo& game) {
  std::ifstream in(wolfProtonChoiceFile(game), std::ios::binary);
  if (!in) return "experimental";
  std::string value;
  std::getline(in, value);
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
  return value.empty() ? "experimental" : value;
}

bool writeWolfProtonChoice(const GameInfo& game, const std::string& value) {
  std::ofstream out(wolfProtonChoiceFile(game), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << (value.empty() ? "experimental" : value) << '\n';
  return static_cast<bool>(out);
}

std::optional<ProtonTool> selectedWolfProton(const GameInfo& game) {
  const std::string wanted = readWolfProtonChoice(game);
  const auto tools = installedProtonTools();
  for (const auto& tool : tools) if (tool.id == wanted) return tool;
  for (const auto& tool : tools) if (tool.experimental) return tool;
  return std::nullopt;
}

std::string wolfProtonLabel(const GameInfo& game) {
  const auto selected = selectedWolfProton(game);
  if (!selected) return "Proton Experimental";
  if (selected->experimental && !selected->installed) {
    if (gUiLanguage == UiLanguage::Korean) return "Proton Experimental (미설치)";
    if (gUiLanguage == UiLanguage::Japanese) return "Proton Experimental (未インストール)";
    return "Proton Experimental (not installed)";
  }
  return selected->name;
}

bool supportsProtonCompatibility(const GameInfo& game) {
  return isRgssEngine(game.engine) || isWebEngine(game.engine);
}

fs::path compatibilityModeFile(const GameInfo& game) {
  return game.path / "mkxp-compat-mode.txt";
}

std::string readCompatibilityMode(const GameInfo& game) {
  if (!supportsProtonCompatibility(game)) return "native";
  std::ifstream in(compatibilityModeFile(game), std::ios::binary);
  if (!in) return "native";
  std::string value;
  std::getline(in, value);
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
  return lowerAsciiCopy(value) == "proton" ? "proton" : "native";
}

bool writeCompatibilityMode(const GameInfo& game, const std::string& value) {
  if (!supportsProtonCompatibility(game)) return false;
  std::ofstream out(compatibilityModeFile(game), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << (lowerAsciiCopy(value) == "proton" ? "proton" : "native") << '\n';
  return static_cast<bool>(out);
}

bool protonCompatibilityEnabled(const GameInfo& game) {
  return readCompatibilityMode(game) == "proton";
}

std::string compatibilityModeLabel(const GameInfo& game) {
  if (protonCompatibilityEnabled(game)) return "Proton";
  if (isRgssEngine(game.engine)) return uiWord("기본 (mkxp-z)", "Default (mkxp-z)", "標準 (mkxp-z)");
  if (isWebEngine(game.engine)) return uiWord("기본 (NW.js)", "Default (NW.js)", "標準 (NW.js)");
  return uiWord("기본", "Default", "標準");
}

fs::path gameProtonChoiceFile(const GameInfo& game) {
  return game.path / "mkxp-proton.txt";
}

std::string readGameProtonChoice(const GameInfo& game) {
  std::ifstream in(gameProtonChoiceFile(game), std::ios::binary);
  if (!in) return "experimental";
  std::string value;
  std::getline(in, value);
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
  return value.empty() ? "experimental" : value;
}

bool writeGameProtonChoice(const GameInfo& game, const std::string& value) {
  std::ofstream out(gameProtonChoiceFile(game), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << (value.empty() ? "experimental" : value) << '\n';
  return static_cast<bool>(out);
}

std::optional<ProtonTool> selectedGameProton(const GameInfo& game) {
  const std::string wanted = readGameProtonChoice(game);
  const auto tools = installedProtonTools();
  for (const auto& tool : tools) if (tool.id == wanted) return tool;
  for (const auto& tool : tools) if (tool.experimental) return tool;
  return std::nullopt;
}

fs::path protonEnvPresetFile(const GameInfo& game) {
  return game.path / "mkxp-proton-env-preset.txt";
}

fs::path protonCustomEnvFile(const GameInfo& game) {
  return game.path / "mkxp-proton-env.txt";
}

void ensureProtonCustomEnvTemplate(const GameInfo& game) {
  std::error_code ec;
  const fs::path path = protonCustomEnvFile(game);
  if (fs::exists(path, ec)) return;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return;
  out << "# RPG Maker Player - per-game Proton environment variables\n"
         "# One variable per line: KEY=VALUE\n"
         "# Lines beginning with # or ; are ignored. Custom values override the selected preset.\n"
         "# Example: PROTON_USE_WINED3D=1\n";
}

const std::array<std::string, 5>& protonEnvPresetIds() {
  static const std::array<std::string, 5> ids{{
    "default", "wined3d", "nosync", "wined3d-nosync", "software-gl"
  }};
  return ids;
}

std::string readProtonEnvPreset(const GameInfo& game) {
  std::ifstream in(protonEnvPresetFile(game), std::ios::binary);
  if (!in) return "default";
  std::string value;
  std::getline(in, value);
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) value.pop_back();
  const auto& ids = protonEnvPresetIds();
  return std::find(ids.begin(), ids.end(), value) != ids.end() ? value : "default";
}

bool writeProtonEnvPreset(const GameInfo& game, const std::string& value) {
  const auto& ids = protonEnvPresetIds();
  const std::string safe = std::find(ids.begin(), ids.end(), value) != ids.end() ? value : "default";
  std::ofstream out(protonEnvPresetFile(game), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << safe << '\n';
  return static_cast<bool>(out);
}

std::string protonEnvPresetLabel(const GameInfo& game) {
  const std::string id = readProtonEnvPreset(game);
  if (id == "wined3d") return "WineD3D (OpenGL)";
  if (id == "nosync") return uiWord("ESYNC/FSYNC 끄기", "Disable ESYNC/FSYNC", "ESYNC/FSYNC 無効");
  if (id == "wined3d-nosync") return uiWord("WineD3D + 동기화 끄기", "WineD3D + no sync", "WineD3D + 同期無効");
  if (id == "software-gl") return uiWord("소프트웨어 OpenGL", "Software OpenGL", "Software OpenGL");
  return uiWord("기본", "Default", "標準");
}

std::vector<std::pair<std::string, std::string>> protonEnvPresetValues(const GameInfo& game) {
  const std::string id = readProtonEnvPreset(game);
  std::vector<std::pair<std::string, std::string>> vars;
  if (id == "wined3d" || id == "wined3d-nosync" || id == "software-gl")
    vars.push_back({"PROTON_USE_WINED3D", "1"});
  if (id == "nosync" || id == "wined3d-nosync") {
    vars.push_back({"PROTON_NO_ESYNC", "1"});
    vars.push_back({"PROTON_NO_FSYNC", "1"});
  }
  if (id == "software-gl") vars.push_back({"LIBGL_ALWAYS_SOFTWARE", "1"});
  return vars;
}

bool validEnvironmentName(const std::string& key) {
  if (key.empty()) return false;
  const unsigned char first = static_cast<unsigned char>(key.front());
  if (!(std::isalpha(first) || key.front() == '_')) return false;
  for (char ch : key) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (!(std::isalnum(c) || ch == '_')) return false;
  }
  return true;
}

std::string trimEnvironmentField(std::string value) {
  auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
  while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) value.pop_back();
  return value;
}

std::vector<std::pair<std::string, std::string>> readCustomProtonEnvironment(const GameInfo& game) {
  std::vector<std::pair<std::string, std::string>> vars;
  std::ifstream in(protonCustomEnvFile(game), std::ios::binary);
  if (!in) return vars;
  std::string line;
  while (vars.size() < 64 && std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    line = trimEnvironmentField(line);
    if (line.empty() || line.front() == '#' || line.front() == ';') continue;
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) continue;
    std::string key = trimEnvironmentField(line.substr(0, equals));
    std::string value = trimEnvironmentField(line.substr(equals + 1));
    if (!validEnvironmentName(key) || value.size() > 4096) continue;
    vars.push_back({std::move(key), std::move(value)});
  }
  return vars;
}

std::size_t customProtonEnvironmentCount(const GameInfo& game) {
  return readCustomProtonEnvironment(game).size();
}

void applyGameProtonEnvironment(const GameInfo& game, int logFd) {
#if defined(__linux__)
  const std::string preset = readProtonEnvPreset(game);
  if (logFd >= 0) dprintf(logFd, "proton env preset=%s\n", preset.c_str());
  for (const auto& item : protonEnvPresetValues(game)) {
    setenv(item.first.c_str(), item.second.c_str(), 1);
    if (logFd >= 0) dprintf(logFd, "proton env preset %s=%s\n", item.first.c_str(), item.second.c_str());
  }
  for (const auto& item : readCustomProtonEnvironment(game)) {
    setenv(item.first.c_str(), item.second.c_str(), 1);
    if (logFd >= 0) dprintf(logFd, "proton env custom %s=%s\n", item.first.c_str(), item.second.c_str());
  }
#else
  (void)game; (void)logFd;
#endif
}

std::string gameProtonLabel(const GameInfo& game) {
  const auto selected = selectedGameProton(game);
  if (!selected) return "Proton Experimental";
  if (selected->experimental && !selected->installed) {
    if (gUiLanguage == UiLanguage::Korean) return "Proton Experimental (미설치)";
    if (gUiLanguage == UiLanguage::Japanese) return "Proton Experimental (未インストール)";
    return "Proton Experimental (not installed)";
  }
  return selected->name;
}

std::optional<fs::path> findCompatibilityWindowsExecutable(const GameInfo& game) {
  if (!supportsProtonCompatibility(game)) return std::nullopt;
  std::error_code ec;
  if (!fs::is_directory(game.path, ec)) return std::nullopt;

  auto findNamed = [&](const std::string& wanted) -> std::optional<fs::path> {
    const std::string target = lowerAsciiCopy(wanted);
    std::error_code iterEc;
    for (const auto& entry : fs::directory_iterator(game.path, fs::directory_options::skip_permission_denied, iterEc)) {
      if (iterEc) break;
      if (!entry.is_regular_file(iterEc)) continue;
      if (lowerAsciiCopy(entry.path().filename().string()) == target) return entry.path();
    }
    return std::nullopt;
  };

  if (const auto gameExe = findNamed("Game.exe")) return gameExe;
  if (isWebEngine(game.engine)) {
    if (const auto nwExe = findNamed("nw.exe")) return nwExe;
  }

  std::vector<fs::path> candidates;
  for (const auto& entry : fs::directory_iterator(game.path, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    if (lowerAsciiCopy(entry.path().extension().string()) != ".exe") continue;
    const std::string lower = lowerAsciiCopy(entry.path().filename().string());
    if (lower == "config.exe" || lower == "setup.exe" || lower == "crashpad_handler.exe" ||
        lower.rfind("unins", 0) == 0 || lower.find("uninstall") != std::string::npos) continue;
    candidates.push_back(entry.path());
  }
  if (candidates.empty()) return std::nullopt;

  const std::string folderExe = lowerAsciiCopy(game.folderName) + ".exe";
  std::stable_sort(candidates.begin(), candidates.end(), [&](const fs::path& a, const fs::path& b) {
    auto score = [&](const fs::path& p) {
      const std::string name = lowerAsciiCopy(p.filename().string());
      int value = 0;
      if (name == folderExe) value += 100;
      if (name.find("game") != std::string::npos) value += 50;
      if (name.find("launcher") != std::string::npos) value += 10;
      return value;
    };
    return score(a) > score(b);
  });
  return candidates.front();
}

std::optional<fs::path> findWolfExecutable(const GameInfo& game, bool config) {
  const std::vector<std::string> names = config
    ? std::vector<std::string>{"Config.exe"}
    : std::vector<std::string>{"GamePro.exe", "Game.exe"};
  std::error_code ec;
  if (!fs::is_directory(game.path, ec)) return std::nullopt;
  for (const auto& wanted : names) {
    const std::string target = lowerAsciiCopy(wanted);
    for (const auto& entry : fs::directory_iterator(game.path, fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      if (!entry.is_regular_file(ec)) continue;
      if (lowerAsciiCopy(entry.path().filename().string()) == target) return entry.path();
    }
  }
  return std::nullopt;
}
std::optional<fs::path> findFileCaseInsensitiveInDir(const fs::path& dir, const std::string& wanted);

bool forceWolfWindowMode(const GameInfo& game) {
  fs::path iniPath = game.path / "Game.ini";
  if (const auto existing = findFileCaseInsensitiveInDir(game.path, "Game.ini")) iniPath = *existing;

  std::vector<std::string> lines;
  {
    std::ifstream in(iniPath, std::ios::binary);
    std::string line;
    while (in && std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      lines.push_back(line);
    }
  }

  bool defaultFound = false;
  bool windowWritten = false;
  for (auto& line : lines) {
    std::string trimmed = line;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
    if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
      if (lowerAsciiCopy(trimmed.substr(1, trimmed.size() - 2)) == "default") defaultFound = true;
      continue;
    }
    if (lowerAsciiCopy(trimmed).rfind("windowmodeflag=", 0) == 0) {
      line = "WindowModeFlag=1";
      windowWritten = true;
    }
  }

  if (!windowWritten && defaultFound) {
    for (auto it = lines.begin(); it != lines.end(); ++it) {
      std::string trimmed = *it;
      while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
      while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
      if (lowerAsciiCopy(trimmed) == "[default]") {
        lines.insert(std::next(it), "WindowModeFlag=1");
        windowWritten = true;
        break;
      }
    }
  }

  if (!windowWritten) {
    if (!lines.empty() && !lines.back().empty()) lines.push_back("");
    lines.push_back("[Default]");
    lines.push_back("WindowModeFlag=1");
  }

  std::ofstream out(iniPath, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  for (const auto& line : lines) out << line << "\r\n";
  return static_cast<bool>(out);
}

bool isPortableFontFile(const fs::path& path) {
  std::string ext = lowerAsciiCopy(path.extension().string());
  return ext == ".ttf" || ext == ".ttc" || ext == ".otf" ||
         ext == ".fon" || ext == ".fnt" || ext == ".bdf" ||
         ext == ".woff" || ext == ".woff2";
}

bool isWindowsFontFile(const fs::path& path) {
  std::string ext = lowerAsciiCopy(path.extension().string());
  return ext == ".ttf" || ext == ".ttc" || ext == ".otf" ||
         ext == ".fon" || ext == ".fnt";
}

std::string xmlEscape(std::string value) {
  auto replaceAll = [&](const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
      value.replace(pos, from.size(), to);
      pos += to.size();
    }
  };
  replaceAll("&", "&amp;");
  replaceAll("<", "&lt;");
  replaceAll(">", "&gt;");
  replaceAll("\"", "&quot;");
  replaceAll("'", "&apos;");
  return value;
}

std::vector<fs::path> localGameFontDirs(const GameInfo& game) {
  std::vector<fs::path> dirs;
  std::error_code ec;
  auto addDir = [&](const fs::path& candidate) {
    if (!fs::is_directory(candidate, ec)) return;
    for (const auto& existing : dirs) {
      if (existing.lexically_normal() == candidate.lexically_normal()) return;
    }
    dirs.push_back(candidate);
  };

  const std::array<const char*, 4> names{{"Fonts", "fonts", "Font", "font"}};
  for (const char* name : names) addDir(game.path / name);
  for (const char* name : names) addDir(game.path / "Data" / name);
  for (const char* name : names) addDir(game.path / "data" / name);

  bool rootHasFont = false;
  if (fs::is_directory(game.path, ec)) {
    for (const auto& entry : fs::directory_iterator(game.path, fs::directory_options::skip_permission_denied, ec)) {
      if (ec) break;
      if (entry.is_regular_file(ec) && isPortableFontFile(entry.path())) {
        rootHasFont = true;
        break;
      }
    }
  }
  if (rootHasFont) addDir(game.path);
  return dirs;
}

fs::path writePortableFontconfig(const fs::path& launcherRoot, const GameInfo& game,
                                 const std::string& profile) {
  const fs::path shared = launcherRoot / "assets/fonts";
  std::error_code ec;
  const auto gameDirs = localGameFontDirs(game);
  if (gameDirs.empty() && !fs::is_directory(shared, ec)) return {};

  const std::string key = nwjsCacheKey(game);
  const fs::path work = launcherRoot / "cache/fontconfig" / profile / key;
  const fs::path cache = work / "cache";
  const fs::path config = work / "fonts.conf";
  fs::create_directories(cache, ec);
  std::ofstream out(config, std::ios::binary | std::ios::trunc);
  if (!out) return {};
  out << "<?xml version=\"1.0\"?>\n";
  out << "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n";
  out << "<fontconfig>\n";
  for (const auto& dir : gameDirs) {
    out << "  <dir>" << xmlEscape(fs::absolute(dir).string()) << "</dir>\n";
  }
  if (fs::is_directory(shared, ec)) {
    out << "  <dir>" << xmlEscape(fs::absolute(shared).string()) << "</dir>\n";
  }
  out << "  <include ignore_missing=\"yes\">/etc/fonts/fonts.conf</include>\n";
  out << "  <cachedir>" << xmlEscape(fs::absolute(cache).string()) << "</cachedir>\n";
  out << "</fontconfig>\n";
  out.close();
  return config;
}

std::vector<fs::path> fontFilesInDirectory(const fs::path& dir, bool windowsOnly = false) {
  std::vector<fs::path> files;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return files;
  for (const auto& entry : fs::recursive_directory_iterator(
         dir, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    if (windowsOnly ? isWindowsFontFile(entry.path()) : isPortableFontFile(entry.path())) {
      files.push_back(entry.path());
    }
  }
  return files;
}

bool hardlinkOrCopyFont(const fs::path& source, const fs::path& destination, bool replace) {
  std::error_code ec;
  fs::create_directories(destination.parent_path(), ec);
  if (fs::exists(destination, ec)) {
    if (!replace) return true;
    fs::remove(destination, ec);
  }
  fs::create_hard_link(source, destination, ec);
  if (!ec) return true;
  ec.clear();
  fs::create_symlink(source, destination, ec);
  if (!ec) return true;
  ec.clear();
  fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
  return !ec;
}

void syncSharedFontsIntoDirectory(const fs::path& launcherRoot, const fs::path& destination) {
  const fs::path shared = launcherRoot / "assets/fonts";
  for (const auto& font : fontFilesInDirectory(shared)) {
    hardlinkOrCopyFont(font, destination / font.filename(), false);
  }
}
std::string runtimeUiLabel(const GameInfo& game) {
  if (isEasyRpgEngine(game.engine)) return "EasyRPG 0.8.1.1";
  if (isWolfRpgEngine(game.engine)) return wolfProtonLabel(game);
  if (supportsProtonCompatibility(game) && protonCompatibilityEnabled(game))
    return std::string("Proton ") + gameProtonLabel(game);
  if (isWebEngine(game.engine)) {
    if (game.nwjsRuntime.empty()) return tr(UiKey::NwjsMissingShort);
    return (nwjsIsManual(game) ? "NWJS " : "NWJS " + std::string(tr(UiKey::AutoPrefix))) + game.nwjsRuntime;
  }
  return rubyUiLabel(game);
}

fs::path easyRpgProjectPath(const GameInfo& game) {
  return game.easyRpgProjectPath.empty() ? game.path : game.easyRpgProjectPath;
}

struct EasyRpgCompatProject {
  fs::path path;
  std::size_t aliasCount = 0;
  bool mirrored = false;
};

#if defined(__linux__)
std::optional<std::string> convertEncoding(const std::string& input,
                                           const char* fromEncoding,
                                           const char* toEncoding) {
  iconv_t cd = iconv_open(toEncoding, fromEncoding);
  if (cd == reinterpret_cast<iconv_t>(-1)) return std::nullopt;
  std::vector<char> output(std::max<std::size_t>(64, input.size() * 6 + 32));
  char* inPtr = const_cast<char*>(input.data());
  std::size_t inLeft = input.size();
  char* outPtr = output.data();
  std::size_t outLeft = output.size();
  const std::size_t result = iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft);
  iconv_close(cd);
  if (result == static_cast<std::size_t>(-1) || inLeft != 0) return std::nullopt;
  return std::string(output.data(), output.size() - outLeft);
}

std::optional<std::string> easyRpgCp932NameAsCp949(const std::string& utf8Name) {
  const auto cp932 = convertEncoding(utf8Name, "UTF-8", "CP932");
  if (!cp932 || cp932->empty()) return std::nullopt;
  const auto alias = convertEncoding(*cp932, "CP949", "UTF-8");
  if (!alias || alias->empty() || *alias == utf8Name) return std::nullopt;
  if (alias->find('/') != std::string::npos || alias->find('\\') != std::string::npos) return std::nullopt;
  return alias;
}

EasyRpgCompatProject prepareEasyRpgCompatProject(const fs::path& launcherRoot,
                                                  const GameInfo& game) {
  EasyRpgCompatProject result{easyRpgProjectPath(game), 0, false};
  std::error_code ec;
  if (!fs::is_directory(result.path, ec)) return result;

  const fs::path source = fs::absolute(result.path, ec);
  if (ec) return result;
  const fs::path mirror = launcherRoot / "cache/easyrpg" / nwjsCacheKey(game) / "compat_project";
  fs::remove_all(mirror, ec);
  ec.clear();
  fs::create_directories(mirror, ec);
  if (ec) return result;

  for (const auto& entry : fs::recursive_directory_iterator(
         source, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    const fs::path relative = fs::relative(entry.path(), source, ec);
    if (ec) { ec.clear(); continue; }
    const fs::path destination = mirror / relative;
    if (entry.is_directory(ec)) {
      fs::create_directories(destination, ec);
      ec.clear();
      continue;
    }
    if (!entry.is_regular_file(ec)) { ec.clear(); continue; }
    fs::create_directories(destination.parent_path(), ec);
    ec.clear();
    fs::create_symlink(entry.path(), destination, ec);
    if (ec) {
      ec.clear();
      fs::create_hard_link(entry.path(), destination, ec);
    }
    if (ec) {
      ec.clear();
      fs::copy_file(entry.path(), destination, fs::copy_options::overwrite_existing, ec);
    }
    ec.clear();

    const auto aliasName = easyRpgCp932NameAsCp949(entry.path().filename().string());
    if (!aliasName) continue;
    const fs::path aliasPath = destination.parent_path() / *aliasName;
    if (fs::exists(aliasPath, ec)) { ec.clear(); continue; }
    fs::create_symlink(entry.path(), aliasPath, ec);
    if (ec) {
      ec.clear();
      fs::create_hard_link(entry.path(), aliasPath, ec);
    }
    if (!ec) ++result.aliasCount;
    ec.clear();
  }

  if (result.aliasCount == 0) {
    fs::remove_all(mirror, ec);
    return result;
  }
  result.path = mirror;
  result.mirrored = true;
  return result;
}
#endif


std::optional<fs::path> findFileCaseInsensitiveInDir(const fs::path& dir, const std::string& wanted) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return std::nullopt;
  const std::string target = lowerAsciiCopy(wanted);
  for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    if (lowerAsciiCopy(entry.path().filename().string()) == target) return entry.path();
  }
  return std::nullopt;
}

bool syncEasyRpgIniEncoding(const GameInfo& game, const std::string& encoding) {
  const fs::path project = easyRpgProjectPath(game);
  std::error_code ec;
  if (!fs::is_directory(project, ec)) return true;

  fs::path iniPath = game.path / "RPG_RT.ini";
  if (const auto existing = findFileCaseInsensitiveInDir(game.path, "RPG_RT.ini")) iniPath = *existing;

  std::vector<std::string> lines;
  {
    std::ifstream in(iniPath, std::ios::binary);
    std::string line;
    while (in && std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      lines.push_back(line);
    }
  }

  const std::string value = "Encoding=" + (encoding.empty() ? std::string("auto") : encoding);
  bool inEasy = false;
  bool sectionFound = false;
  bool encodingWritten = false;
  for (auto& line : lines) {
    std::string trimmed = line;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
    if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
      const std::string section = lowerAsciiCopy(trimmed.substr(1, trimmed.size() - 2));
      inEasy = section == "easyrpg";
      if (inEasy) sectionFound = true;
      continue;
    }
    if (inEasy && lowerAsciiCopy(trimmed).rfind("encoding=", 0) == 0) {
      line = value;
      encodingWritten = true;
    }
  }

  if (!sectionFound) {
    if (!lines.empty() && !lines.back().empty()) lines.push_back("");
    lines.push_back("[EasyRPG]");
    lines.push_back(value);
    encodingWritten = true;
  } else if (!encodingWritten) {
    for (auto it = lines.begin(); it != lines.end(); ++it) {
      std::string trimmed = *it;
      while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
      while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
      if (lowerAsciiCopy(trimmed) == "[easyrpg]") {
        lines.insert(std::next(it), value);
        break;
      }
    }
  }

  std::ofstream out(iniPath, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  for (const auto& line : lines) out << line << "\r\n";
  return static_cast<bool>(out);
}

int launchEasyRpgGame(const fs::path& launcherRoot, const GameInfo& game, std::string& status,
                      SDL_Window* window, SDL_Renderer* renderer, const FontSet& fonts,
                      SDL_GameController* controller) {
#if defined(__linux__)
  const fs::path player = launcherRoot / "runtime/easyrpg/easyrpg-player";
  if (!fs::is_regular_file(player)) {
    status = uiWord("EasyRPG Player 실행 파일이 없습니다.", "EasyRPG Player is missing.", "EasyRPG Player が見つかりません。");
    return -1;
  }

  const fs::path exitRequestFile = launcherRoot / "cache/game_exit_request.flag";
  std::error_code ec;
  fs::create_directories(exitRequestFile.parent_path(), ec);
  {
    std::ofstream clear(exitRequestFile, std::ios::binary | std::ios::trunc);
  }

  const std::string key = nwjsCacheKey(game);
  const fs::path configDir = launcherRoot / "cache/easyrpg" / key / "config";
  fs::create_directories(configDir, ec);
  const fs::path soundfontDir = launcherRoot / "runtime/easyrpg/Soundfont";
  const fs::path userFontDir = launcherRoot / "runtime/easyrpg/Font";
  const fs::path fontDir = launcherRoot / "cache/easyrpg/shared_fonts";
  fs::create_directories(soundfontDir, ec);
  fs::create_directories(userFontDir, ec);
  fs::remove_all(fontDir, ec);
  fs::create_directories(fontDir, ec);
  for (const auto& font : fontFilesInDirectory(userFontDir)) {
    hardlinkOrCopyFont(font, fontDir / font.filename(), true);
  }
  syncSharedFontsIntoDirectory(launcherRoot, fontDir);

  const std::string soundfont = readEasyRpgChoice(game, EasyRpgChoiceKind::Soundfont);
  const std::string font1 = readEasyRpgChoice(game, EasyRpgChoiceKind::Font1);
  const std::string font2 = readEasyRpgChoice(game, EasyRpgChoiceKind::Font2);
  const std::string easyEncoding = readEasyRpgEncodingChoice(game);

  if (!syncEasyRpgIniEncoding(game, easyEncoding)) {
    status = uiWord("EasyRPG RPG_RT.ini 인코딩 저장 실패",
                    "Failed to update EasyRPG RPG_RT.ini encoding",
                    "EasyRPG RPG_RT.ini のエンコーディング保存に失敗しました");
    return -1;
  }
#if defined(__linux__)
  const EasyRpgCompatProject compatProject = prepareEasyRpgCompatProject(launcherRoot, game);
  const fs::path projectPath = compatProject.path;
#else
  const fs::path projectPath = easyRpgProjectPath(game);
#endif

  const pid_t pid = fork();
  if (pid == 0) {
    setpgid(0, 0);
    applyGameLocaleEnvironment(game);
    const fs::path gameLogPath = launcherRoot / "logs" /
        ("game_" + game.folderName + "_easyrpg.log");
    const int gameLogFd = open(gameLogPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (gameLogFd >= 0) {
      dup2(gameLogFd, STDOUT_FILENO);
      dup2(gameLogFd, STDERR_FILENO);
      dprintf(gameLogFd, "launcher game=%s engine=%s easyrpg=0.8.1.1 project=%s encoding=%s soundfont=%s font1=%s font2=%s compat_mirror=%d aliases=%zu\n",
              game.folderName.c_str(), engineLabel(game.engine).c_str(),
              projectPath.c_str(), easyEncoding.c_str(), soundfont.c_str(), font1.c_str(), font2.c_str(),
              compatProject.mirrored ? 1 : 0, compatProject.aliasCount);
      close(gameLogFd);
    }

    std::vector<std::string> args;
    args.push_back(player.string());
    args.push_back("--project-path");
    args.push_back(projectPath.string());
    if (compatProject.mirrored) {
      args.push_back("--save-path");
      args.push_back(game.path.string());
    }
    if (easyEncoding != "auto") {
      args.push_back("--encoding");
      args.push_back(easyEncoding);
    }
    args.push_back("--config-path");
    args.push_back(configDir.string());
    args.push_back("--soundfont-path");
    args.push_back(soundfontDir.string());
    args.push_back("--font-path");
    args.push_back(fontDir.string());
    args.push_back("--log-file");
    args.push_back((launcherRoot / "logs" / ("easyrpg_" + game.folderName + ".log")).string());

    if (soundfont != "auto" && fs::is_regular_file(soundfontDir / soundfont)) {
      args.push_back("--soundfont");
      args.push_back((soundfontDir / soundfont).string());
    }
    if (font1 != "auto" && fs::is_regular_file(fontDir / font1)) {
      args.push_back("--font1");
      args.push_back((fontDir / font1).string());
    }
    if (font2 != "auto" && fs::is_regular_file(fontDir / font2)) {
      args.push_back("--font2");
      args.push_back((fontDir / font2).string());
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    execv(player.c_str(), argv.data());
    _exit(127);
  }
  if (pid < 0) {
    status = uiWord("EasyRPG 실행 실패: fork 오류", "EasyRPG launch failed: fork error", "EasyRPG 起動失敗: fork エラー");
    return -1;
  }

  const int childStatus = waitForGameChild(pid, window, renderer, fonts, controller, exitRequestFile);
  if (WIFEXITED(childStatus)) {
    const int code = WEXITSTATUS(childStatus);
    status = code == 0 ? tr(UiKey::GameExit0)
                       : uiWord("EasyRPG 실행 실패. exit=", "EasyRPG failed. exit=", "EasyRPG 起動失敗. exit=")
                         + std::to_string(code) + " (logs/game_*_easyrpg.log)";
    return code;
  }
  if (WIFSIGNALED(childStatus)) {
    status = uiWord("EasyRPG 비정상 종료. signal=", "EasyRPG signal: ", "EasyRPG 異常終了. signal=") + std::to_string(WTERMSIG(childStatus));
  } else {
    status = uiWord("EasyRPG 게임이 비정상 종료되었습니다.", "EasyRPG exited abnormally.", "EasyRPG が異常終了しました。");
  }
  return -1;
#else
  (void)launcherRoot; (void)game; (void)window; (void)renderer; (void)fonts; (void)controller;
  status = tr(UiKey::LinuxOnly);
  return -1;
#endif
}

int launchRgssGame(const fs::path& launcherRoot, const GameInfo& game, std::string& status,
                   SDL_Window* window, SDL_Renderer* renderer, const FontSet& fonts,
                   SDL_GameController* controller) {
  const auto runtime = resolveMkxpBinary(launcherRoot, game.rubyRuntime);
  if (!runtime) {
    status = std::string(tr(UiKey::SelectedRubyMissing)) + game.rubyRuntime;
    return -1;
  }
#if defined(__linux__)
  const fs::path exitRequestFile = launcherRoot / "cache/game_exit_request.flag";
  std::error_code exitDirEc;
  fs::create_directories(exitRequestFile.parent_path(), exitDirEc);
  {
    std::ofstream clear(exitRequestFile, std::ios::binary | std::ios::trunc);
  }
  const fs::path portableFontConfig = writePortableFontconfig(launcherRoot, game, "rgss");

  const pid_t pid = fork();
  if (pid == 0) {
    setpgid(0, 0);
    applyGameLocaleEnvironment(game);
    const fs::path gameLogPath = launcherRoot / "logs" /
        ("game_" + game.folderName + "_" + game.rubyRuntime + ".log");
    const int gameLogFd = open(gameLogPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (gameLogFd >= 0) {
      dup2(gameLogFd, STDOUT_FILENO);
      dup2(gameLogFd, STDERR_FILENO);
      dprintf(gameLogFd, "launcher game=%s ruby=%s engine=%s\n",
              game.folderName.c_str(), game.rubyRuntime.c_str(), engineLabel(game.engine).c_str());
      close(gameLogFd);
    }

    const fs::path runtimeRoot = runtime->parent_path();
    const fs::path rubyRoot = runtimeRoot / "ruby";
    std::string ld;
    if (game.rubyRuntime == "ruby19" && fs::is_directory(runtimeRoot / "nothreaded_ruby")) {
      const fs::path safeRuby = runtimeRoot / "nothreaded_ruby";
      ld = runtimeRoot.string() + ":" +
           (safeRuby / "lib").string() + ":" +
           (safeRuby / "openssl/lib").string() + ":" +
           (rubyRoot / "lib").string();
    } else {
      ld = runtimeRoot.string() + ":" +
           (rubyRoot / "lib").string() + ":" +
           (rubyRoot / "openssl/lib").string();
    }
    if (const char* oldLd = std::getenv("LD_LIBRARY_PATH")) {
      if (*oldLd) ld += ":" + std::string(oldLd);
    }
    setenv("LD_LIBRARY_PATH", ld.c_str(), 1);

    const std::string loadPath = portableRubyLoadPath(runtimeRoot, game.rubyRuntime);
    if (!loadPath.empty()) setenv("MKXP_PORTABLE_RUBY_LOADPATH", loadPath.c_str(), 1);

    const fs::path preloadDir = launcherRoot / "preload/common";
    std::error_code preloadEc;
    if (fs::is_directory(preloadDir, preloadEc)) {
      const std::string preloadPath = fs::absolute(preloadDir).string();
      setenv("MKXP_PORTABLE_PRELOAD_DIR", preloadPath.c_str(), 1);
    } else {
      unsetenv("MKXP_PORTABLE_PRELOAD_DIR");
    }

    const fs::path portableSoundFont = launcherRoot / "assets/soundfonts/TimGM6mb.sf2";
    if (fs::is_regular_file(portableSoundFont)) {
      const std::string soundFontPath = fs::absolute(portableSoundFont).string();
      setenv("MKXP_PORTABLE_SOUNDFONT", soundFontPath.c_str(), 1);
    } else {
      unsetenv("MKXP_PORTABLE_SOUNDFONT");
    }

    if (!portableFontConfig.empty()) {
      const std::string fontConfigPath = fs::absolute(portableFontConfig).string();
      const std::string fontConfigDir = fs::absolute(portableFontConfig.parent_path()).string();
      const std::string globalFontDir = fs::absolute(launcherRoot / "assets/fonts").string();
      setenv("FONTCONFIG_FILE", fontConfigPath.c_str(), 1);
      setenv("FONTCONFIG_PATH", fontConfigDir.c_str(), 1);
      setenv("MKXP_GLOBAL_FONT_DIR", globalFontDir.c_str(), 1);
    }

    if (game.rubyRuntime == "ruby19") {
      setenv("SDL_VIDEODRIVER", "x11", 1);
      // Ruby 1.9.3's old GC can fault very early when embedded on modern Linux
      // if the initial heap is too small. Keep a generous initial heap so the
      // RGSS class bootstrap completes before normal GC pressure begins.
      setenv("RUBY_HEAP_MIN_SLOTS", "1000000", 0);
      setenv("RUBY_GC_MALLOC_LIMIT", "1000000000", 0);
      setenv("RUBY_FREE_MIN", "100000", 0);

      // Ruby 1.9.3 creates its VM timer thread with a very small legacy stack
      // attribute. Some modern kernels/libpthread combinations reject that
      // pthread_create() call with EINVAL. The compatibility shim retries only
      // that EINVAL case with default pthread attributes.
      const fs::path pthreadCompat = launcherRoot / "runtime/ruby19/libpthread_retry_einval.so";
      if (fs::is_regular_file(pthreadCompat)) {
        fs::path preloadCompat = pthreadCompat;
        std::error_code preloadEc;
        const fs::path preloadAliasDir = fs::temp_directory_path(preloadEc) /
            ("mkxp-ruby19-" + std::to_string(static_cast<unsigned long long>(getuid())) + "-" +
             std::to_string(static_cast<unsigned long long>(std::hash<std::string>{}(launcherRoot.string()))));
        if (!preloadEc) {
          fs::create_directories(preloadAliasDir, preloadEc);
          if (!preloadEc) {
            const fs::path preloadAlias = preloadAliasDir / "libpthread_retry_einval.so";
            fs::remove(preloadAlias, preloadEc);
            preloadEc.clear();
            fs::create_symlink(pthreadCompat, preloadAlias, preloadEc);
            if (preloadEc) {
              preloadEc.clear();
              fs::copy_file(pthreadCompat, preloadAlias,
                            fs::copy_options::overwrite_existing, preloadEc);
            }
            if (!preloadEc && fs::is_regular_file(preloadAlias)) preloadCompat = preloadAlias;
          }
        }

        const std::string preloadCompatText = preloadCompat.string();
        if (preloadCompatText.find_first_of(" \t\r\n") == std::string::npos) {
          std::string preload = preloadCompatText;
          if (const char* oldPreload = std::getenv("LD_PRELOAD")) {
            if (*oldPreload) preload += ":" + std::string(oldPreload);
          }
          setenv("LD_PRELOAD", preload.c_str(), 1);
          dprintf(STDERR_FILENO, "ruby19 pthread preload=%s\n", preloadCompatText.c_str());
        } else {
          dprintf(STDERR_FILENO,
                  "ruby19 pthread preload skipped: no whitespace-safe alias for %s\n",
                  pthreadCompat.c_str());
        }
      }
    }

    if (chdir(game.path.c_str()) != 0) _exit(126);
    setenv("MKXP_LAUNCHER_ENGINE", engineLabel(game.engine).c_str(), 1);
    setenv("MKXP_LAUNCHER_RUBY", game.rubyRuntime.c_str(), 1);
    setenv("MKXP_EXIT_REQUEST_FILE", exitRequestFile.c_str(), 1);
    const fs::path remapFile = controllerRemapFile(game);
    if (fs::is_regular_file(remapFile))
      setenv("MKXP_CONTROLLER_REMAP_FILE", remapFile.c_str(), 1);
    else
      unsetenv("MKXP_CONTROLLER_REMAP_FILE");
    execl(runtime->c_str(), runtime->c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) {
    status = tr(UiKey::MkxpForkFailed);
    return -1;
  }
  const int childStatus = waitForGameChild(pid, window, renderer, fonts, controller, exitRequestFile, true);
  if (WIFEXITED(childStatus)) {
    const int code = WEXITSTATUS(childStatus);
    if (code == 0)
      status = tr(UiKey::GameExit0);
    else
      status = std::string(tr(UiKey::RunFailed)) + std::to_string(code) + " (logs/game_*)";
    return code;
  }
  if (WIFSIGNALED(childStatus)) {
    status = std::string(tr(UiKey::GameSignal)) + std::to_string(WTERMSIG(childStatus)) + " (logs/game_*)";
  } else {
    status = tr(UiKey::GameAbnormal);
  }
  return -1;
#else
  (void)game;
  status = tr(UiKey::LinuxOnly);
  return -1;
#endif
}

int launchWebGame(const fs::path& launcherRoot, const GameInfo& game, std::string& status,
                  SDL_Window* window, SDL_Renderer* renderer, const FontSet& fonts,
                  SDL_GameController* controller) {
#if defined(__linux__)
  if (game.nwjsRuntime.empty()) {
    status = tr(UiKey::NwjsNotInstalled);
    return -1;
  }
  const fs::path script = launcherRoot / "runtime/mvmz/launch_mvmz.sh";
  const fs::path nw = launcherRoot / "runtime/nwjs" / game.nwjsRuntime / "nw";
  if (!fs::is_regular_file(script) || !fs::is_regular_file(nw)) {
    status = std::string(tr(UiKey::MvmzRuntimeMissing)) + game.nwjsRuntime;
    return -1;
  }
  if (game.webRoot.empty() || !fs::is_directory(game.webRoot)) {
    status = tr(UiKey::MvmzWebRootMissing);
    return -1;
  }

  const fs::path exitRequestFile = launcherRoot / "cache/game_exit_request.flag";
  std::error_code exitDirEc;
  fs::create_directories(exitRequestFile.parent_path(), exitDirEc);
  {
    std::ofstream clear(exitRequestFile, std::ios::binary | std::ios::trunc);
  }

  const pid_t pid = fork();
  if (pid == 0) {
    setpgid(0, 0);
    applyGameLocaleEnvironment(game);
    const fs::path gameLogPath = launcherRoot / "logs" /
        ("game_" + game.folderName + "_nwjs_" + game.nwjsRuntime + ".log");
    const int gameLogFd = open(gameLogPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (gameLogFd >= 0) {
      dup2(gameLogFd, STDOUT_FILENO);
      dup2(gameLogFd, STDERR_FILENO);
      dprintf(gameLogFd, "launcher game=%s nwjs=%s engine=%s webroot=%s\n",
              game.folderName.c_str(), game.nwjsRuntime.c_str(), engineLabel(game.engine).c_str(),
              game.webRoot.c_str());
      close(gameLogFd);
    }
    setenv("MKXP_LAUNCHER_ENGINE", engineLabel(game.engine).c_str(), 1);
    setenv("MKXP_LAUNCHER_NWJS", game.nwjsRuntime.c_str(), 1);
    setenv("MKXP_EXIT_REQUEST_FILE", exitRequestFile.c_str(), 1);
    const std::string originalNwjs = detectNwjsVersionFromDll(game.path);
    if (!originalNwjs.empty())
      setenv("MKXP_MVMZ_ORIGINAL_NWJS", originalNwjs.c_str(), 1);
    else
      unsetenv("MKXP_MVMZ_ORIGINAL_NWJS");
    execl("/bin/bash", "bash", script.c_str(), launcherRoot.c_str(), game.path.c_str(),
          game.webRoot.c_str(), engineLabel(game.engine).c_str(), game.nwjsRuntime.c_str(),
          static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) {
    status = tr(UiKey::MvmzForkFailed);
    return -1;
  }
  const int childStatus = waitForGameChild(pid, window, renderer, fonts, controller, exitRequestFile);
  if (WIFEXITED(childStatus)) {
    const int code = WEXITSTATUS(childStatus);
    status = code == 0 ? tr(UiKey::GameExit0)
                       : std::string(tr(UiKey::MvmzRunFailed)) + std::to_string(code) + " (logs/game_*)";
    return code;
  }
  if (WIFSIGNALED(childStatus)) {
    status = std::string(tr(UiKey::MvmzSignal)) + std::to_string(WTERMSIG(childStatus)) + " (logs/game_*)";
  } else {
    status = tr(UiKey::MvmzAbnormal);
  }
  return -1;
#else
  (void)launcherRoot;
  (void)game;
  status = tr(UiKey::LinuxOnly);
  return -1;
#endif
}

fs::path primarySteamRoot() {
  const auto roots = steamRoots();
  std::error_code ec;
  for (const auto& root : roots) {
    if (fs::is_directory(root / "steamapps", ec)) return root;
  }
  return roots.empty() ? fs::path{} : roots.front();
}

struct UpdateHelperResult {
  bool ok = false;
  std::string status;
  std::string latest;
  std::string error;
};

UpdateHelperResult runUpdateHelper(const fs::path& root, const std::string& mode,
                                   const std::string& latest = {}) {
  UpdateHelperResult result;
#if defined(__linux__)
  const fs::path script = root / "runtime/update/update.sh";
  const fs::path out = root / "cache/update/launcher_update_status.txt";
  std::error_code ec;
  fs::create_directories(out.parent_path(), ec);
  if (!fs::is_regular_file(script)) {
    result.error = "helper_missing";
    return result;
  }
  fs::remove(out, ec);
  const pid_t pid = fork();
  if (pid == 0) {
    execl("/bin/bash", "bash", script.c_str(), mode.c_str(), root.c_str(), APP_VERSION,
          latest.c_str(), out.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) {
    result.error = "fork";
    return result;
  }
  int childStatus = 0;
  waitpid(pid, &childStatus, 0);
  std::ifstream in(out, std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    const auto pos = line.find('=');
    if (pos == std::string::npos) continue;
    const std::string key = line.substr(0, pos);
    const std::string value = line.substr(pos + 1);
    if (key == "STATUS") result.status = value;
    else if (key == "LATEST") result.latest = value;
    else if (key == "ERROR") result.error = value;
  }
  result.ok = !result.status.empty() && result.status != "error" &&
              WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0;
#else
  (void)root; (void)mode; (void)latest;
  result.error = "unsupported";
#endif
  return result;
}

bool requestProtonExperimentalInstall() {
#if defined(__linux__)
  const pid_t pid = fork();
  if (pid == 0) {
    execlp("steam", "steam", "steam://install/1493710", static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) return false;
  int childStatus = 0;
  waitpid(pid, &childStatus, 0);
  return WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0;
#else
  return false;
#endif
}

int runProtonUtility(const fs::path& protonScript, const fs::path& compatData,
                     const fs::path& gamePath, const fs::path& steamRoot,
                     const fs::path& fontConfig, const fs::path& logDir,
                     const std::vector<std::string>& utilityArgs) {
#if defined(__linux__)
  const pid_t pid = fork();
  if (pid == 0) {
    const int nullFd = open("/dev/null", O_WRONLY);
    if (nullFd >= 0) {
      dup2(nullFd, STDOUT_FILENO);
      dup2(nullFd, STDERR_FILENO);
      close(nullFd);
    }
    const std::string compatText = compatData.string();
    const std::string gameText = gamePath.string();
    const std::string steamText = steamRoot.string();
    const std::string logText = logDir.string();
    setenv("STEAM_COMPAT_DATA_PATH", compatText.c_str(), 1);
    setenv("STEAM_COMPAT_INSTALL_PATH", gameText.c_str(), 1);
    if (!steamText.empty()) setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamText.c_str(), 1);
    setenv("STEAM_COMPAT_APP_ID", "0", 1);
    setenv("SteamAppId", "0", 1);
    setenv("SteamGameId", "0", 1);
    setenv("PROTON_LOG_DIR", logText.c_str(), 1);
    if (!fontConfig.empty()) {
      const std::string configText = fs::absolute(fontConfig).string();
      const std::string configDir = fs::absolute(fontConfig.parent_path()).string();
      setenv("FONTCONFIG_FILE", configText.c_str(), 1);
      setenv("FONTCONFIG_PATH", configDir.c_str(), 1);
    }
    if (chdir(gamePath.c_str()) != 0) _exit(126);
    std::vector<std::string> args;
    args.push_back(protonScript.string());
    args.insert(args.end(), utilityArgs.begin(), utilityArgs.end());
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    execv(protonScript.c_str(), argv.data());
    _exit(127);
  }
  if (pid < 0) return -1;
  int childStatus = 0;
  waitpid(pid, &childStatus, 0);
  if (WIFEXITED(childStatus)) return WEXITSTATUS(childStatus);
  return -1;
#else
  (void)protonScript; (void)compatData; (void)gamePath; (void)steamRoot;
  (void)fontConfig; (void)logDir; (void)utilityArgs;
  return -1;
#endif
}

std::vector<fs::path> syncWolfPrefixFonts(const fs::path& launcherRoot, const GameInfo& game,
                                          const fs::path& compatData) {
  const fs::path windowsFonts = compatData / "pfx/drive_c/windows/Fonts";
  std::error_code ec;
  fs::create_directories(windowsFonts, ec);
  std::vector<fs::path> bitmapInstalled;

  for (const auto& dir : localGameFontDirs(game)) {
    for (const auto& font : fontFilesInDirectory(dir, true)) {
      const fs::path target = windowsFonts / ("mkxp_game_" + font.filename().string());
      if (hardlinkOrCopyFont(font, target, true)) {
        const std::string ext = lowerAsciiCopy(font.extension().string());
        if (ext == ".fon" || ext == ".fnt") bitmapInstalled.push_back(target);
      }
    }
  }

  const fs::path shared = launcherRoot / "assets/fonts";
  for (const auto& font : fontFilesInDirectory(shared, true)) {
    const fs::path target = windowsFonts / ("mkxp_global_" + font.filename().string());
    if (hardlinkOrCopyFont(font, target, true)) {
      const std::string ext = lowerAsciiCopy(font.extension().string());
      if (ext == ".fon" || ext == ".fnt") bitmapInstalled.push_back(target);
    }
  }
  return bitmapInstalled;
}

void registerWolfBitmapFonts(const fs::path& protonScript, const fs::path& compatData,
                             const GameInfo& game, const fs::path& steamRoot,
                             const fs::path& fontConfig, const fs::path& logDir,
                             const std::vector<fs::path>& fonts) {
  for (const auto& font : fonts) {
    const std::string valueName = "MKXP " + font.stem().string();
    const std::string windowsPath = "C:\\windows\\Fonts\\" + font.filename().string();
    runProtonUtility(protonScript, compatData, game.path, steamRoot, fontConfig, logDir,
      {"run", "reg.exe", "add", "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
       "/v", valueName, "/d", windowsPath, "/f"});
  }
}
#if defined(__linux__)
bool processEnvironmentContains(pid_t pid, const std::string& marker) {
  std::ifstream in(fs::path("/proc") / std::to_string(pid) / "environ", std::ios::binary);
  if (!in) return false;
  std::ostringstream data;
  data << in.rdbuf();
  const std::string blob = data.str();
  std::size_t pos = 0;
  while (pos < blob.size()) {
    const std::size_t end = blob.find('\0', pos);
    const std::size_t len = (end == std::string::npos ? blob.size() : end) - pos;
    if (blob.compare(pos, len, marker) == 0) return true;
    if (end == std::string::npos) break;
    pos = end + 1;
  }
  return false;
}

std::vector<pid_t> wolfCompatProcesses(const fs::path& compatData) {
  std::vector<pid_t> result;
  const std::string compat = fs::absolute(compatData).string();
  const std::string steamMarker = "STEAM_COMPAT_DATA_PATH=" + compat;
  const std::string wineMarker = "WINEPREFIX=" + (fs::path(compat) / "pfx").string();
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator("/proc", fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    const std::string name = entry.path().filename().string();
    if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
    const pid_t candidate = static_cast<pid_t>(std::strtol(name.c_str(), nullptr, 10));
    if (candidate <= 1 || candidate == getpid()) continue;
    if (processEnvironmentContains(candidate, steamMarker) || processEnvironmentContains(candidate, wineMarker)) {
      result.push_back(candidate);
    }
  }
  return result;
}

void terminateWolfCompatProcesses(const fs::path& compatData) {
  auto pids = wolfCompatProcesses(compatData);
  for (const pid_t candidate : pids) kill(candidate, SIGTERM);
  for (int attempt = 0; attempt < 20; ++attempt) {
    SDL_Delay(50);
    pids = wolfCompatProcesses(compatData);
    if (pids.empty()) return;
  }
  for (const pid_t candidate : pids) kill(candidate, SIGKILL);
  SDL_Delay(100);
  pids = wolfCompatProcesses(compatData);
  for (const pid_t candidate : pids) kill(candidate, SIGKILL);
}
#endif
int launchWolfExecutable(const fs::path& launcherRoot, const GameInfo& game, bool configMode,
                         std::string& status, SDL_Window* window, SDL_Renderer* renderer,
                         const FontSet& fonts, SDL_GameController* controller) {
#if defined(__linux__)
  const auto executable = findWolfExecutable(game, configMode);
  if (!executable) {
    status = configMode ? uiWord("WOLF Config.exe를 찾을 수 없습니다.", "WOLF Config.exe was not found.", "WOLF Config.exe が見つかりません。") : uiWord("WOLF Game.exe/GamePro.exe를 찾을 수 없습니다.", "WOLF Game.exe/GamePro.exe was not found.", "WOLF Game.exe/GamePro.exe が見つかりません。");
    return -1;
  }

  auto proton = selectedWolfProton(game);
  if (!proton || !proton->installed || !fs::is_regular_file(proton->path / "proton")) {
    if (!proton || proton->experimental) {
      const bool requested = requestProtonExperimentalInstall();
      if (gUiLanguage == UiLanguage::Korean)
        status = requested ? "Proton Experimental 다운로드를 Steam에 요청했습니다. 설치 완료 후 다시 실행하세요."
                           : "Proton Experimental 설치 요청에 실패했습니다. Steam 실행 상태를 확인하세요.";
      else if (gUiLanguage == UiLanguage::Japanese)
        status = requested ? "Steam に Proton Experimental のインストールを要求しました。完了後に再実行してください。"
                           : "Proton Experimental のインストール要求に失敗しました。";
      else
        status = requested ? "Requested Proton Experimental installation from Steam. Launch again after it finishes."
                           : "Failed to request Proton Experimental installation from Steam.";
      return -2;
    }
    status = uiWord("선택한 Proton이 설치되어 있지 않습니다.", "The selected Proton is not installed.", "選択した Proton がインストールされていません。");
    return -1;
  }

  const fs::path protonScript = proton->path / "proton";
  const fs::path steamRoot = primarySteamRoot();
  const std::string key = nwjsCacheKey(game);
  const fs::path compatData = launcherRoot / "cache/wolf" / key / "compatdata";
  const fs::path exitRequestFile = launcherRoot / "cache/game_exit_request.flag";
  std::error_code ec;
  fs::create_directories(compatData, ec);
  fs::create_directories(exitRequestFile.parent_path(), ec);
  {
    std::ofstream clear(exitRequestFile, std::ios::binary | std::ios::trunc);
  }

  if (!configMode && !forceWolfWindowMode(game)) {
    status = uiWord("WOLF 창모드 호환 설정(Game.ini) 저장 실패",
                    "Failed to apply WOLF windowed compatibility mode to Game.ini",
                    "WOLF ウィンドウ互換設定(Game.ini)の保存に失敗しました");
    return -1;
  }

  const fs::path portableFontConfig = writePortableFontconfig(launcherRoot, game, "wolf");
  const fs::path wolfLogDir = launcherRoot / "logs";
  bool prefixReady = fs::is_regular_file(compatData / "pfx/system.reg");
  if (!prefixReady) {
    runProtonUtility(protonScript, compatData, game.path, steamRoot, portableFontConfig, wolfLogDir,
      {"run", "reg.exe", "query", "HKCU\\Software\\Wine"});
    prefixReady = fs::is_regular_file(compatData / "pfx/system.reg");
  }
  if (prefixReady) {
    const auto bitmapFonts = syncWolfPrefixFonts(launcherRoot, game, compatData);
    registerWolfBitmapFonts(protonScript, compatData, game, steamRoot, portableFontConfig, wolfLogDir, bitmapFonts);
  }

  const pid_t pid = fork();
  if (pid == 0) {
    setpgid(0, 0);
    applyGameLocaleEnvironment(game);
    const std::string mode = configMode ? "config" : "game";
    const fs::path gameLogPath = launcherRoot / "logs" /
        ("game_" + game.folderName + "_wolf_" + mode + ".log");
    const int gameLogFd = open(gameLogPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (gameLogFd >= 0) {
      dup2(gameLogFd, STDOUT_FILENO);
      dup2(gameLogFd, STDERR_FILENO);
      dprintf(gameLogFd, "launcher game=%s engine=%s proton=%s executable=%s compatdata=%s keyboardBridge=Select+X\n",
              game.folderName.c_str(), engineLabel(game.engine).c_str(), proton->name.c_str(),
              executable->c_str(), compatData.c_str());
      close(gameLogFd);
    }

    const std::string steamRootText = steamRoot.string();
    const std::string compatDataText = compatData.string();
    const std::string installPath = game.path.string();
    const std::string logDir = (launcherRoot / "logs").string();
    setenv("STEAM_COMPAT_DATA_PATH", compatDataText.c_str(), 1);
    setenv("STEAM_COMPAT_INSTALL_PATH", installPath.c_str(), 1);
    if (!steamRootText.empty()) setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamRootText.c_str(), 1);
    setenv("STEAM_COMPAT_APP_ID", "0", 1);
    setenv("SteamAppId", "0", 1);
    setenv("SteamGameId", "0", 1);
    setenv("PROTON_LOG", "1", 1);
    setenv("PROTON_LOG_DIR", logDir.c_str(), 1);
    setenv("MKXP_EXIT_REQUEST_FILE", exitRequestFile.c_str(), 1);
    if (!portableFontConfig.empty()) {
      const std::string fontConfigPath = fs::absolute(portableFontConfig).string();
      const std::string fontConfigDir = fs::absolute(portableFontConfig.parent_path()).string();
      const std::string globalFontDir = fs::absolute(launcherRoot / "assets/fonts").string();
      setenv("FONTCONFIG_FILE", fontConfigPath.c_str(), 1);
      setenv("FONTCONFIG_PATH", fontConfigDir.c_str(), 1);
      setenv("MKXP_GLOBAL_FONT_DIR", globalFontDir.c_str(), 1);
    }

    if (chdir(game.path.c_str()) != 0) _exit(126);
    execl(protonScript.c_str(), protonScript.c_str(), "run", executable->c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) {
    status = uiWord("WOLF/Proton 실행 실패: fork 오류", "WOLF/Proton launch failed: fork error", "WOLF/Proton 起動失敗: fork エラー");
    return -1;
  }

  const int childStatus = waitForGameChild(pid, window, renderer, fonts, controller, exitRequestFile, true);
  terminateWolfCompatProcesses(compatData);
  if (WIFEXITED(childStatus)) {
    const int code = WEXITSTATUS(childStatus);
    status = code == 0 ? tr(UiKey::GameExit0)
                       : uiWord("WOLF/Proton 실행 실패. exit=", "WOLF/Proton failed. exit=", "WOLF/Proton 起動失敗. exit=")
                         + std::to_string(code) + " (logs/game_*_wolf_*.log)";
    return code;
  }
  if (WIFSIGNALED(childStatus)) {
    status = uiWord("WOLF/Proton 비정상 종료. signal=", "WOLF/Proton signal: ", "WOLF/Proton 異常終了. signal=") + std::to_string(WTERMSIG(childStatus));
  } else {
    status = uiWord("WOLF/Proton 게임이 비정상 종료되었습니다.", "WOLF/Proton exited abnormally.", "WOLF/Proton が異常終了しました。");
  }
  return -1;
#else
  (void)launcherRoot; (void)game; (void)configMode; (void)window; (void)renderer; (void)fonts; (void)controller;
  status = tr(UiKey::LinuxOnly);
  return -1;
#endif
}
int launchProtonCompatibilityGame(const fs::path& launcherRoot, const GameInfo& game,
                                  std::string& status, SDL_Window* window,
                                  SDL_Renderer* renderer, const FontSet& fonts,
                                  SDL_GameController* controller) {
#if defined(__linux__)
  const auto executable = findCompatibilityWindowsExecutable(game);
  if (!executable) {
    status = uiWord("Proton으로 실행할 Windows EXE를 찾을 수 없습니다.",
                    "No Windows executable was found for Proton mode.",
                    "Proton で実行する Windows EXE が見つかりません。");
    return -1;
  }

  auto proton = selectedGameProton(game);
  if (!proton || !proton->installed || !fs::is_regular_file(proton->path / "proton")) {
    if (!proton || proton->experimental) {
      const bool requested = requestProtonExperimentalInstall();
      status = requested
        ? uiWord("Proton Experimental 다운로드를 Steam에 요청했습니다. 설치 완료 후 다시 실행하세요.",
                 "Requested Proton Experimental installation from Steam. Launch again after it finishes.",
                 "Steam に Proton Experimental のインストールを要求しました。完了後に再実行してください。")
        : uiWord("Proton Experimental 설치 요청에 실패했습니다. Steam 실행 상태를 확인하세요.",
                 "Failed to request Proton Experimental installation from Steam.",
                 "Proton Experimental のインストール要求に失敗しました。");
      return -2;
    }
    status = uiWord("선택한 Proton이 설치되어 있지 않습니다.",
                    "The selected Proton is not installed.",
                    "選択した Proton がインストールされていません。");
    return -1;
  }

  const fs::path protonScript = proton->path / "proton";
  const fs::path steamRoot = primarySteamRoot();
  const std::string key = nwjsCacheKey(game);
  const fs::path compatData = launcherRoot / "cache/proton" / key / "compatdata";
  const fs::path exitRequestFile = launcherRoot / "cache/game_exit_request.flag";
  std::error_code ec;
  fs::create_directories(compatData, ec);
  fs::create_directories(exitRequestFile.parent_path(), ec);
  { std::ofstream clear(exitRequestFile, std::ios::binary | std::ios::trunc); }

  const fs::path portableFontConfig = writePortableFontconfig(launcherRoot, game, "proton");
  const fs::path protonLogDir = launcherRoot / "logs";
  bool prefixReady = fs::is_regular_file(compatData / "pfx/system.reg");
  if (!prefixReady) {
    runProtonUtility(protonScript, compatData, game.path, steamRoot,
                     portableFontConfig, protonLogDir,
                     {"run", "reg.exe", "query", "HKCU\\Software\\Wine"});
    prefixReady = fs::is_regular_file(compatData / "pfx/system.reg");
  }
  if (prefixReady) {
    const auto bitmapFonts = syncWolfPrefixFonts(launcherRoot, game, compatData);
    registerWolfBitmapFonts(protonScript, compatData, game, steamRoot,
                            portableFontConfig, protonLogDir, bitmapFonts);
  }
  const pid_t pid = fork();
  if (pid == 0) {
    setpgid(0, 0);
    applyGameLocaleEnvironment(game);
    const fs::path gameLogPath = launcherRoot / "logs" / ("game_" + game.folderName + "_proton.log");
    const int gameLogFd = open(gameLogPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (gameLogFd >= 0) {
      dup2(gameLogFd, STDOUT_FILENO);
      dup2(gameLogFd, STDERR_FILENO);
      dprintf(gameLogFd, "launcher game=%s engine=%s mode=proton proton=%s executable=%s compatdata=%s keyboardBridge=Select+X\n",
              game.folderName.c_str(), engineLabel(game.engine).c_str(), proton->name.c_str(),
              executable->c_str(), compatData.c_str());
    }

    const std::string steamRootText = steamRoot.string();
    const std::string compatDataText = compatData.string();
    const std::string installPath = game.path.string();
    const std::string logDir = protonLogDir.string();
    setenv("STEAM_COMPAT_DATA_PATH", compatDataText.c_str(), 1);
    setenv("STEAM_COMPAT_INSTALL_PATH", installPath.c_str(), 1);
    if (!steamRootText.empty()) setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamRootText.c_str(), 1);
    setenv("STEAM_COMPAT_APP_ID", "0", 0);
    setenv("SteamAppId", "0", 0);
    setenv("SteamGameId", "0", 0);
    setenv("PROTON_LOG", "1", 1);
    setenv("PROTON_LOG_DIR", logDir.c_str(), 1);
    setenv("MKXP_LAUNCHER_ENGINE", engineLabel(game.engine).c_str(), 1);
    setenv("MKXP_PROTON_COMPAT_MODE", "1", 1);
    setenv("MKXP_EXIT_REQUEST_FILE", exitRequestFile.c_str(), 1);
    unsetenv("MKXP_CONTROLLER_REMAP_FILE");
    applyGameProtonEnvironment(game, gameLogFd);
    if (!portableFontConfig.empty()) {
      const std::string fontConfigPath = fs::absolute(portableFontConfig).string();
      const std::string fontConfigDir = fs::absolute(portableFontConfig.parent_path()).string();
      const std::string globalFontDir = fs::absolute(launcherRoot / "assets/fonts").string();
      setenv("FONTCONFIG_FILE", fontConfigPath.c_str(), 1);
      setenv("FONTCONFIG_PATH", fontConfigDir.c_str(), 1);
      setenv("MKXP_GLOBAL_FONT_DIR", globalFontDir.c_str(), 1);
    }
    if (gameLogFd >= 0) close(gameLogFd);
    if (chdir(game.path.c_str()) != 0) _exit(126);
    execl(protonScript.c_str(), protonScript.c_str(), "run", executable->c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) {
    status = uiWord("Proton 호환 모드 실행 실패: fork 오류", "Proton compatibility launch failed: fork error", "Proton 互換モード起動失敗: fork エラー");
    return -1;
  }

  const int childStatus = waitForGameChild(pid, window, renderer, fonts, controller, exitRequestFile, true);
  terminateWolfCompatProcesses(compatData);
  if (WIFEXITED(childStatus)) {
    const int code = WEXITSTATUS(childStatus);
    status = code == 0 ? tr(UiKey::GameExit0)
                       : uiWord("Proton 호환 모드 실행 실패. exit=", "Proton compatibility mode failed. exit=", "Proton 互換モード起動失敗. exit=")
                         + std::to_string(code) + " (logs/game_*_proton.log)";
    return code;
  }
  if (WIFSIGNALED(childStatus)) {
    status = uiWord("Proton 호환 모드 비정상 종료. signal=", "Proton compatibility mode signal: ", "Proton 互換モード異常終了. signal=") + std::to_string(WTERMSIG(childStatus));
  } else {
    status = uiWord("Proton 호환 모드 게임이 비정상 종료되었습니다.", "The Proton compatibility game exited abnormally.", "Proton 互換モードのゲームが異常終了しました。");
  }
  return -1;
#else
  (void)launcherRoot; (void)game; (void)status; (void)window; (void)renderer; (void)fonts; (void)controller;
  return -1;
#endif
}
int launchGame(const fs::path& launcherRoot, const GameInfo& game, std::string& status,
               SDL_Window* window, SDL_Renderer* renderer, const FontSet& fonts,
               SDL_GameController* controller) {
  if (isEasyRpgEngine(game.engine)) return launchEasyRpgGame(launcherRoot, game, status, window, renderer, fonts, controller);
  if (isWolfRpgEngine(game.engine)) return launchWolfExecutable(launcherRoot, game, false, status, window, renderer, fonts, controller);
  if (supportsProtonCompatibility(game) && protonCompatibilityEnabled(game))
    return launchProtonCompatibilityGame(launcherRoot, game, status, window, renderer, fonts, controller);
  if (isWebEngine(game.engine)) return launchWebGame(launcherRoot, game, status, window, renderer, fonts, controller);
  if (isRgssEngine(game.engine)) return launchRgssGame(launcherRoot, game, status, window, renderer, fonts, controller);
  status = tr(UiKey::UnsupportedGame);
  return -1;
}

SDL_GameController* openFirstController() {
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    if (SDL_IsGameController(i)) return SDL_GameControllerOpen(i);
  }
  return nullptr;
}

} // namespace

int main(int, char**) {
  const fs::path root = executableRoot();
  gUiLanguage = detectUiLanguage();
  std::error_code ec;
  fs::create_directories(root / "logs", ec);
  fs::create_directories(root / "config", ec);
  fs::create_directories(root / "cache", ec);
#if defined(__linux__)
  if (!std::getenv("MKXP_TEST_ALLOW_MULTIPLE_INSTANCES")) {
    const fs::path lockPath = root / "cache/launcher.instance.lock";
    gSingleInstanceLockFd = open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (gSingleInstanceLockFd >= 0 && flock(gSingleInstanceLockFd, LOCK_EX | LOCK_NB) != 0) {
      std::ofstream duplicateLog(root / "logs/MKXP_Launcher.log", std::ios::app);
      if (duplicateLog) {
        duplicateLog << "single instance | blocked duplicate launcher pid=" << getpid() << '\n';
      }
      close(gSingleInstanceLockFd);
      gSingleInstanceLockFd = -1;
      return 0;
    }
    if (gSingleInstanceLockFd >= 0) {
      const std::string pidText = std::to_string(getpid()) + "\n";
      const int truncateRc = ftruncate(gSingleInstanceLockFd, 0);
      const ssize_t writeRc = truncateRc == 0
        ? write(gSingleInstanceLockFd, pidText.data(), pidText.size())
        : -1;
      (void)writeRc;
    }
  }
#endif

  if (const char* testGamePath = std::getenv("MKXP_TEST_NWJS_GAME")) {
    GameInfo testGame = inspectGame(fs::path(testGamePath));
    if (!isWebEngine(testGame.engine)) {
      std::cerr << "MKXP_TEST_NWJS_GAME is not an MV/MZ game\n";
      return 91;
    }
    const std::string action = std::getenv("MKXP_TEST_NWJS_ACTION")
      ? std::getenv("MKXP_TEST_NWJS_ACTION") : "detect";
    if (action == "scan") {
      std::string diagnostic;
      std::vector<NwjsProbeResult> results;
      const std::string runtime = scanNwjsRuntimeDeep(root, testGame, diagnostic,
        [&](const std::string& candidate, int index, int total) {
          std::cout << "progress=" << candidate << " " << (index + 1) << "/" << total << "\n";
        }, &results);
      for (const auto& probe : results) {
        std::cout << "candidate=" << probe.version << " state=" << probe.state
                  << " detail=" << probe.detail << "\n";
      }
      std::cout << "selected=" << runtime << " diagnostic=" << diagnostic << "\n";
      return runtime.empty() ? 92 : 0;
    }
    std::string diagnostic;
    const std::string runtime = detectNwjsRuntime(root, testGame, diagnostic);
    std::cout << "selected=" << runtime << " diagnostic=" << diagnostic << "\n";
    return runtime.empty() ? 93 : 0;
  }

  std::string configDiagnostic;
  LauncherConfig launcherConfig = loadLauncherConfig(root, configDiagnostic);
  fs::path gameRoot = launcherConfig.hasGameRoot ? launcherConfig.gameRoot : (root / "game");
  bool folderSelectionRequired = !launcherConfig.hasGameRoot || !fs::is_directory(gameRoot, ec);
  std::vector<GameInfo> allGames;
  std::vector<GameInfo> games;
  LauncherCatalog catalog;
  std::string catalogDiagnostic;
  std::ofstream log(root / "logs/MKXP_Launcher.log", std::ios::app);
  auto reloadGameRoot = [&]() {
    allGames = scanGames(gameRoot);
    for (auto& game : allGames) {
      if (isRgssEngine(game.engine)) {
        const std::string mode = configuredRubyMode(game);
        if (mode == "auto") {
          game.rubyRuntime = "ruby31";
          game.rubyDetectionSource = "deferred:auto";
        } else {
          game.rubyRuntime = mode;
          game.rubyDetectionSource = "manual:" + mode;
        }
      } else if (isWebEngine(game.engine)) {
        game.nwjsRuntime = detectNwjsRuntime(root, game, game.nwjsDetectionSource);
      }
    }
    catalogDiagnostic.clear();
    const fs::path storageRoot = catalogStorageRoot(root, gameRoot);
    migrateLegacyCatalogIfNeeded(root, gameRoot, storageRoot);
    catalog = prepareCatalog(storageRoot, allGames, catalogDiagnostic);
    games = buildGameView(allGames, catalog, launcherConfig.engineFilter, launcherConfig.sortMode);
    log << "game root | path=" << gameRoot.string() << " games=" << allGames.size()
        << " visible=" << games.size() << " filter=" << launcherConfig.engineFilter
        << " sort=" << launcherConfig.sortMode
        << " catalog=" << catalogDiagnostic << '\n';
    for (const auto& game : allGames) {
      log << "game | folder=" << game.folderName << " engine=" << engineLabel(game.engine)
          << " detect=" << game.detectionSource;
      if (isWebEngine(game.engine)) {
        log << " nwjs=" << game.nwjsRuntime << " nwjsDetect=" << game.nwjsDetectionSource
            << " webRoot=" << game.webRoot.string();
      } else if (isRgssEngine(game.engine)) {
        log << " ruby=" << game.rubyRuntime << " rubyDetect=" << game.rubyDetectionSource;
      }
      log << '\n';
    }
    log.flush();
  };

  if (!folderSelectionRequired) reloadGameRoot();
  log << "launcher start | build=V102 root=" << root.string()
      << " gameRoot=" << (folderSelectionRequired ? std::string("<select-required>") : gameRoot.string())
      << " config=" << configDiagnostic << " uiLanguage=" << uiLanguageCode(gUiLanguage) << '\n';
  log.flush();

  SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "0");
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return 1;
  }
  SDL_StopTextInput();
  UiSoundPlayer uiSounds;
  uiSounds.init(log);
  if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
    std::cerr << "SDL_image PNG init failed: " << IMG_GetError() << '\n';
  }
  if (TTF_Init() != 0) {
    std::cerr << "SDL_ttf init failed: " << TTF_GetError() << '\n';
    IMG_Quit();
    SDL_Quit();
    return 1;
  }

  const DisplayLayout displayLayout = detectDisplayLayout();
  log << "display layout | display=" << displayLayout.displayWidth << "x" << displayLayout.displayHeight
      << " logical=" << displayLayout.logicalWidth << "x" << displayLayout.logicalHeight
      << " uiOffsetY=" << displayLayout.uiOffsetY
      << " mode=" << (displayLayout.wide16x10 ? "16:10" : "16:9") << std::endl;
  log.flush();
  Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
  if (!std::getenv("MKXP_LAUNCHER_WINDOWED")) windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  const double outputCapScale = std::min({1.0,
      1920.0 / std::max(displayLayout.displayWidth, 1),
      1200.0 / std::max(displayLayout.displayHeight, 1)});
  const int targetOutputW = std::max(displayLayout.logicalWidth,
      static_cast<int>(std::lround(displayLayout.displayWidth * outputCapScale)));
  const int targetOutputH = std::max(displayLayout.logicalHeight,
      static_cast<int>(std::lround(displayLayout.displayHeight * outputCapScale)));
  const bool windowed = std::getenv("MKXP_LAUNCHER_WINDOWED") != nullptr;
  const int initialWindowW = windowed ? displayLayout.logicalWidth : targetOutputW;
  const int initialWindowH = windowed ? displayLayout.logicalHeight : targetOutputH;
  SDL_Window* window = SDL_CreateWindow("RPG Maker Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                        initialWindowW, initialWindowH, windowFlags);
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
  if (window && !renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!window || !renderer) {
    std::cerr << "SDL window/renderer failed: " << SDL_GetError() << '\n';
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 1;
  }
  SDL_RenderSetLogicalSize(renderer, displayLayout.logicalWidth, displayLayout.logicalHeight);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

  FontSet fonts = loadFonts(root);
  TextureCache thumbs(renderer);
  SDL_GameController* controller = openFirstController();

  bool running = true;
  bool gridView = launcherConfig.viewMode != "list";
  bool exitModal = false;
  bool exitYes = false;
  bool startHeld = false;
  bool selectHeld = false;
  bool startTapCandidate = false;
  bool selectTapCandidate = false;
  bool comboLatched = false;
  Uint64 comboStarted = 0;
  int analogXDir = 0;
  int analogYDir = 0;
  Uint64 analogXNext = 0;
  Uint64 analogYNext = 0;
  bool triggerL2Held = false;
  bool triggerR2Held = false;
  std::size_t selected = 0;
  int scroll = 0;
  std::string status = localizeCatalogDiagnostic(catalogDiagnostic);
  FolderPickerState folderPicker;
  GameSettingsState gameSettings;
  KeyRemapState keyRemap;
  FilterSortState filterSort;
  SearchState search;
  UiShellState uiShell;
  UpdateUiState updateUi;
  PlayHistory playHistory = loadPlayHistory(root);
  savePlayHistory(root, playHistory);
  std::string searchQuery;
  if (!games.empty()) {
    const auto recent = recentGameIndices(games, playHistory, games.size());
    if (recent.empty()) {
      uiShell.homeRow = 1;
      selected = 0;
    } else {
      uiShell.homeRow = 0;
      selected = recent.front();
    }
  }
  if (folderSelectionRequired) {
    openFolderPicker(folderPicker, gameRoot, root, true);
    status.clear();
  }

  auto rebuildGameView = [&](const std::string& preserveFolder) {
    games = buildGameView(allGames, catalog, launcherConfig.engineFilter, launcherConfig.sortMode, searchQuery);
    selected = 0;
    if (!preserveFolder.empty()) {
      for (std::size_t i = 0; i < games.size(); ++i) {
        if (games[i].folderName == preserveFolder) {
          selected = i;
          break;
        }
      }
    }
    scroll = 0;
    if (uiShell.section == MainSection::Home && !games.empty()) {
      const auto recent = recentGameIndices(games, playHistory, games.size());
      if (uiShell.homeRow == 0 && !recent.empty()) {
        uiShell.homeRecentPos = std::min(uiShell.homeRecentPos, recent.size() - 1);
        selected = recent[uiShell.homeRecentPos];
      } else {
        uiShell.homeRow = 1;
        uiShell.homeLibraryPos = std::min<std::size_t>(uiShell.homeLibraryPos, games.size() - 1);
        selected = uiShell.homeLibraryPos;
      }
    }
  };

  auto saveUiPreferences = [&](bool showStatus) {
    LauncherConfig next = launcherConfig;
    next.viewMode = gridView ? "grid" : "list";
    std::string diagnostic;
    if (saveLauncherConfig(root, next, diagnostic)) {
      launcherConfig = next;
      if (showStatus) {
        status = gridView
          ? uiWord("기본 보기를 그리드로 저장했습니다", "Default view saved as Grid", "既定表示をグリッドに保存しました")
          : uiWord("기본 보기를 리스트로 저장했습니다", "Default view saved as List", "既定表示をリストに保存しました");
      }
    } else if (showStatus) {
      status = uiWord("보기 설정 저장에 실패했습니다", "Failed to save view setting", "表示設定の保存に失敗しました");
    }
  };

  auto toggleViewMode = [&]() {
    if (folderPicker.active || gameSettings.active || filterSort.active || search.active || exitModal ||
        uiShell.libraryPageTransitionActive) return;
    gridView = !gridView;
    scroll = 0;
    uiShell.libraryPageTransitionActive = false;
    saveUiPreferences(false);
  };

  auto cycleSortMode = [&](int delta) {
    const std::string preserve = games.empty() ? std::string() : games[selected].folderName;
    LauncherConfig next = launcherConfig;
    const std::array<std::string, 2> modes{{"name", "type"}};
    int index = next.sortMode == "type" ? 1 : 0;
    index = (index + delta) % 2;
    if (index < 0) index += 2;
    next.sortMode = modes[static_cast<std::size_t>(index)];
    std::string diagnostic;
    if (saveLauncherConfig(root, next, diagnostic)) {
      launcherConfig = next;
      rebuildGameView(preserve);
    }
  };

  auto cycleEngineFilter = [&](int delta) {
    const std::string preserve = games.empty() ? std::string() : games[selected].folderName;
    int index = filterIndexFromCode(launcherConfig.engineFilter);
    index = (index + delta) % 8;
    if (index < 0) index += 8;
    LauncherConfig next = launcherConfig;
    next.engineFilter = filterCodeAt(index);
    std::string diagnostic;
    if (saveLauncherConfig(root, next, diagnostic)) {
      launcherConfig = next;
      rebuildGameView(preserve);
    }
  };
  auto stopSearchInput = [&]() {
    SDL_StopTextInput();
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "0");
  };

  auto refreshSearch = [&]() {
    const std::string preserve = games.empty() ? std::string() : games[selected].folderName;
    searchQuery = search.text;
    rebuildGameView(preserve);
    log << "search live | query=" << searchQuery << " visible=" << games.size()
        << " total=" << allGames.size() << '\n';
    log.flush();
  };

  auto openSearch = [&]() {
    if (folderPicker.active || gameSettings.active || exitModal || filterSort.active || search.active ||
        uiShell.sectionTransitionActive || uiShell.libraryPageTransitionActive) return;
    search.active = true;
    search.text = searchQuery;
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    const SDL_Rect inputRect{350, 286, 580, 62};
    SDL_SetTextInputRect(&inputRect);
    SDL_StartTextInput();
    const SteamKeyboardRequestResult keyboardRequest = requestSteamOnScreenKeyboard();
    log << "search keyboard | method=" << keyboardRequest.method
        << " shown=" << (keyboardRequest.shown ? 1 : 0)
        << " diagnostic=" << keyboardRequest.diagnostic << '\n';
    log.flush();
  };

  auto closeSearch = [&]() {
    if (!search.active) return;
    stopSearchInput();
    search.active = false;
  };

  auto clearSearch = [&]() {
    if (!search.active) return;
    search.text.clear();
    refreshSearch();
  };

  auto closeFilterSort = [&]() {
    filterSort.active = false;
  };

  auto applyFilterSort = [&]() {
    if (!filterSort.active) return;
    const std::string preserve = games.empty() ? std::string() : games[selected].folderName;
    LauncherConfig nextConfig = launcherConfig;
    nextConfig.engineFilter = filterCodeAt(filterSort.filterIndex);
    nextConfig.sortMode = filterSort.sortIndex == 0 ? "type" : "name";
    std::string diagnostic;
    if (!saveLauncherConfig(root, nextConfig, diagnostic)) {
      status = tr(UiKey::FilterSortSaveFailed);
      log << "filter/sort save failed | filter=" << nextConfig.engineFilter
          << " sort=" << nextConfig.sortMode << " diagnostic=" << diagnostic << '\n';
      log.flush();
      return;
    }
    launcherConfig = nextConfig;
    rebuildGameView(preserve);
    filterSort.active = false;
    status = tr(UiKey::FilterSortSaved);
    log << "filter/sort applied | filter=" << launcherConfig.engineFilter
        << " sort=" << launcherConfig.sortMode << " visible=" << games.size()
        << " total=" << allGames.size() << '\n';
    log.flush();
  };

  auto moveFilterSortRow = [&](int delta) {
    if (!filterSort.active) return;
    filterSort.row = (filterSort.row + delta) % 4;
    if (filterSort.row < 0) filterSort.row += 4;
    uiSounds.play(UiSoundKind::Move);
  };

  auto moveFilterSortChoice = [&](int delta) {
    if (!filterSort.active) return;
    const int beforeFilter = filterSort.filterIndex;
    const int beforeSort = filterSort.sortIndex;
    const int beforeButton = filterSort.buttonIndex;
    if (filterSort.row == 0) {
      filterSort.filterIndex = (filterSort.filterIndex + delta) % 8;
      if (filterSort.filterIndex < 0) filterSort.filterIndex += 8;
    } else if (filterSort.row == 1) {
      filterSort.sortIndex = (filterSort.sortIndex + delta) % 2;
      if (filterSort.sortIndex < 0) filterSort.sortIndex += 2;
    } else if (filterSort.row == 2) {
      filterSort.buttonIndex = (filterSort.buttonIndex + delta) % 2;
      if (filterSort.buttonIndex < 0) filterSort.buttonIndex += 2;
    }
    const bool changed = beforeFilter != filterSort.filterIndex || beforeSort != filterSort.sortIndex ||
                         beforeButton != filterSort.buttonIndex;
    uiSounds.play(changed ? UiSoundKind::Move : UiSoundKind::Boundary);
  };

  auto activateFilterSort = [&]() {
    if (!filterSort.active) return;
    if (filterSort.row < 2) {
      ++filterSort.row;
      return;
    }
    if (filterSort.row == 3) {
      filterSort.active = false;
      openFolderPicker(folderPicker, gameRoot, root, false);
      return;
    }
    if (filterSort.row == 2) {
      if (filterSort.buttonIndex == 0) applyFilterSort();
      else closeFilterSort();
    }
  };

  auto openGameFolderDialog = [&]() {
    if (exitModal || folderPicker.active || gameSettings.active || filterSort.active || search.active) return;
    openFolderPicker(folderPicker, gameRoot, root, false);
  };

  auto chooseCurrentFolder = [&]() {
    if (!folderPicker.active) return;
    std::error_code folderEc;
    if (!fs::is_directory(folderPicker.current, folderEc)) return;

    LauncherConfig nextConfig = launcherConfig;
    nextConfig.gameRoot = fs::absolute(folderPicker.current, folderEc).lexically_normal();
    nextConfig.hasGameRoot = true;
    std::string saveDiagnostic;
    if (!saveLauncherConfig(root, nextConfig, saveDiagnostic)) {
      status = tr(UiKey::FolderSaveFailed);
      log << "game root save failed | path=" << folderPicker.current.string()
          << " diagnostic=" << saveDiagnostic << '\n';
      log.flush();
      return;
    }

    launcherConfig = nextConfig;
    gameRoot = nextConfig.gameRoot;
    folderSelectionRequired = false;
    folderPicker.active = false;
    searchQuery.clear();
    search.text.clear();
    thumbs.clear();
    selected = 0;
    scroll = 0;
    reloadGameRoot();
    const auto recent = recentGameIndices(games, playHistory, games.size());
    uiShell.homeRecentPos = 0;
    uiShell.homeLibraryPos = 0;
    uiShell.homeRow = recent.empty() ? 1 : 0;
    if (!recent.empty()) selected = recent.front();
    status = std::string(tr(UiKey::FolderSaved)) + gameRoot.string();
  };

  auto cancelFolderDialog = [&]() {
    if (!folderPicker.active || folderPicker.mandatory) return;
    folderPicker.active = false;
  };

  auto moveFolderSelection = [&](int delta) {
    if (!folderPicker.active) return;
    if (folderPicker.children.empty()) {
      uiSounds.play(UiSoundKind::Boundary);
      return;
    }
    const long long n = static_cast<long long>(folderPicker.children.size());
    long long next = static_cast<long long>(folderPicker.selected) + delta;
    next %= n;
    if (next < 0) next += n;
    folderPicker.selected = static_cast<std::size_t>(next);
    const int visible = 9;
    if (static_cast<int>(folderPicker.selected) < folderPicker.scroll)
      folderPicker.scroll = static_cast<int>(folderPicker.selected);
    if (static_cast<int>(folderPicker.selected) >= folderPicker.scroll + visible)
      folderPicker.scroll = static_cast<int>(folderPicker.selected) - visible + 1;
    uiSounds.play(UiSoundKind::Move);
  };

  auto stopNameEditing = [&]() {
    if (!gameSettings.editingName) return;
    SDL_StopTextInput();
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "0");
    gameSettings.editingName = false;
  };

  auto openGameSettings = [&]() {
    if (games.empty() || folderPicker.active || exitModal || filterSort.active || search.active ||
        uiShell.sectionTransitionActive || uiShell.libraryPageTransitionActive) return;
    gameSettings.active = true;
    gameSettings.editingName = false;
    gameSettings.row = 0;
    gameSettings.scrollFirstRow = 1;
    gameSettings.locale = loadGameLocale(games[selected]);
    gameSettings.editText.clear();
    gameSettings.originalName.clear();
    gameSettings.rubyScanResult.clear();
    gameSettings.nwjsScanResult.clear();
    keyRemap.active = false;
    keyRemap.stage = 0;
    keyRemap.source = -1;
    keyRemap.entries.clear();
    keyRemap.message.clear();
  };

  auto closeGameSettings = [&]() {
    if (!gameSettings.active) return;
    stopNameEditing();
    keyRemap.active = false;
    gameSettings.active = false;
  };

  auto beginNameEditing = [&]() {
    if (!gameSettings.active || games.empty()) return;
    gameSettings.originalName = displayNameFor(games[selected], catalog);
    gameSettings.editText = gameSettings.originalName;
    gameSettings.editingName = true;
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    const SDL_Rect inputRect{650, 184, 460, 48};
    SDL_SetTextInputRect(&inputRect);
    SDL_StartTextInput();
    const SteamKeyboardRequestResult keyboardRequest = requestSteamOnScreenKeyboard();
    log << "name keyboard | method=" << keyboardRequest.method
        << " shown=" << (keyboardRequest.shown ? 1 : 0)
        << " diagnostic=" << keyboardRequest.diagnostic << '\n';
    log.flush();
  };

  auto cancelNameEditing = [&]() {
    if (!gameSettings.editingName) return;
    gameSettings.editText = gameSettings.originalName;
    stopNameEditing();
  };

  auto saveEditedName = [&]() {
    if (!gameSettings.editingName || games.empty()) return;
    const std::string folder = games[selected].folderName;
    std::string name = gameSettings.editText;
    if (name.empty()) name = folder;
    std::string diagnostic;
    if (setDisplayName(games[selected], catalog, name, diagnostic)) {
      status = tr(UiKey::NameSaved);
      log << "game name saved | folder=" << folder << " display=" << name << '\n';
      rebuildGameView(folder);
    } else {
      status = std::string(tr(UiKey::NameSaveFailed)) + " (" + diagnostic + ")";
      log << "game name save failed | folder=" << folder << " diagnostic=" << diagnostic << '\n';
    }
    log.flush();
    stopNameEditing();
  };

  auto saveRubyMode = [&](const std::string& mode) {
    if (!gameSettings.active || games.empty() || !isRgssEngine(games[selected].engine)) return;
    auto& game = games[selected];
    std::ofstream out(game.path / "mkxp-ruby.txt", std::ios::binary | std::ios::trunc);
    if (!out) {
      status = tr(UiKey::RubySaveFailed);
      return;
    }
    out << mode << '\n';
    out.close();

    if (mode == "auto") {
      game.rubyRuntime = "ruby31";
      game.rubyDetectionSource = "deferred:auto";
      gameSettings.rubyScanResult.clear();
    } else {
      game.rubyRuntime = mode;
      game.rubyDetectionSource = "manual:" + mode;
    }
    for (auto& source : allGames) {
      if (source.folderName == game.folderName) {
        source.rubyRuntime = game.rubyRuntime;
        source.rubyDetectionSource = game.rubyDetectionSource;
        break;
      }
    }
    status = std::string(tr(UiKey::RubySaved)) + rubySettingText(game);
    log << "ruby setting saved | folder=" << game.folderName
        << " mode=" << mode << " runtime=" << game.rubyRuntime << '\n';
    log.flush();
  };

  auto stepRubyMode = [&](int delta) {
    if (!gameSettings.active || games.empty() || !isRgssEngine(games[selected].engine)) return;
    const auto& game = games[selected];
    const std::vector<std::string> modes{"auto", "ruby18", "ruby19", "ruby31"};
    const std::string current = rubyIsManual(game) ? game.rubyRuntime : "auto";
    auto it = std::find(modes.begin(), modes.end(), current);
    int index = it == modes.end() ? 0 : static_cast<int>(std::distance(modes.begin(), it));
    index = (index + delta) % static_cast<int>(modes.size());
    if (index < 0) index += static_cast<int>(modes.size());
    saveRubyMode(modes[static_cast<std::size_t>(index)]);
  };

  auto saveNwjsMode = [&](const std::string& mode) {
    if (!gameSettings.active || games.empty() || !isWebEngine(games[selected].engine)) return;
    auto& game = games[selected];
    std::ofstream out(game.path / "mkxp-nwjs.txt", std::ios::binary | std::ios::trunc);
    if (!out) {
      status = tr(UiKey::NwjsSaveFailed);
      return;
    }
    out << mode << '\n';
    out.close();

    if (mode == "auto") {
      game.nwjsRuntime = detectNwjsRuntime(root, game, game.nwjsDetectionSource);
      gameSettings.nwjsScanResult.clear();
    } else {
      game.nwjsRuntime = mode;
      game.nwjsDetectionSource = "manual:" + mode;
    }
    for (auto& source : allGames) {
      if (source.folderName == game.folderName) {
        source.nwjsRuntime = game.nwjsRuntime;
        source.nwjsDetectionSource = game.nwjsDetectionSource;
        break;
      }
    }
    status = std::string(tr(UiKey::NwjsSetting)) + nwjsSettingText(game);
    log << "nwjs setting saved | folder=" << game.folderName
        << " mode=" << mode << " runtime=" << game.nwjsRuntime
        << " source=" << game.nwjsDetectionSource << '\n';
    log.flush();
  };

  auto stepNwjsMode = [&](int delta) {
    if (!gameSettings.active || games.empty() || !isWebEngine(games[selected].engine)) return;
    const auto& game = games[selected];
    std::vector<std::string> modes{"auto"};
    const auto versions = installedNwjsVersions(root);
    modes.insert(modes.end(), versions.begin(), versions.end());
    const std::string current = nwjsIsManual(game) ? game.nwjsRuntime : "auto";
    auto it = std::find(modes.begin(), modes.end(), current);
    int index = it == modes.end() ? 0 : static_cast<int>(std::distance(modes.begin(), it));
    index = (index + delta) % static_cast<int>(modes.size());
    if (index < 0) index += static_cast<int>(modes.size());
    saveNwjsMode(modes[static_cast<std::size_t>(index)]);
  };

  auto stepEasyRpgEncoding = [&](int delta) {
    if (!gameSettings.active || games.empty() || !isEasyRpgEngine(games[selected].engine)) return;
    auto& game = games[selected];
    const std::array<std::string, 4> choices{{"auto", "949", "932", "1252"}};
    const std::string current = readEasyRpgEncodingChoice(game);
    auto it = std::find(choices.begin(), choices.end(), current);
    int index = it == choices.end() ? 0 : static_cast<int>(std::distance(choices.begin(), it));
    index = (index + delta) % static_cast<int>(choices.size());
    if (index < 0) index += static_cast<int>(choices.size());
    const std::string next = choices[static_cast<std::size_t>(index)];
    if (!writeEasyRpgEncodingChoice(game, next)) {
      status = uiWord("EasyRPG 인코딩 저장 실패", "Failed to save EasyRPG encoding", "EasyRPG エンコーディングの保存に失敗しました");
      return;
    }
    status = uiWord("EasyRPG 인코딩: ", "EasyRPG encoding: ", "EasyRPG エンコーディング: ") + easyRpgEncodingLabel(game);
    log << "easyrpg encoding saved | folder=" << game.folderName << " value=" << next << '\n';
    log.flush();
  };
  auto stepEasyRpgChoice = [&](EasyRpgChoiceKind kind, int delta) {
    if (!gameSettings.active || games.empty() || !isEasyRpgEngine(games[selected].engine)) return;
    auto& game = games[selected];
    const auto choices = easyRpgAssetChoices(root, kind);
    if (choices.empty()) return;
    const std::string current = readEasyRpgChoice(game, kind);
    auto it = std::find(choices.begin(), choices.end(), current);
    int index = it == choices.end() ? 0 : static_cast<int>(std::distance(choices.begin(), it));
    index = (index + delta) % static_cast<int>(choices.size());
    if (index < 0) index += static_cast<int>(choices.size());
    const std::string next = choices[static_cast<std::size_t>(index)];
    if (!writeEasyRpgChoice(game, kind, next)) {
      status = uiWord("EasyRPG 설정 저장 실패", "Failed to save EasyRPG settings", "EasyRPG 設定の保存に失敗しました");
      return;
    }
    const char* key = kind == EasyRpgChoiceKind::Soundfont ? "soundfont" :
                      (kind == EasyRpgChoiceKind::Font1 ? "font1" : "font2");
    status = std::string("EasyRPG ") + key + ": " + easyRpgChoiceLabel(game, kind);
    log << "easyrpg setting saved | folder=" << game.folderName
        << " key=" << key << " value=" << next << '\n';
    log.flush();
  };
  auto stepCompatibilityMode = [&](int delta) {
    if (!gameSettings.active || games.empty() || !supportsProtonCompatibility(games[selected])) return;
    auto& game = games[selected];
    const std::array<std::string, 2> choices{{"native", "proton"}};
    const std::string current = readCompatibilityMode(game);
    auto it = std::find(choices.begin(), choices.end(), current);
    int index = it == choices.end() ? 0 : static_cast<int>(std::distance(choices.begin(), it));
    index = (index + delta) % static_cast<int>(choices.size());
    if (index < 0) index += static_cast<int>(choices.size());
    const std::string next = choices[static_cast<std::size_t>(index)];
    if (!writeCompatibilityMode(game, next)) {
      status = uiWord("호환성 모드 저장 실패", "Failed to save compatibility mode", "互換モードの保存に失敗しました");
      return;
    }
    if (next == "proton") ensureProtonCustomEnvTemplate(game);
    status = uiWord("호환성 모드: ", "Compatibility mode: ", "互換モード: ") + compatibilityModeLabel(game);
    log << "compatibility mode saved | folder=" << game.folderName << " mode=" << next << '\n';
    log.flush();
  };

  auto stepGameProton = [&](int delta) {
    if (!gameSettings.active || games.empty() || !supportsProtonCompatibility(games[selected])) return;
    auto& game = games[selected];
    const auto tools = installedProtonTools();
    if (tools.empty()) return;
    const std::string current = readGameProtonChoice(game);
    int index = 0;
    for (int i = 0; i < static_cast<int>(tools.size()); ++i) {
      if (tools[static_cast<std::size_t>(i)].id == current) { index = i; break; }
    }
    index = (index + delta) % static_cast<int>(tools.size());
    if (index < 0) index += static_cast<int>(tools.size());
    const auto& next = tools[static_cast<std::size_t>(index)];
    if (!writeGameProtonChoice(game, next.id)) {
      status = uiWord("Proton 설정 저장 실패", "Failed to save Proton setting", "Proton 設定の保存に失敗しました");
      return;
    }
    status = std::string("Proton: ") + gameProtonLabel(game);
    log << "game proton setting saved | folder=" << game.folderName
        << " id=" << next.id << " name=" << next.name
        << " installed=" << (next.installed ? 1 : 0) << '\n';
    log.flush();
  };

  auto stepProtonEnvPreset = [&](int delta) {
    if (!gameSettings.active || games.empty() || !supportsProtonCompatibility(games[selected]) ||
        !protonCompatibilityEnabled(games[selected])) return;
    auto& game = games[selected];
    const auto& ids = protonEnvPresetIds();
    const std::string current = readProtonEnvPreset(game);
    auto it = std::find(ids.begin(), ids.end(), current);
    int index = it == ids.end() ? 0 : static_cast<int>(std::distance(ids.begin(), it));
    index = (index + delta) % static_cast<int>(ids.size());
    if (index < 0) index += static_cast<int>(ids.size());
    const std::string next = ids[static_cast<std::size_t>(index)];
    if (!writeProtonEnvPreset(game, next)) {
      status = uiWord("Proton 환경변수 프리셋 저장 실패", "Failed to save Proton environment preset", "Proton 環境変数プリセットの保存に失敗しました");
      return;
    }
    ensureProtonCustomEnvTemplate(game);
    status = uiWord("Proton 환경변수: ", "Proton environment: ", "Proton 環境変数: ") + protonEnvPresetLabel(game);
    log << "proton env preset saved | folder=" << game.folderName
        << " preset=" << next << " custom=" << customProtonEnvironmentCount(game) << '\n';
    log.flush();
  };
  auto stepWolfProton = [&](int delta) {
    if (!gameSettings.active || games.empty() || !isWolfRpgEngine(games[selected].engine)) return;
    auto& game = games[selected];
    const auto tools = installedProtonTools();
    if (tools.empty()) return;
    const std::string current = readWolfProtonChoice(game);
    int index = 0;
    for (int i = 0; i < static_cast<int>(tools.size()); ++i) {
      if (tools[static_cast<std::size_t>(i)].id == current) {
        index = i;
        break;
      }
    }
    index = (index + delta) % static_cast<int>(tools.size());
    if (index < 0) index += static_cast<int>(tools.size());
    const auto& next = tools[static_cast<std::size_t>(index)];
    if (!writeWolfProtonChoice(game, next.id)) {
      status = uiWord("WOLF Proton 설정 저장 실패", "Failed to save WOLF Proton setting", "WOLF Proton 設定の保存に失敗しました");
      return;
    }
    status = std::string("WOLF Proton: ") + wolfProtonLabel(game);
    log << "wolf proton setting saved | folder=" << game.folderName
        << " id=" << next.id << " name=" << next.name
        << " installed=" << (next.installed ? 1 : 0) << '\n';
    log.flush();
  };

  auto stepGameLocale = [&](int delta) {
    if (!gameSettings.active || games.empty()) return;
    int index = static_cast<int>(gameSettings.locale);
    index = (index + delta) % 3;
    if (index < 0) index += 3;
    const GameLocale next = static_cast<GameLocale>(index);
    if (!saveGameLocale(games[selected], next)) {
      status = uiWord("로케일 설정 저장 실패", "Failed to save game locale", "ロケール設定の保存に失敗しました");
      return;
    }
    gameSettings.locale = next;
    status = uiWord("게임 로케일: ", "Game locale: ", "ゲームロケール: ") + gameLocaleUiLabel(next);
    log << "game locale saved | folder=" << games[selected].folderName
        << " locale=" << gameLocaleCode(next)
        << " posix=" << gameLocalePosix(next) << '\n';
    log.flush();
  };

  auto adjustGameSettingsValue = [&](int delta) {
    if (!gameSettings.active || games.empty()) return;
    const GameInfo& game = games[selected];
    if (gameSettings.row == 1) {
      stepGameLocale(delta);
      uiSounds.play(UiSoundKind::Move);
      return;
    }
    if (isRgssEngine(game.engine) && gameSettings.row == 2) {
      stepRubyMode(delta);
      uiSounds.play(UiSoundKind::Move);
      return;
    }
    if (isWebEngine(game.engine) && gameSettings.row == 2) {
      stepNwjsMode(delta);
      uiSounds.play(UiSoundKind::Move);
      return;
    }
    if (supportsProtonCompatibility(game) && gameSettings.row == 3) {
      stepCompatibilityMode(delta);
      uiSounds.play(UiSoundKind::Move);
      return;
    }
    if (supportsProtonCompatibility(game) && protonCompatibilityEnabled(game) && gameSettings.row == 4) {
      stepGameProton(delta);
      uiSounds.play(UiSoundKind::Move);
      return;
    }
    if (supportsProtonCompatibility(game) && protonCompatibilityEnabled(game) && gameSettings.row == 5) {
      stepProtonEnvPreset(delta);
      uiSounds.play(UiSoundKind::Move);
      return;
    }
    if (isEasyRpgEngine(game.engine) && gameSettings.row == 2) {
      stepEasyRpgEncoding(delta);
      uiSounds.play(UiSoundKind::Move);
      return;
    }
    if (isEasyRpgEngine(game.engine) && gameSettings.row >= 3 && gameSettings.row <= 5) {
      const EasyRpgChoiceKind kind = gameSettings.row == 3 ? EasyRpgChoiceKind::Soundfont :
                                     (gameSettings.row == 4 ? EasyRpgChoiceKind::Font1 :
                                                              EasyRpgChoiceKind::Font2);
      stepEasyRpgChoice(kind, delta);
      uiSounds.play(UiSoundKind::Move);
      return;
    }
    if (isWolfRpgEngine(game.engine) && gameSettings.row == 2) {
      stepWolfProton(delta);
      uiSounds.play(UiSoundKind::Move);
      return;
    }
    uiSounds.play(UiSoundKind::Boundary);
  };

  auto runWolfConfig = [&]() {
    if (!gameSettings.active || games.empty() || !isWolfRpgEngine(games[selected].engine)) return;
    if (!findWolfExecutable(games[selected], true)) {
      status = uiWord("WOLF Config.exe를 찾을 수 없습니다.", "WOLF Config.exe was not found.", "WOLF Config.exe が見つかりません。");
      return;
    }
    drawFrontendStandby(renderer);
    log << "frontend surface | mapped standby kind=wolf-config folder=" << games[selected].folderName
        << " proton=" << wolfProtonLabel(games[selected]) << '\n';
    log.flush();
    launchWolfExecutable(root, games[selected], true, status, window, renderer, fonts, controller);
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_KEYDOWN, SDL_CONTROLLERBUTTONUP);
    log << "frontend surface | child returned kind=wolf-config folder=" << games[selected].folderName << '\n';
    log.flush();
  };
  auto drawNwjsDeepScanProgress = [&](const std::string& version, int index, int total) {
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, BG.r, BG.g, BG.b, BG.a);
    SDL_RenderClear(renderer);
    setUiContentViewport(renderer);
    const SDL_Rect panel{330, 205, 620, 310};
    fillRoundedRect(renderer, panel, 20, SDL_Color{7, 24, 41, 242});
    strokeRoundedRect(renderer, panel, 20, SDL_Color{102, 184, 230, 105}, 1);

    drawText(renderer, fonts.normal, nwjsDeepScanRunningText(), WIDTH / 2, 245, WHITE, true);
    drawText(renderer, fonts.medium, std::string("NW.js ") + version, WIDTH / 2, 305, SELECT_BG, true);
    const std::string count = std::to_string(index + 1) + " / " + std::to_string(std::max(total, 1));
    drawText(renderer, fonts.medium, count, WIDTH / 2, 365, WHITE, true);
    const char* help = gUiLanguage == UiLanguage::Korean
      ? "게임을 숨김 실행해 렌더러/오류/WebGL 호환성을 검사합니다..."
      : (gUiLanguage == UiLanguage::Japanese
         ? "ゲームを非表示起動してレンダラー / エラー / WebGL 互換性を確認します..."
         : "Hidden-launching the game to test renderer, errors and WebGL compatibility...");
    drawText(renderer, fonts.small, help, WIDTH / 2, 438, MUTED2, true);
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_RenderPresent(renderer);
    SDL_PumpEvents();
    SDL_Delay(20);
  };

  auto runNwjsDeepScan = [&]() {
    if (!gameSettings.active || games.empty() || !isWebEngine(games[selected].engine)) return;
    auto& game = games[selected];
    status = nwjsDeepScanRunningText();
    log << "nwjs deep scan start | folder=" << game.folderName << '\n';
    log.flush();

    std::string diagnostic;
    std::vector<NwjsProbeResult> results;
    const std::string runtime = scanNwjsRuntimeDeep(root, game, diagnostic,
      [&](const std::string& candidate, int index, int total) {
        drawNwjsDeepScanProgress(candidate, index, total);
      }, &results);

    std::ofstream out(game.path / "mkxp-nwjs.txt", std::ios::binary | std::ios::trunc);
    if (out) out << "auto\n";
    game.nwjsRuntime = runtime;
    game.nwjsDetectionSource = diagnostic;
    gameSettings.nwjsScanResult = runtime.empty()
      ? std::string(tr(UiKey::NwjsMissingShort))
      : nwjsDeepScanDoneText(runtime);
    for (auto& source : allGames) {
      if (source.folderName == game.folderName) {
        source.nwjsRuntime = runtime;
        source.nwjsDetectionSource = diagnostic;
        break;
      }
    }
    for (const auto& probe : results) {
      log << "nwjs deep scan candidate | folder=" << game.folderName
          << " version=" << probe.version << " state=" << probe.state
          << " detail=" << probe.detail << '\n';
    }
    status = gameSettings.nwjsScanResult;
    log << "nwjs deep scan done | folder=" << game.folderName
        << " runtime=" << runtime << " diagnostic=" << diagnostic << '\n';
    log.flush();
  };
  auto drawRubyDeepScanProgress = [&](const std::string& runtimeName) {
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, BG.r, BG.g, BG.b, BG.a);
    SDL_RenderClear(renderer);
    setUiContentViewport(renderer);
    const SDL_Rect panel{330, 205, 620, 310};
    fillRoundedRect(renderer, panel, 20, SDL_Color{7, 24, 41, 242});
    strokeRoundedRect(renderer, panel, 20, SDL_Color{102, 184, 230, 105}, 1);

    const std::string version = runtimeName == "ruby18" ? "1.8" :
                                (runtimeName == "ruby19" ? "1.9" :
                                 (runtimeName == "ruby31" ? "3.1" : ""));
    drawText(renderer, fonts.normal, rubyDeepScanRunningText(), WIDTH / 2, 245, WHITE, true);
    drawText(renderer, fonts.medium, std::string("Ruby ") + version, WIDTH / 2, 302, SELECT_BG, true);

    const std::array<std::string, 3> names{{"1.8", "1.9", "3.1"}};
    const std::array<std::string, 3> ids{{"ruby18", "ruby19", "ruby31"}};
    int current = 0;
    for (int i = 0; i < 3; ++i) if (ids[static_cast<std::size_t>(i)] == runtimeName) current = i;
    for (int i = 0; i < 3; ++i) {
      const SDL_Rect step{405 + i * 165, 365, 140, 54};
      const bool active = i == current;
      const bool done = i < current;
      fillRoundedRect(renderer, step, 14, active ? SELECT_BG : CARD_INNER);
      strokeRoundedRect(renderer, step, 14, active ? SDL_Color{140, 218, 255, 180} : BORDER, 1);
      const std::string label = (done ? "✓  " : (active ? "▶  " : "")) + std::string("Ruby ") + names[static_cast<std::size_t>(i)];
      drawText(renderer, fonts.small, label, step.x + step.w / 2, step.y + 16,
               active ? SELECT_TEXT : (done ? WHITE : MUTED2), true);
    }
    const char* help = gUiLanguage == UiLanguage::Korean ? "스크립트 섹션을 하나씩 검사하고 있습니다..." :
                       (gUiLanguage == UiLanguage::Japanese ? "スクリプトを1セクションずつ確認しています..." :
                        "Checking script sections one by one...");
    drawText(renderer, fonts.small, help, WIDTH / 2, 458, MUTED2, true);
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_RenderPresent(renderer);
    SDL_PumpEvents();
    SDL_Delay(20);
  };

  auto runRubyDeepScan = [&]() {
    if (!gameSettings.active || games.empty() || !isRgssEngine(games[selected].engine)) return;
    auto& game = games[selected];
    status = rubyDeepScanRunningText();
    log << "ruby deep scan start | folder=" << game.folderName << '\n';
    log.flush();
    drawRubyDeepScanProgress("ruby18");
    std::string diagnostic;
    const std::string runtime = scanRubyRuntimeDeep(root, game, diagnostic,
      [&](const std::string& candidate) { drawRubyDeepScanProgress(candidate); });
    std::ofstream out(game.path / "mkxp-ruby.txt", std::ios::binary | std::ios::trunc);
    if (out) out << "auto\n";
    game.rubyRuntime = runtime;
    game.rubyDetectionSource = diagnostic;
    gameSettings.rubyScanResult = rubyDeepScanDoneText(runtime);
    for (auto& source : allGames) {
      if (source.folderName == game.folderName) {
        source.rubyRuntime = runtime;
        source.rubyDetectionSource = diagnostic;
        break;
      }
    }
    status = gameSettings.rubyScanResult;
    log << "ruby deep scan done | folder=" << game.folderName
        << " runtime=" << runtime << " diagnostic=" << diagnostic << '\n';
    log.flush();
  };

  auto openKeyRemap = [&]() {
    if (!gameSettings.active || games.empty() || !isRgssEngine(games[selected].engine)) return;
    if (protonCompatibilityEnabled(games[selected])) {
      status = uiWord("Proton 구동 시 MKXP 게임별 키매핑을 사용할 수 없습니다.",
                      "MKXP per-game mapping is unavailable in Proton mode.",
                      "Proton モードでは MKXP のゲーム別マッピングを使用できません。");
      uiSounds.play(UiSoundKind::Boundary);
      return;
    }
    keyRemap.active = true;
    keyRemap.stage = 0;
    keyRemap.source = -1;
    keyRemap.entries = loadControllerRemapFile(games[selected]);
    keyRemap.message.clear();
  };

  auto closeKeyRemap = [&]() {
    keyRemap.active = false;
    keyRemap.stage = 0;
    keyRemap.source = -1;
  };

  auto resetKeyRemaps = [&]() {
    if (games.empty()) return;
    keyRemap.entries.clear();
    if (saveControllerRemapFile(games[selected], keyRemap.entries)) {
      keyRemap.message = gUiLanguage == UiLanguage::Korean ? "게임별 매핑을 초기화했습니다" :
                         (gUiLanguage == UiLanguage::Japanese ? "ゲーム別マッピングを初期化しました" :
                          "Per-game mappings reset");
    }
  };

  auto saveCapturedKeyRemap = [&](int destination) {
    if (games.empty() || keyRemap.source < 0 || destination < 0) return;
    std::vector<ControllerRemapEntry> next;
    for (const auto& entry : keyRemap.entries) {
      if (entry.from == keyRemap.source) continue;
      if (entry.to == destination) continue;
      next.push_back(entry);
    }
    next.push_back({keyRemap.source, destination});
    keyRemap.entries.swap(next);
    if (saveControllerRemapFile(games[selected], keyRemap.entries)) {
      keyRemap.message = std::string(controllerButtonShortName(keyRemap.source)) + " → " +
                         controllerButtonShortName(destination);
      log << "controller remap saved | folder=" << games[selected].folderName
          << " from=" << controllerButtonShortName(keyRemap.source)
          << " to=" << controllerButtonShortName(destination) << '\n';
      log.flush();
    }
    keyRemap.stage = 0;
    keyRemap.source = -1;
  };

  auto settingsRowCount = [&]() -> int {
    if (games.empty()) return 0;
    if (isRgssEngine(games[selected].engine)) return protonCompatibilityEnabled(games[selected]) ? 9 : 7;
    if (isWebEngine(games[selected].engine)) return protonCompatibilityEnabled(games[selected]) ? 8 : 6;
    if (isEasyRpgEngine(games[selected].engine)) return 7;
    if (isWolfRpgEngine(games[selected].engine)) return 5;
    return 3;
  };

  auto settingsDoneRow = [&]() -> int {
    if (games.empty()) return 0;
    if (isRgssEngine(games[selected].engine)) return protonCompatibilityEnabled(games[selected]) ? 8 : 6;
    if (isWebEngine(games[selected].engine)) return protonCompatibilityEnabled(games[selected]) ? 7 : 5;
    if (isEasyRpgEngine(games[selected].engine)) return 6;
    if (isWolfRpgEngine(games[selected].engine)) return 4;
    return 2;
  };

  constexpr int SETTINGS_VISIBLE_MIDDLE_ROWS = 5;
  auto clampSettingsScroll = [&]() {
    const int doneRow = settingsDoneRow();
    const int middleRows = std::max(0, doneRow - 1);
    const int maxFirst = std::max(1, middleRows - SETTINGS_VISIBLE_MIDDLE_ROWS + 1);
    gameSettings.scrollFirstRow = std::clamp(gameSettings.scrollFirstRow, 1, maxFirst);
  };

  auto ensureSettingsRowVisible = [&]() {
    clampSettingsScroll();
    const int doneRow = settingsDoneRow();
    if (gameSettings.row <= 0) {
      gameSettings.scrollFirstRow = 1;
      return;
    }
    if (gameSettings.row >= doneRow) return;
    if (gameSettings.row < gameSettings.scrollFirstRow)
      gameSettings.scrollFirstRow = gameSettings.row;
    if (gameSettings.row >= gameSettings.scrollFirstRow + SETTINGS_VISIBLE_MIDDLE_ROWS)
      gameSettings.scrollFirstRow = gameSettings.row - SETTINGS_VISIBLE_MIDDLE_ROWS + 1;
    clampSettingsScroll();
  };

  auto scrollSettingsContent = [&](int delta) {
    if (!gameSettings.active || games.empty() || delta == 0) return;
    const int doneRow = settingsDoneRow();
    const int middleRows = std::max(0, doneRow - 1);
    const int maxFirst = std::max(1, middleRows - SETTINGS_VISIBLE_MIDDLE_ROWS + 1);
    if (maxFirst <= 1) {
      uiSounds.play(UiSoundKind::Boundary);
      return;
    }
    const int before = gameSettings.scrollFirstRow;
    gameSettings.scrollFirstRow = std::clamp(before + delta, 1, maxFirst);
    if (gameSettings.scrollFirstRow == before) {
      uiSounds.play(UiSoundKind::Boundary);
      return;
    }
    if (gameSettings.row > 0 && gameSettings.row < doneRow) {
      if (gameSettings.row < gameSettings.scrollFirstRow)
        gameSettings.row = gameSettings.scrollFirstRow;
      const int lastVisible = gameSettings.scrollFirstRow + SETTINGS_VISIBLE_MIDDLE_ROWS - 1;
      if (gameSettings.row > lastVisible)
        gameSettings.row = std::min(lastVisible, doneRow - 1);
    }
    uiSounds.play(UiSoundKind::Move);
  };

  auto moveSettingsRow = [&](int delta) {
    if (!gameSettings.active || gameSettings.editingName) return;
    const int count = settingsRowCount();
    if (count <= 0) return;
    gameSettings.row = (gameSettings.row + delta) % count;
    if (gameSettings.row < 0) gameSettings.row += count;
    ensureSettingsRowVisible();
    uiSounds.play(UiSoundKind::Move);
  };

  auto activateSettingsRow = [&]() {
    if (!gameSettings.active || gameSettings.editingName || games.empty()) return;
    if (gameSettings.row == 0) {
      beginNameEditing();
      return;
    }
    if (gameSettings.row == 1) {
      stepGameLocale(1);
      return;
    }
    if (isRgssEngine(games[selected].engine) && gameSettings.row == 2) {
      stepRubyMode(1);
      return;
    }
    if (isRgssEngine(games[selected].engine) && gameSettings.row == 3) {
      stepCompatibilityMode(1);
      return;
    }
    if (isRgssEngine(games[selected].engine) && protonCompatibilityEnabled(games[selected]) && gameSettings.row == 4) {
      stepGameProton(1);
      return;
    }
    if (isRgssEngine(games[selected].engine) && protonCompatibilityEnabled(games[selected]) && gameSettings.row == 5) {
      stepProtonEnvPreset(1);
      return;
    }
    if (isRgssEngine(games[selected].engine) &&
        gameSettings.row == (protonCompatibilityEnabled(games[selected]) ? 6 : 4)) {
      runRubyDeepScan();
      return;
    }
    if (isRgssEngine(games[selected].engine) &&
        gameSettings.row == (protonCompatibilityEnabled(games[selected]) ? 7 : 5)) {
      openKeyRemap();
      return;
    }
    if (isWebEngine(games[selected].engine) && gameSettings.row == 2) {
      stepNwjsMode(1);
      return;
    }
    if (isWebEngine(games[selected].engine) && gameSettings.row == 3) {
      stepCompatibilityMode(1);
      return;
    }
    if (isWebEngine(games[selected].engine) && protonCompatibilityEnabled(games[selected]) && gameSettings.row == 4) {
      stepGameProton(1);
      return;
    }
    if (isWebEngine(games[selected].engine) && protonCompatibilityEnabled(games[selected]) && gameSettings.row == 5) {
      stepProtonEnvPreset(1);
      return;
    }
    if (isWebEngine(games[selected].engine) &&
        gameSettings.row == (protonCompatibilityEnabled(games[selected]) ? 6 : 4)) {
      runNwjsDeepScan();
      return;
    }
    if (isEasyRpgEngine(games[selected].engine) && gameSettings.row == 2) {
      stepEasyRpgEncoding(1);
      return;
    }
    if (isEasyRpgEngine(games[selected].engine) && gameSettings.row == 3) {
      stepEasyRpgChoice(EasyRpgChoiceKind::Soundfont, 1);
      return;
    }
    if (isEasyRpgEngine(games[selected].engine) && gameSettings.row == 4) {
      stepEasyRpgChoice(EasyRpgChoiceKind::Font1, 1);
      return;
    }
    if (isEasyRpgEngine(games[selected].engine) && gameSettings.row == 5) {
      stepEasyRpgChoice(EasyRpgChoiceKind::Font2, 1);
      return;
    }
    if (isWolfRpgEngine(games[selected].engine) && gameSettings.row == 2) {
      stepWolfProton(1);
      return;
    }
    if (isWolfRpgEngine(games[selected].engine) && gameSettings.row == 3) {
      runWolfConfig();
      return;
    }
    if (gameSettings.row == settingsDoneRow()) closeGameSettings();
  };

  auto handlePointerDown = [&](float x, float y) {
    if (uiShell.sectionTransitionActive || uiShell.libraryPageTransitionActive) return;
    if (keyRemap.active) return;
    if (search.active) {
      const SDL_Rect clearButton{430, 390, 190, 52};
      const SDL_Rect closeButton{660, 390, 190, 52};
      if (pointInRect(x, y, clearButton)) clearSearch();
      else if (pointInRect(x, y, closeButton)) closeSearch();
      return;
    }
    if (filterSort.active) {
      const int filterX = 286;
      const int filterY = 200;
      const int filterW = 82;
      const int filterGap = 6;
      for (int i = 0; i < 8; ++i) {
        if (pointInRect(x, y, SDL_Rect{filterX + i * (filterW + filterGap), filterY, filterW, 48})) {
          filterSort.row = 0;
          filterSort.filterIndex = i;
          return;
        }
      }
      const SDL_Rect typeSort{420, 310, 200, 52};
      const SDL_Rect nameSort{660, 310, 200, 52};
      if (pointInRect(x, y, typeSort)) {
        filterSort.row = 1;
        filterSort.sortIndex = 0;
        return;
      }
      if (pointInRect(x, y, nameSort)) {
        filterSort.row = 1;
        filterSort.sortIndex = 1;
        return;
      }
      const SDL_Rect applyButton{445, 405, 180, 52};
      const SDL_Rect cancelButton{655, 405, 180, 52};
      if (pointInRect(x, y, applyButton)) {
        filterSort.row = 2;
        filterSort.buttonIndex = 0;
        applyFilterSort();
        return;
      }
      if (pointInRect(x, y, cancelButton)) {
        filterSort.row = 2;
        filterSort.buttonIndex = 1;
        closeFilterSort();
        return;
      }
      const SDL_Rect folderButton{420, 485, 440, 52};
      if (pointInRect(x, y, folderButton)) {
        filterSort.row = 3;
        filterSort.active = false;
        openFolderPicker(folderPicker, gameRoot, root, false);
        return;
      }
      return;
    }
    if (folderPicker.active) {
      const SDL_Rect useButton{370, 574, 250, 52};
      const SDL_Rect upButton{640, 574, 190, 52};
      const SDL_Rect cancelButton{850, 574, 150, 52};
      if (pointInRect(x, y, useButton)) {
        chooseCurrentFolder();
        return;
      }
      if (pointInRect(x, y, upButton)) {
        folderPickerParent(folderPicker);
        return;
      }
      if (!folderPicker.mandatory && pointInRect(x, y, cancelButton)) {
        cancelFolderDialog();
        return;
      }
      const int rowX = 230;
      const int rowY = 198;
      const int rowW = 820;
      const int rowH = 39;
      const int visible = 9;
      for (int slot = 0; slot < visible; ++slot) {
        const int index = folderPicker.scroll + slot;
        if (index >= static_cast<int>(folderPicker.children.size())) break;
        if (pointInRect(x, y, SDL_Rect{rowX, rowY + slot * rowH, rowW, rowH - 2})) {
          folderPicker.selected = static_cast<std::size_t>(index);
          folderPickerEnter(folderPicker);
          return;
        }
      }
      return;
    }
    if (gameSettings.active) {
      if (gameSettings.editingName) return;
      clampSettingsScroll();
      const SDL_Rect nameRow{460, 176, 680, 52};
      const SDL_Rect doneButton{860, 468, 240, 42};
      if (pointInRect(x, y, nameRow)) {
        gameSettings.row = 0;
        beginNameEditing();
        return;
      }
      const int doneRow = settingsDoneRow();
      for (int slot = 0; slot < SETTINGS_VISIBLE_MIDDLE_ROWS; ++slot) {
        const int actualRow = gameSettings.scrollFirstRow + slot;
        if (actualRow >= doneRow) break;
        const SDL_Rect rowRect{460, 236 + slot * 44, 680, 40};
        if (!pointInRect(x, y, rowRect)) continue;
        gameSettings.row = actualRow;
        activateSettingsRow();
        ensureSettingsRowVisible();
        return;
      }
      if (pointInRect(x, y, doneButton)) {
        gameSettings.row = doneRow;
        closeGameSettings();
        return;
      }
      return;
    }
  };

  auto libraryPageSize = [&]() -> int {
    return gridView ? 10 : 9;
  };

  auto startLibraryPageTransition = [&](std::size_t fromSelected, int fromScroll,
                                        std::size_t toSelected, int toScroll, int direction) {
    if (fromSelected == toSelected || direction == 0) return;
    uiShell.libraryPageTransitionActive = true;
    uiShell.libraryPageTransitionDirection = direction > 0 ? 1 : -1;
    uiShell.libraryPageTransitionFromSelected = fromSelected;
    uiShell.libraryPageTransitionFromScroll = fromScroll;
    uiShell.libraryPageTransitionToSelected = toSelected;
    uiShell.libraryPageTransitionToScroll = toScroll;
    uiShell.libraryPageTransitionStartedAt = SDL_GetTicks64();
  };

  auto moveSelection = [&](int dx, int dy) {
    if (games.empty() || exitModal || folderPicker.active || gameSettings.active || filterSort.active || search.active ||
        uiShell.libraryPageTransitionActive) return;
    const std::size_t before = selected;
    if (!gridView) {
      if (dy < 0 && selected > 0) --selected;
      if (dy > 0 && selected + 1 < games.size()) ++selected;
    } else {
      if (dx < 0 && selected > 0) --selected;
      if (dx > 0 && selected + 1 < games.size()) ++selected;
      if (dy < 0 && selected >= 5) selected -= 5;
      if (dy > 0 && selected + 5 < games.size()) selected += 5;
    }
    if (gridView && selected != before && before / 10 != selected / 10) {
      startLibraryPageTransition(before, scroll, selected, scroll, selected > before ? 1 : -1);
    }
    if (dx != 0 || dy != 0)
      uiSounds.play(selected != before ? UiSoundKind::Move : UiSoundKind::Boundary);
  };

  auto movePage = [&](int direction) {
    if (games.empty() || exitModal || folderPicker.active || gameSettings.active || filterSort.active || search.active ||
        uiShell.libraryPageTransitionActive || direction == 0) return;
    const int pageSize = libraryPageSize();
    const int currentPage = static_cast<int>(selected) / pageSize;
    const int lastPage = static_cast<int>((games.size() - 1) / static_cast<std::size_t>(pageSize));
    const int targetPage = std::clamp(currentPage + (direction > 0 ? 1 : -1), 0, lastPage);
    if (targetPage == currentPage) {
      uiSounds.play(UiSoundKind::Boundary);
      return;
    }
    const int offset = static_cast<int>(selected) % pageSize;
    const std::size_t targetBase = static_cast<std::size_t>(targetPage * pageSize);
    const std::size_t targetSelected = std::min(targetBase + static_cast<std::size_t>(offset), games.size() - 1);
    const int targetScroll = gridView ? scroll : targetPage * pageSize;
    const std::size_t fromSelected = selected;
    const int fromScroll = scroll;
    selected = targetSelected;
    scroll = targetScroll;
    startLibraryPageTransition(fromSelected, fromScroll, selected, scroll, targetPage > currentPage ? 1 : -1);
    uiSounds.play(UiSoundKind::Move);
  };
  auto homeRecentIndices = [&]() {
    return recentGameIndices(games, playHistory, games.size());
  };

  auto syncHomeSelected = [&]() {
    if (games.empty()) return;
    if (uiShell.homeRow == 0) {
      const auto recent = homeRecentIndices();
      if (!recent.empty()) {
        uiShell.homeRecentPos = std::min(uiShell.homeRecentPos, recent.size() - 1);
        selected = recent[uiShell.homeRecentPos];
        return;
      }
      uiShell.homeRow = 1;
    }
    const std::size_t count = games.size();
    if (count > 0) {
      uiShell.homeLibraryPos = std::min(uiShell.homeLibraryPos, count - 1);
      selected = uiShell.homeLibraryPos;
    }
  };

  auto startSectionTransition = [&](MainSection target) {
    if (uiShell.sectionTransitionActive || uiShell.libraryPageTransitionActive || uiShell.section == target) return;
    uiShell.sectionTransitionFrom = uiShell.section;
    uiShell.sectionTransitionTo = target;
    uiShell.sectionTransitionStartedAt = SDL_GetTicks64();
    uiShell.sectionTransitionActive = true;
    uiShell.section = target;
    if (target == MainSection::Library) {
      uiShell.sidebarIndex = 1;
      if (!games.empty()) {
        uiShell.homeLibraryPos = std::min(uiShell.homeLibraryPos, games.size() - 1);
        selected = uiShell.homeLibraryPos;
      }
    }
  };

  auto moveHomePage = [&](int direction) {
    if (uiShell.section != MainSection::Home || uiShell.sidebarFocused || games.empty() ||
        exitModal || folderPicker.active || gameSettings.active || filterSort.active || search.active) return;
    if (uiShell.homeRow != 1) return;
    const std::size_t before = uiShell.homeLibraryPos;
    const long long last = static_cast<long long>(games.size()) - 1;
    const long long next = std::clamp(static_cast<long long>(before) + static_cast<long long>(direction) * 6LL, 0LL, last);
    uiShell.homeLibraryPos = static_cast<std::size_t>(next);
    syncHomeSelected();
    uiSounds.play(uiShell.homeLibraryPos != before ? UiSoundKind::Move : UiSoundKind::Boundary);
  };

  auto quickCycleEngineFilter = [&](int delta) {
    const bool homeLibraryFocused = uiShell.section == MainSection::Home && uiShell.homeRow == 1;
    const bool libraryFocused = uiShell.section == MainSection::Library;
    if (uiShell.sidebarFocused || uiShell.sectionTransitionActive || uiShell.libraryPageTransitionActive ||
        (!homeLibraryFocused && !libraryFocused) ||
        exitModal || folderPicker.active || gameSettings.active || filterSort.active || search.active) return false;
    cycleEngineFilter(delta);
    selected = 0;
    scroll = 0;
    if (uiShell.section == MainSection::Home) {
      uiShell.homeLibraryPos = 0;
      syncHomeSelected();
    }
    uiSounds.play(UiSoundKind::Move);
    return true;
  };

  auto activateSidebar = [&]() {
    uiSounds.play(UiSoundKind::SidebarOut);
    if (uiShell.sidebarIndex == 0) {
      uiShell.section = MainSection::Home;
      uiShell.homeRow = homeRecentIndices().empty() ? 1 : 0;
      syncHomeSelected();
    } else if (uiShell.sidebarIndex == 1) {
      uiShell.section = MainSection::Library;
    } else if (uiShell.sidebarIndex == 2) {
      openSearch();
    } else {
      uiShell.section = MainSection::Settings;
      uiShell.settingsRow = 0;
      uiShell.settingsDetailFocused = false;
    }
    if (!search.active) uiShell.sidebarFocused = false;
  };

  auto checkForLauncherUpdate = [&]() {
    status = uiWord("업데이트 확인 중...", "Checking for updates...", "アップデートを確認中...");
    const UpdateHelperResult result = runUpdateHelper(root, "check");
    updateUi.checked = true;
    updateUi.latestVersion = result.latest;
    updateUi.available = result.ok && result.status == "update";
    updateUi.prompt = updateUi.available;
    updateUi.promptYes = true;
    if (!result.ok) {
      updateUi.message = uiWord("업데이트 확인에 실패했습니다. 네트워크를 확인해주세요.",
                                "Update check failed. Check your network connection.",
                                "アップデート確認に失敗しました。ネットワークを確認してください。");
    } else if (updateUi.available) {
      updateUi.message = uiWord("새 버전을 찾았습니다: ", "New version available: ", "新しいバージョン: ") +
                         std::string("v") + result.latest;
    } else {
      updateUi.message = uiWord("현재 최신 버전입니다.", "You are up to date.", "最新バージョンです。");
    }
    status = updateUi.message;
  };

  auto installLauncherUpdate = [&]() {
    if (!updateUi.available || updateUi.latestVersion.empty()) return;
    status = uiWord("업데이트 다운로드 및 설치 중...", "Downloading and installing update...",
                    "アップデートをダウンロードしてインストール中...");
    const UpdateHelperResult result = runUpdateHelper(root, "install", updateUi.latestVersion);
    updateUi.prompt = false;
    if (result.ok && result.status == "installed") {
      updateUi.available = false;
      updateUi.message = uiWord("설치 완료. 런처를 종료한 뒤 다시 실행해주세요.",
                                "Update installed. Restart the launcher to use the new version.",
                                "インストール完了。ランチャーを再起動してください。");
    } else {
      updateUi.message = uiWord("업데이트 설치에 실패했습니다.", "Update installation failed.",
                                "アップデートのインストールに失敗しました。");
    }
    status = updateUi.message;
  };

  auto activateSettingsScreen = [&]() {
    if (!uiShell.settingsDetailFocused) {
      uiShell.settingsDetailFocused = true;
      return;
    }
    if (uiShell.settingsRow == 0) {
      toggleViewMode();
      saveUiPreferences(true);
    } else if (uiShell.settingsRow == 1) {
      cycleSortMode(1);
    } else if (uiShell.settingsRow == 2) {
      cycleEngineFilter(1);
    } else if (uiShell.settingsRow == 3) {
      openGameFolderDialog();
    } else if (uiShell.settingsRow == 4) {
      checkForLauncherUpdate();
    }
  };

  auto moveHome = [&](int dx, int dy) {
    const bool beforeSidebar = uiShell.sidebarFocused;
    const int beforeRow = uiShell.homeRow;
    const std::size_t beforeRecent = uiShell.homeRecentPos;
    const std::size_t beforeLibrary = uiShell.homeLibraryPos;
    if (dy > 0 && uiShell.homeRow == 1) {
      startSectionTransition(MainSection::Library);
      uiSounds.play(UiSoundKind::Move);
      return;
    }
    if (games.empty()) {
      if (dx < 0) uiShell.sidebarFocused = true;
      if (!beforeSidebar && uiShell.sidebarFocused) uiSounds.play(UiSoundKind::SidebarIn);
      else uiSounds.play(UiSoundKind::Boundary);
      return;
    }
    const auto recent = homeRecentIndices();
    if (dy < 0 && uiShell.homeRow == 1 && !recent.empty()) uiShell.homeRow = 0;
    if (dy > 0 && uiShell.homeRow == 0) uiShell.homeRow = 1;
    if (uiShell.homeRow == 0 && !recent.empty()) {
      if (dx < 0) {
        if (uiShell.homeRecentPos == 0) uiShell.sidebarFocused = true;
        else --uiShell.homeRecentPos;
      } else if (dx > 0 && uiShell.homeRecentPos + 1 < recent.size()) {
        ++uiShell.homeRecentPos;
      }
    } else {
      uiShell.homeRow = 1;
      const std::size_t count = games.size();
      if (dx < 0) {
        if (uiShell.homeLibraryPos == 0) uiShell.sidebarFocused = true;
        else --uiShell.homeLibraryPos;
      } else if (dx > 0 && uiShell.homeLibraryPos + 1 < count) {
        ++uiShell.homeLibraryPos;
      }
    }
    syncHomeSelected();
    if (!beforeSidebar && uiShell.sidebarFocused) uiSounds.play(UiSoundKind::SidebarIn);
    else if (beforeRow != uiShell.homeRow || beforeRecent != uiShell.homeRecentPos || beforeLibrary != uiShell.homeLibraryPos)
      uiSounds.play(UiSoundKind::Move);
    else uiSounds.play(UiSoundKind::Boundary);
  };
  auto moveByDirection = [&](int dx, int dy) {
    if (uiShell.sectionTransitionActive || uiShell.libraryPageTransitionActive) return;
    if (keyRemap.active || search.active || gameSettings.editingName) return;
    if (updateUi.prompt) {
      if (dx < 0) updateUi.promptYes = true;
      if (dx > 0) updateUi.promptYes = false;
      return;
    }
    if (filterSort.active) {
      if (dy < 0) moveFilterSortRow(-1);
      if (dy > 0) moveFilterSortRow(1);
      if (dx < 0) moveFilterSortChoice(-1);
      if (dx > 0) moveFilterSortChoice(1);
      return;
    }
    if (gameSettings.active) {
      if (dy < 0) moveSettingsRow(-1);
      if (dy > 0) moveSettingsRow(1);
      if (dx < 0) adjustGameSettingsValue(-1);
      if (dx > 0) adjustGameSettingsValue(1);
      return;
    }
    if (folderPicker.active) {
      if (dy < 0) moveFolderSelection(-1);
      if (dy > 0) moveFolderSelection(1);
      if (dx != 0) uiSounds.play(UiSoundKind::Boundary);
      return;
    }
    if (exitModal) {
      const bool before = exitYes;
      if (dx < 0) exitYes = true;
      if (dx > 0) exitYes = false;
      if (dx != 0) uiSounds.play(before != exitYes ? UiSoundKind::Move : UiSoundKind::Boundary);
      return;
    }
    if (uiShell.sidebarFocused) {
      const int before = uiShell.sidebarIndex;
      if (dy < 0) uiShell.sidebarIndex = (uiShell.sidebarIndex + 3) % 4;
      if (dy > 0) uiShell.sidebarIndex = (uiShell.sidebarIndex + 1) % 4;
      if (dx > 0) {
        uiShell.sidebarFocused = false;
        uiSounds.play(UiSoundKind::SidebarOut);
      } else if (uiShell.sidebarIndex != before) {
        uiSounds.play(UiSoundKind::Move);
      } else if (dx != 0 || dy != 0) {
        uiSounds.play(UiSoundKind::Boundary);
      }
      return;
    }
    if (uiShell.section == MainSection::Home) {
      moveHome(dx, dy);
      return;
    }
    if (uiShell.section == MainSection::Settings) {
      if (uiShell.settingsDetailFocused) {
        if (uiShell.settingsRow == 0 && dx != 0) {
          const bool wantGrid = dx > 0;
          if (gridView != wantGrid) { gridView = wantGrid; scroll = 0; saveUiPreferences(true); uiSounds.play(UiSoundKind::Move); }
          else uiSounds.play(UiSoundKind::Boundary);
        } else if (uiShell.settingsRow == 1 && dx != 0) { cycleSortMode(dx); uiSounds.play(UiSoundKind::Move); }
        else if (uiShell.settingsRow == 2 && dx != 0) { cycleEngineFilter(dx); uiSounds.play(UiSoundKind::Move); }
        else if (dx != 0 || dy != 0) uiSounds.play(UiSoundKind::Boundary);
        return;
      }
      if (dx < 0) { uiShell.sidebarFocused = true; uiSounds.play(UiSoundKind::SidebarIn); return; }
      if (dy < 0) { uiShell.settingsRow = (uiShell.settingsRow + 4) % 5; uiSounds.play(UiSoundKind::Move); }
      if (dy > 0) { uiShell.settingsRow = (uiShell.settingsRow + 1) % 5; uiSounds.play(UiSoundKind::Move); }
      if (dx > 0) { uiShell.settingsDetailFocused = true; uiSounds.play(UiSoundKind::Move); }
      return;
    }
    if (!gridView && dx < 0) { uiShell.sidebarFocused = true; uiSounds.play(UiSoundKind::SidebarIn); return; }
    if (gridView && dx < 0 && selected % 5 == 0) { uiShell.sidebarFocused = true; uiSounds.play(UiSoundKind::SidebarIn); return; }
    moveSelection(dx, dy);
  };

  auto confirm = [&]() {
    if (uiShell.sectionTransitionActive || uiShell.libraryPageTransitionActive) return;
    if (updateUi.prompt) {
      if (updateUi.promptYes) installLauncherUpdate();
      else updateUi.prompt = false;
      return;
    }
    if (exitModal) {
      if (exitYes) running = false;
      else exitModal = false;
      return;
    }
    if (uiShell.sidebarFocused) {
      activateSidebar();
      return;
    }
    if (uiShell.section == MainSection::Settings) {
      activateSettingsScreen();
      return;
    }
    if (games.empty() || folderPicker.active || gameSettings.active || filterSort.active || search.active) return;
    // ES-DE-style handoff: keep the frontend window mapped for the entire game
    // lifetime.  A black standby frame sits behind the child so Gamescope always
    // has a valid frontend surface to fall back to when the game closes.
    drawFrontendStandby(renderer);
    log << "frontend surface | mapped standby kind=game folder=" << games[selected].folderName
        << " engine=" << engineLabel(games[selected].engine) << '\n';
    log.flush();
    if (isRgssEngine(games[selected].engine) && !protonCompatibilityEnabled(games[selected]) &&
        games[selected].rubyDetectionSource == "deferred:auto") {
      auto& game = games[selected];
      game.rubyRuntime = detectRubyRuntime(root, game, game.rubyDetectionSource);
      for (auto& source : allGames) {
        if (source.folderName == game.folderName) {
          source.rubyRuntime = game.rubyRuntime;
          source.rubyDetectionSource = game.rubyDetectionSource;
          break;
        }
      }
      log << "ruby lazy detect | folder=" << game.folderName
          << " runtime=" << game.rubyRuntime
          << " source=" << game.rubyDetectionSource << '\n';
      log.flush();
    }
    status = std::string(tr(UiKey::Launching)) + displayNameFor(games[selected], catalog);
    SDL_PumpEvents();
    const auto playStartedAt = std::chrono::steady_clock::now();
    launchGame(root, games[selected], status, window, renderer, fonts, controller);
    log << "frontend surface | child returned kind=game folder=" << games[selected].folderName << '\n';
    log.flush();
    const auto playedFor = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - playStartedAt).count();
    auto& historyEntry = playHistory[historyGameKey(games[selected])];
    historyEntry.lastPlayed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
    historyEntry.totalSeconds += static_cast<std::uint64_t>(std::max<std::int64_t>(0, playedFor));
    historyEntry.launchCount += 1;
    trimPlayHistory(playHistory);
    savePlayHistory(root, playHistory);
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_KEYDOWN, SDL_CONTROLLERBUTTONUP);
    startHeld = false;
    selectHeld = false;
    startTapCandidate = false;
    selectTapCandidate = false;
    comboLatched = false;
    comboStarted = 0;
    const Uint64 resumeAt = SDL_GetTicks64() + 350;
    while (SDL_GetTicks64() < resumeAt) {
      SDL_PumpEvents();
      SDL_FlushEvents(SDL_KEYDOWN, SDL_CONTROLLERBUTTONUP);
      SDL_Delay(10);
    }
  };

  auto drawSidebar = [&]() {
    const int collapsed = 82;
    const int expanded = 252;
    const int width = collapsed + static_cast<int>((expanded - collapsed) * uiShell.sidebarAnim);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Rect shell{8, 10, width - 16, 640};
    fillRoundedRect(renderer, shell, 22, SDL_Color{5, 20, 36, static_cast<Uint8>(uiShell.sidebarFocused ? 218 : 182)});
    strokeRoundedRect(renderer, shell, 22, SDL_Color{104, 166, 214, 46}, 1);

    const std::array<std::string, 4> labels{{
      uiWord("홈", "Home", "ホーム"),
      uiWord("라이브러리", "Library", "ライブラリ"),
      uiWord("검색", "Search", "検索"),
      uiWord("설정", "Settings", "設定")
    }};
    const std::array<int, 4> ys{{112, 184, 256, 328}};
    SDL_Texture* iconSheet = thumbs.get(root / "assets/ui/nav_icons_v049.png");
    int iconW = 0;
    int iconH = 0;
    if (iconSheet) SDL_QueryTexture(iconSheet, nullptr, nullptr, &iconW, &iconH);

    const SDL_Rect appIconBox{22, 22, 46, 46};
    drawBrandLogo(renderer, appIconBox);
    if (uiShell.sidebarAnim > 0.42f) {
      drawText(renderer, fonts.small, "RPG MAKER PLAYER", 78, 24, WHITE);
      drawText(renderer, fonts.small,
               uiWord("알만툴 올인원", "All-in-One Player", "RPGツクール統合プレイヤー"),
               79, 49, SDL_Color{127, 174, 211, 255});
    }

    for (int i = 0; i < 4; ++i) {
      const bool sectionActive = (i == 0 && uiShell.section == MainSection::Home) ||
                                 (i == 1 && uiShell.section == MainSection::Library) ||
                                 (i == 3 && uiShell.section == MainSection::Settings);
      const bool focused = uiShell.sidebarFocused && uiShell.sidebarIndex == i;
      const SDL_Rect item{18, ys[i] - 20, width - 36, 50};
      if (sectionActive || focused) {
        fillRoundedRect(renderer, item, 14,
                        focused ? SDL_Color{35, 105, 166, 194} : SDL_Color{24, 69, 109, 154});
        if (focused) strokeRoundedRect(renderer, item, 14, SDL_Color{111, 202, 255, 145}, 1);
      }
      const int itemCenterY = item.y + item.h / 2;
      // Keep the collapsed sidebar icons exactly centered in the 66px shell.
      // shell.x=8, collapsed shell.w=66 => horizontal center X=41.
      constexpr int collapsedNavCenterX = 41;
      const SDL_Rect iconDst{collapsedNavCenterX - 14, itemCenterY - 14, 28, 28};
      if (iconSheet && iconW >= 4 && iconH > 0) {
        SDL_Rect src{i * (iconW / 4), 0, iconW / 4, iconH};
        SDL_SetTextureAlphaMod(iconSheet, static_cast<Uint8>(focused || sectionActive ? 255 : 160));
        SDL_RenderCopy(renderer, iconSheet, &src, &iconDst);
        SDL_SetTextureAlphaMod(iconSheet, 255);
      } else {
        drawNavIcon(renderer, i, iconDst.x + iconDst.w / 2, itemCenterY,
                    focused || sectionActive ? UI_BLUE_SOFT : SDL_Color{164, 190, 212, 255});
      }
      if (uiShell.sidebarAnim > 0.42f) {
        drawText(renderer, fonts.medium, labels[static_cast<std::size_t>(i)], 78,
                 centeredTextY(fonts.medium, item, labels[static_cast<std::size_t>(i)]),
                 focused ? WHITE : (sectionActive ? SDL_Color{216, 239, 255, 255} : SDL_Color{196, 211, 226, 255}));
      }
    }
  };

  auto drawTopTitle = [&](const std::string& title, const std::string& subtitle) {
    drawText(renderer, fonts.normal, title, 108, 22, WHITE);
    if (!subtitle.empty()) drawText(renderer, fonts.small, subtitle, 110, 55, SDL_Color{130, 164, 192, 255});
  };

  auto drawBrandHeader = [&]() {
    const SDL_Rect logo{108, 14, 48, 48};
    drawBrandLogo(renderer, logo);
    const std::string maker = "RPG MAKER";
    const int titleX = 166;
    drawText(renderer, fonts.normal, maker, titleX, 16, WHITE);
    drawText(renderer, fonts.normal, "PLAYER", titleX + textWidth(fonts.normal, maker) + 10, 16,
             UI_BLUE_SOFT);
    drawText(renderer, fonts.small,
             uiWord("알만툴 올인원 플레이어", "RPG Maker All-in-One Player", "RPGツクール オールインワン プレイヤー"),
             titleX + 1, 49, SDL_Color{143, 178, 205, 255});
  };

  auto drawEnginePills = [&](int x, int y) {
    for (int i = 0; i < 8; ++i) {
      const std::string label = filterLabelAt(i);
      const int w = i == 0 ? 62 : (i == 5 ? 78 : 64);
      const bool active = launcherConfig.engineFilter == filterCodeAt(i);
      SDL_Rect pill{x, y, w, 30};
      if (active) {
        fillRoundedRect(renderer, SDL_Rect{pill.x - 2, pill.y - 2, pill.w + 4, pill.h + 4},
                        17, SDL_Color{76, 183, 255, 66});
        fillRoundedRect(renderer, pill, 15, SDL_Color{34, 108, 176, 232});
      } else {
        fillRoundedRect(renderer, pill, 15, SDL_Color{14, 38, 61, 172});
        strokeRoundedRect(renderer, pill, 15, SDL_Color{90, 125, 153, 44}, 1);
      }
      drawText(renderer, fonts.small, label, pill.x + pill.w / 2,
               centeredTextY(fonts.small, pill, label),
               active ? WHITE : SDL_Color{178, 199, 216, 255}, true);
      x += w + 8;
    }
  };

  auto drawHome = [&]() {
    drawBrandHeader();

    const auto recent = homeRecentIndices();
    drawText(renderer, fonts.normal, uiWord("최근 플레이", "Recent Play", "最近のプレイ"), 108, 76, WHITE);

    constexpr std::size_t recentVisible = 5;
    if (recent.empty()) {
      const SDL_Rect empty{108, 120, 1152, 224};
      fillRoundedRect(renderer, empty, 18, SDL_Color{8, 28, 46, 160});
      strokeRoundedRect(renderer, empty, 18, SDL_Color{99, 153, 193, 48}, 1);
      drawText(renderer, fonts.medium,
               uiWord("게임을 실행하면 최근 플레이에 표시됩니다", "Played games will appear here", "ゲームを起動するとここに表示されます"),
               empty.x + empty.w / 2, centeredTextY(fonts.medium, empty), SDL_Color{210, 225, 238, 255}, true);
    } else {
      const std::size_t recentStart = uiShell.homeRecentPos >= recentVisible
        ? uiShell.homeRecentPos - recentVisible + 1 : 0;
      const std::size_t recentEnd = std::min(recent.size(), recentStart + recentVisible);
      const int cardW = 216;
      const int gap = 18;
      for (std::size_t absolutePos = recentStart; absolutePos < recentEnd; ++absolutePos) {
        const std::size_t slot = absolutePos - recentStart;
        const auto index = recent[absolutePos];
        const auto& game = games[index];
        const int x = 108 + static_cast<int>(slot) * (cardW + gap);
        const SDL_Rect card{x, 122, cardW, 226};
        const SDL_Rect image{card.x + 3, card.y + 3, card.w - 6, 174};
        const SDL_Rect titleArea{card.x + 10, card.y + 182, card.w - 20, 38};
        const bool active = uiShell.homeRow == 0 && uiShell.homeRecentPos == absolutePos && !uiShell.sidebarFocused;

        fillRoundedRect(renderer, card, 17, SDL_Color{7, 27, 44, 174});
        SDL_Texture* thumb = thumbs.get(thumbnailPathFor(gameRoot, catalog, game));
        if (thumb) drawCoverRounded(renderer, thumb, image, 14);
        else fillRoundedRect(renderer, image, 14, SDL_Color{22, 48, 72, 220});
        drawEngineBadge(renderer, fonts.small, game.engine, image);
        drawCenteredMarqueeText(renderer, fonts.medium, displayNameFor(game, catalog),
                                titleArea, WHITE, active, "home-recent:" + historyGameKey(game));

        if (active) {
          strokeRoundedRect(renderer, SDL_Rect{card.x - 2, card.y - 2, card.w + 4, card.h + 4},
                            19, SDL_Color{64, 177, 255, 62}, 2);
          strokeRoundedRect(renderer, card, 17, SDL_Color{137, 220, 255, 230}, 1);
        } else {
          strokeRoundedRect(renderer, card, 17, SDL_Color{116, 157, 188, 80}, 1);
        }
      }
      if (recentStart > 0)
        drawText(renderer, fonts.normal, "‹", 96, 214, SDL_Color{119, 198, 245, 210}, true);
      if (recentEnd < recent.size())
        drawText(renderer, fonts.normal, "›", 1272, 214, SDL_Color{119, 198, 245, 210}, true);
    }

    const std::string homeLibraryTitle = uiWord("내 라이브러리", "My Library", "マイライブラリ");
    const SDL_Rect homeLibraryHeaderRow{108, 376, 170, 30};
    drawText(renderer, fonts.normal, homeLibraryTitle, homeLibraryHeaderRow.x,
             centeredTextY(fonts.normal, homeLibraryHeaderRow, homeLibraryTitle), WHITE);
    drawEnginePills(286, 376);
    drawText(renderer, fonts.small,
             launcherConfig.sortMode == "type"
               ? uiWord("정렬 · 종류순", "Sort · Type", "並び替え · 種類")
               : uiWord("정렬 · 이름순", "Sort · Name", "並び替え · 名前"),
             1260, 383, SDL_Color{156, 180, 201, 255}, false, true);

    constexpr std::size_t libraryVisible = 6;
    const std::size_t libraryStart = (uiShell.homeLibraryPos / libraryVisible) * libraryVisible;
    const std::size_t libraryEnd = std::min(games.size(), libraryStart + libraryVisible);
    const int smallW = 182;
    const int gap = 12;
    for (std::size_t absolutePos = libraryStart; absolutePos < libraryEnd; ++absolutePos) {
      const std::size_t slot = absolutePos - libraryStart;
      const auto& game = games[absolutePos];
      const int x = 108 + static_cast<int>(slot) * (smallW + gap);
      const SDL_Rect card{x, 424, smallW, 226};
      const SDL_Rect image{card.x + 3, card.y + 3, card.w - 6, 174};
      const SDL_Rect titleArea{card.x + 9, card.y + 182, card.w - 18, 38};
      const bool active = uiShell.homeRow == 1 && uiShell.homeLibraryPos == absolutePos && !uiShell.sidebarFocused;

      fillRoundedRect(renderer, card, 15, SDL_Color{8, 28, 46, 176});
      SDL_Texture* thumb = thumbs.get(thumbnailPathFor(gameRoot, catalog, game));
      if (thumb) drawCoverRounded(renderer, thumb, image, 12);
      else fillRoundedRect(renderer, image, 12, SDL_Color{24, 52, 78, 210});
      drawEngineBadge(renderer, fonts.small, game.engine, image);
      drawCenteredMarqueeText(renderer, fonts.small, displayNameFor(game, catalog),
                              titleArea, WHITE, active, "home-library:" + historyGameKey(game));

      if (active) {
        strokeRoundedRect(renderer, SDL_Rect{card.x - 2, card.y - 2, card.w + 4, card.h + 4},
                          17, SDL_Color{86, 194, 255, 58}, 1);
        strokeRoundedRect(renderer, card, 15, SDL_Color{127, 217, 255, 225}, 1);
      } else {
        strokeRoundedRect(renderer, card, 15, SDL_Color{100, 140, 171, 58}, 1);
      }
    }
    if (games.empty()) {
      drawText(renderer, fonts.medium, tr(UiKey::EmptyGames), 108, 455, MUTED);
    } else {
      if (libraryStart > 0)
        drawText(renderer, fonts.normal, "‹", 96, 520, SDL_Color{119, 198, 245, 210}, true);
      if (libraryEnd < games.size())
        drawText(renderer, fonts.normal, "›", 1272, 520, SDL_Color{119, 198, 245, 210}, true);
    }
  };

  auto drawLibraryHeader = [&]() {
    drawTopTitle(uiWord("내 라이브러리", "My Library", "マイライブラリ"),
                 filteredGameCountText(games.size(), allGames.size()));
    drawEnginePills(108, 82);
    drawText(renderer, fonts.small,
             launcherConfig.sortMode == "type"
               ? uiWord("정렬 · 종류순", "Sort · Type", "並び替え · 種類")
               : uiWord("정렬 · 이름순", "Sort · Name", "並び替え · 名前"),
             1260, 90, SDL_Color{157, 181, 201, 255}, false, true);

    if (gridView && !games.empty()) {
      const int pageStart = static_cast<int>(selected / 10) * 10;
      drawText(renderer, fonts.small,
               std::to_string(pageStart + 1) + "-" +
               std::to_string(std::min(pageStart + 10, static_cast<int>(games.size()))) +
               " / " + std::to_string(games.size()),
               1260, 55, SDL_Color{139, 164, 184, 255}, false, true);
    }
  };

  auto drawLibraryContent = [&](int yOffset) {

    if (!gridView) {
      const int visible = 9;
      if (!games.empty()) {
        if (static_cast<int>(selected) < scroll) scroll = static_cast<int>(selected);
        if (static_cast<int>(selected) >= scroll + visible) scroll = static_cast<int>(selected) - visible + 1;
      }
      for (int i = 0; i < visible && scroll + i < static_cast<int>(games.size()); ++i) {
        const int index = scroll + i;
        const auto& game = games[static_cast<std::size_t>(index)];
        const int y = 126 + yOffset + i * 56;
        const bool active = static_cast<std::size_t>(index) == selected && !uiShell.sidebarFocused;
        const SDL_Rect row{102, y, 1158, 48};
        fillRoundedRect(renderer, row, 13,
                        active ? SDL_Color{25, 75, 116, 205} : SDL_Color{7, 27, 44, 145});
        if (active) strokeRoundedRect(renderer, row, 13, SDL_Color{113, 207, 255, 95}, 1);

        SDL_Texture* thumb = thumbs.get(thumbnailPathFor(gameRoot, catalog, game));
        const SDL_Rect image{110, y + 4, 68, 40};
        if (thumb) drawCoverRounded(renderer, thumb, image, 9);
        else fillRoundedRect(renderer, image, 9, SDL_Color{26, 53, 76, 220});

        drawMarqueeText(renderer, fonts.medium, displayNameFor(game, catalog),
                        SDL_Rect{194, y + 10, 500, 28},
                        active ? WHITE : SDL_Color{221, 232, 242, 255},
                        active, "library-list:" + historyGameKey(game));

        const SDL_Rect engineTag{720, y + 10, 64, 27};
        fillRoundedRect(renderer, engineTag, 13, SDL_Color{27, 67, 101, 170});
        drawText(renderer, fonts.small, compactEngineLabel(game.engine),
                 engineTag.x + engineTag.w / 2,
               centeredTextY(fonts.small, engineTag, compactEngineLabel(game.engine)),
                 active ? SDL_Color{218, 242, 255, 255} : SDL_Color{172, 202, 224, 255}, true);

        drawText(renderer, fonts.small, truncateText(fonts.small, runtimeUiLabel(game), 300),
                 1238, y + 13, SDL_Color{145, 169, 190, 255}, false, true);
      }
    } else {
      const int pageStart = games.empty() ? 0 : static_cast<int>(selected / 10) * 10;
      const int cardW = 220;
      const int gap = 13;
      const int rowGap = 270;
      for (int slot = 0; slot < 10 && pageStart + slot < static_cast<int>(games.size()); ++slot) {
        const int index = pageStart + slot;
        const auto& game = games[static_cast<std::size_t>(index)];
        const int col = slot % 5;
        const int rowIndex = slot / 5;
        const int x = 108 + col * (cardW + gap);
        const int y = 126 + yOffset + rowIndex * rowGap;
        const SDL_Rect card{x, y, cardW, 254};
        const bool active = static_cast<std::size_t>(index) == selected && !uiShell.sidebarFocused;

        fillRoundedRect(renderer, card, 16, SDL_Color{7, 27, 44, 158});
        SDL_Texture* thumb = thumbs.get(thumbnailPathFor(gameRoot, catalog, game));
        const SDL_Rect image{card.x + 3, card.y + 3, card.w - 6, 194};
        if (thumb) drawCoverRounded(renderer, thumb, image, 13);
        else fillRoundedRect(renderer, image, 13, SDL_Color{24, 51, 76, 210});

        drawEngineBadge(renderer, fonts.small, game.engine, image);
        drawCenteredMarqueeText(renderer, fonts.medium, displayNameFor(game, catalog),
                                SDL_Rect{card.x + 10, card.y + 206, card.w - 20, 40},
                                WHITE, active, "library-grid:" + historyGameKey(game));

        if (active) {
          strokeRoundedRect(renderer, SDL_Rect{card.x - 2, card.y - 2, card.w + 4, card.h + 4},
                            18, SDL_Color{71, 183, 255, 58}, 1);
          strokeRoundedRect(renderer, card, 16, SDL_Color{133, 220, 255, 228}, 1);
        } else {
          strokeRoundedRect(renderer, card, 16, SDL_Color{99, 140, 171, 58}, 1);
        }
      }
    }
  };

  auto drawLibrary = [&]() {
    drawLibraryHeader();
    drawLibraryContent(0);
  };

  auto drawSettingsScreen = [&]() {
    drawTopTitle(uiWord("설정", "Settings", "設定"),
                 uiWord("런처의 기본 동작을 필요한 항목만 간단하게 관리합니다",
                        "Manage only the launcher settings you actually use",
                        "必要なランチャー設定だけを管理します"));

    const std::array<std::string, 5> labels{{
      uiWord("보기 방식", "Library View", "表示形式"),
      uiWord("정렬", "Sorting", "並び替え"),
      uiWord("기본 필터", "Default Filter", "既定フィルター"),
      uiWord("게임 폴더", "Game Folder", "ゲームフォルダー"),
      uiWord("업데이트", "Update", "アップデート")
    }};
    const std::array<std::string, 5> descriptions{{
      uiWord("라이브러리의 기본 표시 방식을 선택합니다.",
             "Choose the default Library layout.",
             "ライブラリの既定表示形式を選びます。"),
      uiWord("게임 목록의 기본 정렬 기준을 선택합니다.",
             "Choose the default Library ordering.",
             "ゲーム一覧の既定並び順を選びます。"),
      uiWord("라이브러리에 처음 표시할 엔진 필터를 선택합니다.",
             "Choose the engine filter shown when Library opens.",
             "ライブラリを開いた時のエンジンフィルターを選びます。"),
      uiWord("게임을 검색할 기본 폴더를 변경합니다.",
             "Change the folder scanned for games.",
             "ゲームを検索する既定フォルダーを変更します。"),
      uiWord("원할 때만 GitHub에서 새 버전을 확인하고 설치합니다.",
             "Check GitHub for a new version only when you request it.",
             "必要な時だけ GitHub で新しいバージョンを確認します。")
    }};
    std::array<std::string, 5> values{{
      gridView ? uiWord("그리드", "Grid", "グリッド") : uiWord("리스트", "List", "リスト"),
      launcherConfig.sortMode == "type" ? uiWord("종류순", "Type", "種類順")
                                        : uiWord("이름순", "Name", "名前順"),
      filterLabelAt(filterIndexFromCode(launcherConfig.engineFilter)),
      gameRoot.string(),
      std::string("v") + APP_VERSION
    }};

    const SDL_Rect categoryPanel{104, 104, 242, 516};
    const SDL_Rect detailPanel{366, 104, 832, 516};
    fillRoundedRect(renderer, categoryPanel, 22, SDL_Color{5, 20, 36, 176});
    strokeRoundedRect(renderer, categoryPanel, 22, SDL_Color{96, 158, 201, 52}, 1);
    fillRoundedRect(renderer, detailPanel, 22, SDL_Color{5, 20, 36, 176});
    strokeRoundedRect(renderer, detailPanel, 22,
                      uiShell.settingsDetailFocused ? SDL_Color{112, 205, 255, 145}
                                                    : SDL_Color{96, 158, 201, 52},
                      uiShell.settingsDetailFocused ? 2 : 1);

    drawText(renderer, fonts.small, uiWord("설정 항목", "SETTINGS", "設定項目"),
             categoryPanel.x + 22, categoryPanel.y + 20, SDL_Color{125, 164, 195, 255});

    const std::array<int, 5> itemY{{132, 218, 304, 390, 476}};
    for (int i = 0; i < 5; ++i) {
      const bool active = uiShell.settingsRow == i && !uiShell.sidebarFocused;
      const bool listFocus = active && !uiShell.settingsDetailFocused;
      SDL_Rect item{categoryPanel.x + 16, itemY[i], categoryPanel.w - 32, 68};
      if (active) {
        fillRoundedRect(renderer, SDL_Rect{item.x - 2, item.y - 2, item.w + 4, item.h + 4},
                        17, SDL_Color{73, 181, 255, 56});
        fillRoundedRect(renderer, item, 15,
                        listFocus ? SDL_Color{28, 88, 141, 208} : SDL_Color{19, 61, 98, 184});
        strokeRoundedRect(renderer, item, 15,
                          listFocus ? SDL_Color{124, 213, 255, 158} : SDL_Color{96, 166, 207, 82}, 1);
      } else {
        fillRoundedRect(renderer, item, 15, SDL_Color{10, 35, 56, 142});
      }
      drawText(renderer, fonts.medium, labels[static_cast<std::size_t>(i)],
               item.x + 18, item.y + 12, active ? WHITE : SDL_Color{202, 218, 232, 255});
      drawText(renderer, fonts.small,
               truncateText(fonts.small, values[static_cast<std::size_t>(i)], item.w - 36),
               item.x + 18, item.y + 41,
               active ? SDL_Color{174, 222, 249, 255} : SDL_Color{124, 153, 177, 255});
    }

    const int activeIndex = std::clamp(uiShell.settingsRow, 0, 4);
    drawText(renderer, fonts.normal, labels[static_cast<std::size_t>(activeIndex)],
             detailPanel.x + 30, detailPanel.y + 24, WHITE);
    drawText(renderer, fonts.small, descriptions[static_cast<std::size_t>(activeIndex)],
             detailPanel.x + 30, detailPanel.y + 67, SDL_Color{145, 174, 197, 255});

    const SDL_Rect content{detailPanel.x + 28, detailPanel.y + 120, detailPanel.w - 56, 286};
    fillRoundedRect(renderer, content, 18, SDL_Color{7, 29, 48, 148});
    strokeRoundedRect(renderer, content, 18, SDL_Color{95, 145, 179, 42}, 1);

    if (activeIndex == 0) {
      drawText(renderer, fonts.medium, uiWord("라이브러리 표시 방식", "Library layout", "ライブラリ表示形式"),
               content.x + content.w / 2, content.y + 28, WHITE, true);
      constexpr int optionW = 280;
      constexpr int optionGap = 44;
      const int optionStartX = content.x + (content.w - (optionW * 2 + optionGap)) / 2;
      const SDL_Rect listBtn{optionStartX, content.y + 104, optionW, 72};
      const SDL_Rect gridBtn{optionStartX + optionW + optionGap, content.y + 104, optionW, 72};
      const bool listOn = !gridView;
      const bool gridOn = gridView;
      fillRoundedRect(renderer, listBtn, 18, listOn ? SDL_Color{31, 102, 165, 220} : SDL_Color{11, 38, 61, 180});
      fillRoundedRect(renderer, gridBtn, 18, gridOn ? SDL_Color{31, 102, 165, 220} : SDL_Color{11, 38, 61, 180});
      strokeRoundedRect(renderer, listBtn, 18,
                        listOn ? SDL_Color{122, 214, 255, static_cast<Uint8>(uiShell.settingsDetailFocused ? 235 : 170)}
                               : SDL_Color{88, 132, 164, 58},
                        listOn && uiShell.settingsDetailFocused ? 2 : 1);
      strokeRoundedRect(renderer, gridBtn, 18,
                        gridOn ? SDL_Color{122, 214, 255, static_cast<Uint8>(uiShell.settingsDetailFocused ? 235 : 170)}
                               : SDL_Color{88, 132, 164, 58},
                        gridOn && uiShell.settingsDetailFocused ? 2 : 1);
      drawText(renderer, fonts.medium, uiWord("리스트", "List", "リスト"),
               listBtn.x + listBtn.w / 2, listBtn.y + 22, listOn ? WHITE : SDL_Color{182, 201, 218, 255}, true);
      drawText(renderer, fonts.medium, uiWord("그리드", "Grid", "グリッド"),
               gridBtn.x + gridBtn.w / 2, gridBtn.y + 22, gridOn ? WHITE : SDL_Color{182, 201, 218, 255}, true);
    } else if (activeIndex == 1) {
      drawText(renderer, fonts.medium, uiWord("기본 정렬", "Default sorting", "既定の並び順"),
               content.x + content.w / 2, content.y + 28, WHITE, true);
      constexpr int optionW = 280;
      constexpr int optionGap = 44;
      const int optionStartX = content.x + (content.w - (optionW * 2 + optionGap)) / 2;
      const SDL_Rect nameBtn{optionStartX, content.y + 104, optionW, 72};
      const SDL_Rect typeBtn{optionStartX + optionW + optionGap, content.y + 104, optionW, 72};
      const bool nameOn = launcherConfig.sortMode != "type";
      const bool typeOn = !nameOn;
      fillRoundedRect(renderer, nameBtn, 18, nameOn ? SDL_Color{31, 102, 165, 220} : SDL_Color{11, 38, 61, 180});
      fillRoundedRect(renderer, typeBtn, 18, typeOn ? SDL_Color{31, 102, 165, 220} : SDL_Color{11, 38, 61, 180});
      strokeRoundedRect(renderer, nameBtn, 18,
                        nameOn ? SDL_Color{122, 214, 255, static_cast<Uint8>(uiShell.settingsDetailFocused ? 235 : 170)}
                               : SDL_Color{88, 132, 164, 58},
                        nameOn && uiShell.settingsDetailFocused ? 2 : 1);
      strokeRoundedRect(renderer, typeBtn, 18,
                        typeOn ? SDL_Color{122, 214, 255, static_cast<Uint8>(uiShell.settingsDetailFocused ? 235 : 170)}
                               : SDL_Color{88, 132, 164, 58},
                        typeOn && uiShell.settingsDetailFocused ? 2 : 1);
      drawText(renderer, fonts.medium, uiWord("이름순", "Name", "名前順"),
               nameBtn.x + nameBtn.w / 2, nameBtn.y + 22, nameOn ? WHITE : SDL_Color{182, 201, 218, 255}, true);
      drawText(renderer, fonts.medium, uiWord("종류순", "Type", "種類順"),
               typeBtn.x + typeBtn.w / 2, typeBtn.y + 22, typeOn ? WHITE : SDL_Color{182, 201, 218, 255}, true);
    } else if (activeIndex == 2) {
      drawText(renderer, fonts.medium, uiWord("기본 엔진 필터", "Default engine filter", "既定エンジンフィルター"),
               content.x + content.w / 2, content.y + 28, WHITE, true);
      const int chipW = 150;
      const int chipH = 48;
      const int gapX = 18;
      const int gapY = 18;
      const int chipGroupW = chipW * 4 + gapX * 3;
      const int chipStartX = content.x + (content.w - chipGroupW) / 2;
      for (int i = 0; i < 8; ++i) {
        const int col = i % 4;
        const int row = i / 4;
        SDL_Rect chip{chipStartX + col * (chipW + gapX),
                      content.y + 92 + row * (chipH + gapY), chipW, chipH};
        const bool chosen = launcherConfig.engineFilter == filterCodeAt(i);
        fillRoundedRect(renderer, chip, 16, chosen ? SDL_Color{31, 102, 165, 222} : SDL_Color{11, 38, 61, 178});
        strokeRoundedRect(renderer, chip, 16,
                          chosen ? SDL_Color{122, 214, 255, static_cast<Uint8>(uiShell.settingsDetailFocused ? 235 : 168)}
                                 : SDL_Color{88, 132, 164, 50},
                          chosen && uiShell.settingsDetailFocused ? 2 : 1);
        drawText(renderer, fonts.small, filterLabelAt(i), chip.x + chip.w / 2, chip.y + 13,
                 chosen ? WHITE : SDL_Color{180, 201, 218, 255}, true);
      }
    } else if (activeIndex == 3) {
      drawText(renderer, fonts.medium, uiWord("게임 검색 폴더", "Game folder", "ゲーム検索フォルダー"),
               content.x + 26, content.y + 28, WHITE);
      SDL_Rect pathBox{content.x + 28, content.y + 95, content.w - 56, 64};
      fillRoundedRect(renderer, pathBox, 16, SDL_Color{9, 31, 51, 194});
      strokeRoundedRect(renderer, pathBox, 16,
                        uiShell.settingsDetailFocused ? SDL_Color{122, 214, 255, 210}
                                                      : SDL_Color{99, 154, 190, 60},
                        uiShell.settingsDetailFocused ? 2 : 1);
      drawText(renderer, fonts.medium, truncateText(fonts.medium, gameRoot.string(), pathBox.w - 36),
               pathBox.x + 18, pathBox.y + 18, SDL_Color{215, 229, 240, 255});
      drawText(renderer, fonts.small,
               uiWord("A 버튼 또는 오른쪽 입력으로 폴더 선택기를 엽니다",
                      "Press A or Right to choose another folder",
                      "A または右入力でフォルダーを変更します"),
               content.x + 28, content.y + 190, SDL_Color{137, 166, 189, 255});
    } else {
      drawText(renderer, fonts.medium, uiWord("RPG Maker Player 업데이트", "RPG Maker Player Update", "RPG Maker Player アップデート"),
               content.x + content.w / 2, content.y + 24, WHITE, true);
      drawText(renderer, fonts.small,
               uiWord("현재 버전", "Current version", "現在のバージョン") + std::string(": v") + APP_VERSION,
               content.x + 34, content.y + 76, SDL_Color{186, 210, 229, 255});
      if (updateUi.checked && !updateUi.latestVersion.empty()) {
        drawText(renderer, fonts.small,
                 uiWord("확인된 최신 버전", "Latest version", "確認した最新バージョン") +
                   std::string(": v") + updateUi.latestVersion,
                 content.x + 34, content.y + 108, SDL_Color{186, 210, 229, 255});
      }
      SDL_Rect checkBtn{content.x + 34, content.y + 156, 250, 58};
      fillRoundedRect(renderer, checkBtn, 16, SDL_Color{31, 102, 165, 220});
      strokeRoundedRect(renderer, checkBtn, 16,
                        uiShell.settingsDetailFocused ? SDL_Color{122, 214, 255, 230}
                                                      : SDL_Color{88, 132, 164, 70},
                        uiShell.settingsDetailFocused ? 2 : 1);
      drawText(renderer, fonts.medium, uiWord("업데이트 확인", "Check for updates", "アップデート確認"),
               checkBtn.x + checkBtn.w / 2, checkBtn.y + 17, WHITE, true);
      if (!updateUi.message.empty()) {
        drawText(renderer, fonts.small, truncateText(fonts.small, updateUi.message, content.w - 360),
                 content.x + 320, content.y + 176, SDL_Color{152, 189, 215, 255});
      }
      if (updateUi.prompt) {
        drawText(renderer, fonts.medium,
                 uiWord("다운로드 후 설치하시겠습니까?", "Download and install this update?", "ダウンロードしてインストールしますか？"),
                 content.x + content.w / 2, content.y + 232, WHITE, true);
        SDL_Rect yesBtn{content.x + content.w / 2 - 170, content.y + 260, 150, 46};
        SDL_Rect noBtn{content.x + content.w / 2 + 20, content.y + 260, 150, 46};
        fillRoundedRect(renderer, yesBtn, 14, updateUi.promptYes ? SDL_Color{31, 102, 165, 230} : SDL_Color{11, 38, 61, 180});
        fillRoundedRect(renderer, noBtn, 14, !updateUi.promptYes ? SDL_Color{31, 102, 165, 230} : SDL_Color{11, 38, 61, 180});
        drawText(renderer, fonts.medium, uiWord("예", "Yes", "はい"), yesBtn.x + yesBtn.w / 2, yesBtn.y + 12, WHITE, true);
        drawText(renderer, fonts.medium, uiWord("아니오", "No", "いいえ"), noBtn.x + noBtn.w / 2, noBtn.y + 12, WHITE, true);
      }
    }

    const std::string settingsHelp = uiShell.settingsDetailFocused
      ? (activeIndex == 3
          ? uiWord("A  폴더 선택     B  목록으로", "A Choose folder     B Back to list", "A フォルダー選択     B 一覧へ")
          : activeIndex == 4
          ? uiWord("A  업데이트 확인     B  목록으로", "A Check for updates     B Back to list", "A アップデート確認     B 一覧へ")
          : uiWord("← →  값 변경     B  목록으로", "← → Change value     B Back to list", "← → 値を変更     B 一覧へ"))
      : uiWord("A  세부 설정     ↑↓  항목 이동", "A Open details     ↑↓ Move", "A 詳細設定     ↑↓ 移動");
    drawText(renderer, fonts.small, settingsHelp,
             detailPanel.x + 30, detailPanel.y + detailPanel.h - 42, SDL_Color{129, 160, 185, 255});
  };
  auto drawBottomHints = [&]() {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (!status.empty()) {
      const SDL_Rect statusBar{94, 672, 430, 34};
      fillRoundedRect(renderer, statusBar, 17, SDL_Color{5, 20, 34, 150});
      const std::string shown = truncateText(fonts.small, status, 390);
      drawText(renderer, fonts.small, shown, 112, centeredTextY(fonts.small, statusBar, shown),
               SDL_Color{135, 160, 181, 255});
    }

    struct HintItem {
      const char* button;
      SDL_Color color;
      std::string label;
    };
    std::vector<HintItem> hints;
    hints.push_back({"A", UI_GREEN, uiWord("선택", "Select", "選択")});
    hints.push_back({"B", UI_RED,
                     uiShell.section == MainSection::Home && !uiShell.sidebarFocused
                       ? uiWord("메뉴", "Menu", "メニュー")
                       : uiWord("뒤로", "Back", "戻る")});
    if (uiShell.section == MainSection::Library)
      hints.push_back({"X", UI_BLUE, uiWord("보기", "View", "表示")});
    hints.push_back({"Y", UI_YELLOW, uiWord("검색", "Search", "検索")});

    const SDL_Rect hintBar{716, 666, 544, 44};
    fillRoundedRect(renderer, hintBar, 20, SDL_Color{4, 18, 31, 195});
    strokeRoundedRect(renderer, hintBar, 20, SDL_Color{97, 147, 181, 38}, 1);

    constexpr int groupGap = 30;
    int totalW = 0;
    for (const auto& hint : hints) totalW += 35 + textWidth(fonts.small, hint.label);
    if (hints.size() > 1) totalW += groupGap * static_cast<int>(hints.size() - 1);
    int cursorX = hintBar.x + (hintBar.w - totalW) / 2;
    const int centerY = hintBar.y + hintBar.h / 2;
    for (const auto& hint : hints) {
      const int circleX = cursorX + 13;
      drawPadHint(renderer, fonts, circleX, centerY, hint.button, hint.color, hint.label);
      cursorX += 35 + textWidth(fonts.small, hint.label) + groupGap;
    }
  };
  Uint64 nextIdleRedrawAt = 0;
  log << "power policy | idleRedrawMs=750 idlePollMs=50 activePollMs=8" << '\n';
  log.flush();
  while (running) {
    const Uint64 loopStartedAt = SDL_GetTicks64();
    bool sawUiEvent = false;
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
      sawUiEvent = true;
      if (event.type == SDL_QUIT) running = false;
      else if (event.type == SDL_CONTROLLERDEVICEADDED && !controller) controller = openFirstController();
      else if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller) {
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event.cdevice.which) {
          SDL_GameControllerClose(controller);
          controller = openFirstController();
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
                 event.button.which != SDL_TOUCH_MOUSEID) {
        float logicalX = 0.0f;
        float logicalY = 0.0f;
        SDL_RenderWindowToLogical(renderer, event.button.x, event.button.y, &logicalX, &logicalY);
        logicalY -= static_cast<float>(displayLayout.uiOffsetY);
        if (logicalY >= 0.0f && logicalY < static_cast<float>(HEIGHT))
          handlePointerDown(logicalX, logicalY);
      } else if (event.type == SDL_FINGERDOWN) {
        int windowW = displayLayout.logicalWidth;
        int windowH = displayLayout.logicalHeight;
        SDL_GetWindowSize(window, &windowW, &windowH);
        float logicalX = 0.0f;
        float logicalY = 0.0f;
        SDL_RenderWindowToLogical(renderer,
                                  static_cast<int>(event.tfinger.x * windowW),
                                  static_cast<int>(event.tfinger.y * windowH),
                                  &logicalX, &logicalY);
        logicalY -= static_cast<float>(displayLayout.uiOffsetY);
        if (logicalY >= 0.0f && logicalY < static_cast<float>(HEIGHT))
          handlePointerDown(logicalX, logicalY);
      } else if (keyRemap.active && event.type == SDL_CONTROLLERBUTTONDOWN) {
        const int button = event.cbutton.button;
        if (keyRemap.stage == 0) {
          if (button == SDL_CONTROLLER_BUTTON_A) {
            keyRemap.stage = 1;
            keyRemap.source = -1;
            keyRemap.message.clear();
          } else if (button == SDL_CONTROLLER_BUTTON_X) {
            resetKeyRemaps();
          } else if (button == SDL_CONTROLLER_BUTTON_B) {
            closeKeyRemap();
          }
        } else if (button == SDL_CONTROLLER_BUTTON_START) {
          keyRemap.stage = 0;
          keyRemap.source = -1;
          keyRemap.message.clear();
        } else if (remappableControllerButton(button)) {
          if (keyRemap.stage == 1) {
            keyRemap.source = button;
            keyRemap.stage = 2;
          } else if (keyRemap.stage == 2) {
            saveCapturedKeyRemap(button);
          }
        }
      } else if (keyRemap.active && event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        if (keyRemap.stage == 0 && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_z)) {
          keyRemap.stage = 1;
          keyRemap.source = -1;
          keyRemap.message.clear();
        } else if (keyRemap.stage == 0 && event.key.keysym.sym == SDLK_DELETE) {
          resetKeyRemaps();
        } else if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_BACKSPACE) {
          if (keyRemap.stage == 0) closeKeyRemap();
          else {
            keyRemap.stage = 0;
            keyRemap.source = -1;
          }
        }
      } else if (search.active && event.type == SDL_TEXTINPUT) {
        const std::string incoming = event.text.text;
        if (search.text.size() + incoming.size() <= 384) {
          search.text += incoming;
          refreshSearch();
        }
      } else if (search.active && event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        switch (event.key.keysym.sym) {
          case SDLK_BACKSPACE: eraseLastUtf8Codepoint(search.text); refreshSearch(); break;
          case SDLK_DELETE: clearSearch(); break;
          case SDLK_RETURN:
          case SDLK_KP_ENTER:
          case SDLK_ESCAPE: closeSearch(); break;
          default: break;
        }
      } else if (search.active && event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_X: clearSearch(); break;
          case SDL_CONTROLLER_BUTTON_A:
          case SDL_CONTROLLER_BUTTON_B:
          case SDL_CONTROLLER_BUTTON_Y: closeSearch(); break;
          default: break;
        }
      } else if (search.active && event.type == SDL_CONTROLLERBUTTONUP) {
        // Search modal owns controller input while text input is active.
      } else if (filterSort.active && event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        switch (event.key.keysym.sym) {
          case SDLK_UP: moveFilterSortRow(-1); break;
          case SDLK_DOWN: moveFilterSortRow(1); break;
          case SDLK_LEFT: moveFilterSortChoice(-1); break;
          case SDLK_RIGHT: moveFilterSortChoice(1); break;
          case SDLK_RETURN:
          case SDLK_z: activateFilterSort(); break;
          case SDLK_ESCAPE:
          case SDLK_BACKSPACE: closeFilterSort(); break;
          default: break;
        }
      } else if (filterSort.active && event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_DPAD_UP: moveFilterSortRow(-1); break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN: moveFilterSortRow(1); break;
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT: moveFilterSortChoice(-1); break;
          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: moveFilterSortChoice(1); break;
          case SDL_CONTROLLER_BUTTON_A: activateFilterSort(); break;
          case SDL_CONTROLLER_BUTTON_B: closeFilterSort(); break;
          default: break;
        }
      } else if (filterSort.active && event.type == SDL_CONTROLLERBUTTONUP) {
        // Filter/sort modal consumes controller input until it is closed.
      } else if (gameSettings.editingName && event.type == SDL_TEXTINPUT) {
        const std::string incoming = event.text.text;
        if (gameSettings.editText.size() + incoming.size() <= 384) gameSettings.editText += incoming;
      } else if (gameSettings.editingName && event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        switch (event.key.keysym.sym) {
          case SDLK_RETURN:
          case SDLK_KP_ENTER: saveEditedName(); break;
          case SDLK_ESCAPE: cancelNameEditing(); break;
          case SDLK_BACKSPACE: eraseLastUtf8Codepoint(gameSettings.editText); break;
          default: break;
        }
      } else if (gameSettings.editingName && event.type == SDL_CONTROLLERBUTTONDOWN) {
        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) saveEditedName();
        else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) cancelNameEditing();
      } else if (gameSettings.editingName && event.type == SDL_CONTROLLERBUTTONUP) {
        // Name editor owns controller input while text input is active.
      } else if (gameSettings.active && event.type == SDL_MOUSEWHEEL) {
        if (event.wheel.y > 0) scrollSettingsContent(-1);
        if (event.wheel.y < 0) scrollSettingsContent(1);
      } else if (gameSettings.active && event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        switch (event.key.keysym.sym) {
          case SDLK_UP: moveSettingsRow(-1); break;
          case SDLK_DOWN: moveSettingsRow(1); break;
          case SDLK_LEFT: adjustGameSettingsValue(-1); break;
          case SDLK_RIGHT: adjustGameSettingsValue(1); break;
          case SDLK_RETURN:
          case SDLK_z: activateSettingsRow(); break;
          case SDLK_ESCAPE:
          case SDLK_BACKSPACE: closeGameSettings(); break;
          default: break;
        }
      } else if (gameSettings.active && event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_DPAD_UP: moveSettingsRow(-1); break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN: moveSettingsRow(1); break;
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT: adjustGameSettingsValue(-1); break;
          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: adjustGameSettingsValue(1); break;
          case SDL_CONTROLLER_BUTTON_A: activateSettingsRow(); break;
          case SDL_CONTROLLER_BUTTON_B: closeGameSettings(); break;
          default: break;
        }
      } else if (gameSettings.active && event.type == SDL_CONTROLLERBUTTONUP) {
        // Settings modal consumes controller input until it is closed.
      } else if (folderPicker.active && event.type == SDL_MOUSEWHEEL) {
        if (event.wheel.y > 0) moveFolderSelection(-1);
        if (event.wheel.y < 0) moveFolderSelection(1);
      } else if (folderPicker.active && event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        switch (event.key.keysym.sym) {
          case SDLK_UP: moveFolderSelection(-1); break;
          case SDLK_DOWN: moveFolderSelection(1); break;
          case SDLK_RETURN:
          case SDLK_z: folderPickerEnter(folderPicker); break;
          case SDLK_BACKSPACE:
          case SDLK_b: folderPickerParent(folderPicker); break;
          case SDLK_x:
          case SDLK_SPACE: chooseCurrentFolder(); break;
          case SDLK_ESCAPE:
          case SDLK_y: cancelFolderDialog(); break;
          default: break;
        }
      } else if (folderPicker.active && event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_DPAD_UP: moveFolderSelection(-1); break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN: moveFolderSelection(1); break;
          case SDL_CONTROLLER_BUTTON_A: folderPickerEnter(folderPicker); break;
          case SDL_CONTROLLER_BUTTON_B: folderPickerParent(folderPicker); break;
          case SDL_CONTROLLER_BUTTON_X: chooseCurrentFolder(); break;
          case SDL_CONTROLLER_BUTTON_Y: cancelFolderDialog(); break;
          default: break;
        }
      } else if (folderPicker.active && event.type == SDL_CONTROLLERBUTTONUP) {
        // Folder picker consumes controller input until it is closed.
      } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        switch (event.key.keysym.sym) {
          case SDLK_UP: moveByDirection(0, -1); break;
          case SDLK_DOWN: moveByDirection(0, 1); break;
          case SDLK_LEFT: moveByDirection(-1, 0); break;
          case SDLK_RIGHT: moveByDirection(1, 0); break;
          case SDLK_RETURN:
          case SDLK_z: confirm(); break;
          case SDLK_TAB:
            if (!exitModal && !uiShell.sidebarFocused && uiShell.section != MainSection::Settings) openGameSettings();
            break;
          case SDLK_ESCAPE:
            if (updateUi.prompt) {
              updateUi.prompt = false;
            } else if (uiShell.section == MainSection::Settings && uiShell.settingsDetailFocused) {
              uiShell.settingsDetailFocused = false;
            } else {
              exitModal = !exitModal;
              exitYes = false;
            }
            break;
          case SDLK_x:
            if (exitModal) exitModal = false;
            else if (uiShell.section == MainSection::Library) toggleViewMode();
            break;
          case SDLK_v: if (!exitModal && uiShell.section == MainSection::Library) toggleViewMode(); break;
          case SDLK_F2: if (!exitModal) openGameFolderDialog(); break;
          case SDLK_F4: if (!exitModal) openSearch(); break;
          case SDLK_PAGEUP: if (!exitModal && uiShell.section == MainSection::Library) movePage(-1); break;
          case SDLK_PAGEDOWN: if (!exitModal && uiShell.section == MainSection::Library) movePage(1); break;
          case SDLK_BACKSPACE: if (exitModal) exitModal = false; break;
          default: break;
        }
      } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_DPAD_UP: moveByDirection(0, -1); break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN: moveByDirection(0, 1); break;
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT: moveByDirection(-1, 0); break;
          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: moveByDirection(1, 0); break;
          case SDL_CONTROLLER_BUTTON_A: confirm(); break;
          case SDL_CONTROLLER_BUTTON_B:
            if (updateUi.prompt) updateUi.prompt = false;
            else if (exitModal) exitModal = false;
            else if (uiShell.sidebarFocused) {
              uiShell.sidebarFocused = false;
              uiSounds.play(UiSoundKind::SidebarOut);
            }
            else if (uiShell.section == MainSection::Settings && uiShell.settingsDetailFocused) {
              uiShell.settingsDetailFocused = false;
            } else if (uiShell.section != MainSection::Home) {
              uiShell.section = MainSection::Home;
              uiShell.sidebarIndex = 0;
              uiShell.settingsDetailFocused = false;
              uiShell.homeRow = homeRecentIndices().empty() ? 1 : 0;
              syncHomeSelected();
            } else {
              uiShell.sidebarIndex = 0;
              uiShell.sidebarFocused = true;
              uiSounds.play(UiSoundKind::SidebarIn);
            }
            break;
          case SDL_CONTROLLER_BUTTON_X:
            if (!exitModal && uiShell.section == MainSection::Library) toggleViewMode();
            break;
          case SDL_CONTROLLER_BUTTON_Y: if (!exitModal) openSearch(); break;
          case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            if (!exitModal && !uiShell.sidebarFocused && uiShell.section == MainSection::Home) moveHomePage(-1);
            else if (!exitModal && !uiShell.sidebarFocused && uiShell.section == MainSection::Library) movePage(-1);
            break;
          case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            if (!exitModal && !uiShell.sidebarFocused && uiShell.section == MainSection::Home) moveHomePage(1);
            else if (!exitModal && !uiShell.sidebarFocused && uiShell.section == MainSection::Library) movePage(1);
            break;
          case SDL_CONTROLLER_BUTTON_BACK:
            selectHeld = true;
            selectTapCandidate = !startHeld;
            if (startHeld) startTapCandidate = false;
            break;
          case SDL_CONTROLLER_BUTTON_START:
            startHeld = true;
            startTapCandidate = !selectHeld;
            if (selectHeld) selectTapCandidate = false;
            break;
          default: break;
        }
      } else if (event.type == SDL_CONTROLLERBUTTONUP) {
        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
          if (selectTapCandidate && !startHeld && !exitModal &&
              !uiShell.sidebarFocused && uiShell.section != MainSection::Settings) {
            openGameSettings();
          }
          selectHeld = false;
          selectTapCandidate = false;
          comboLatched = false;
        } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
          startHeld = false;
          startTapCandidate = false;
          comboLatched = false;
        }
      }
    }

    const Uint64 now = SDL_GetTicks64();
    if (!folderPicker.active && !gameSettings.active && !filterSort.active && !search.active && !exitModal && startHeld && selectHeld) {
      selectTapCandidate = false;
      startTapCandidate = false;
      if (comboStarted == 0) comboStarted = now;
      if (!comboLatched && now - comboStarted >= EXIT_COMBO_HOLD_MS) {
        comboLatched = true;
        exitModal = true;
        exitYes = false;
      }
    } else if (!startHeld || !selectHeld) {
      comboStarted = 0;
    }

    if (controller && SDL_GameControllerGetAttached(controller)) {
      SDL_GameControllerUpdate();
      const Sint16 rawX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
      const Sint16 rawY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
      const Sint16 rawL2 = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
      const Sint16 rawR2 = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
      constexpr Sint16 ANALOG_PRESS = 16000;
      constexpr Sint16 ANALOG_RELEASE = 8000;
      constexpr Sint16 TRIGGER_PRESS = 16000;
      constexpr Sint16 TRIGGER_RELEASE = 8000;
      constexpr Uint64 ANALOG_FIRST_REPEAT = 300;
      constexpr Uint64 ANALOG_REPEAT = 110;

      auto axisDir = [&](Sint16 value, int current) {
        if (current < 0 && value <= -ANALOG_RELEASE) return -1;
        if (current > 0 && value >= ANALOG_RELEASE) return 1;
        if (value <= -ANALOG_PRESS) return -1;
        if (value >= ANALOG_PRESS) return 1;
        return 0;
      };

      auto applyAnalog = [&](int nextDir, int& currentDir, Uint64& nextRepeat, bool horizontal) {
        if (nextDir == 0) {
          currentDir = 0;
          nextRepeat = 0;
          return;
        }
        if (nextDir != currentDir) {
          currentDir = nextDir;
          nextRepeat = now + ANALOG_FIRST_REPEAT;
          moveByDirection(horizontal ? nextDir : 0, horizontal ? 0 : nextDir);
          return;
        }
        if (nextRepeat != 0 && now >= nextRepeat) {
          nextRepeat = now + ANALOG_REPEAT;
          moveByDirection(horizontal ? nextDir : 0, horizontal ? 0 : nextDir);
        }
      };

      applyAnalog(axisDir(rawX, analogXDir), analogXDir, analogXNext, true);
      applyAnalog(axisDir(rawY, analogYDir), analogYDir, analogYNext, false);

      if (!triggerL2Held && rawL2 >= TRIGGER_PRESS) {
        triggerL2Held = true;
        quickCycleEngineFilter(-1);
      } else if (triggerL2Held && rawL2 <= TRIGGER_RELEASE) {
        triggerL2Held = false;
      }
      if (!triggerR2Held && rawR2 >= TRIGGER_PRESS) {
        triggerR2Held = true;
        quickCycleEngineFilter(1);
      } else if (triggerR2Held && rawR2 <= TRIGGER_RELEASE) {
        triggerR2Held = false;
      }
    } else {
      analogXDir = 0;
      analogYDir = 0;
      analogXNext = 0;
      analogYNext = 0;
      triggerL2Held = false;
      triggerR2Held = false;
    }

    const float sidebarTarget = uiShell.sidebarFocused ? 1.0f : 0.0f;
    const bool sidebarAnimating = std::abs(sidebarTarget - uiShell.sidebarAnim) >= 0.01f;
    const bool timedUiActive = uiShell.sectionTransitionActive || uiShell.libraryPageTransitionActive || sidebarAnimating ||
                               startHeld || selectHeld || analogXDir != 0 || analogYDir != 0 ||
                               triggerL2Held || triggerR2Held;
    const Uint64 redrawNow = SDL_GetTicks64();
    const bool periodicIdleRedraw = redrawNow >= nextIdleRedrawAt;
    const bool renderFrame = sawUiEvent || timedUiActive || periodicIdleRedraw;
    if (renderFrame) {
    uiShell.sidebarAnim += (sidebarTarget - uiShell.sidebarAnim) * 0.18f;
    if (std::abs(sidebarTarget - uiShell.sidebarAnim) < 0.01f) uiShell.sidebarAnim = sidebarTarget;

    SDL_RenderSetViewport(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, UI_NAVY.r, UI_NAVY.g, UI_NAVY.b, UI_NAVY.a);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Texture* uiBackground = thumbs.get(root / "assets/ui/launcher_bg_v048.png");
    if (uiBackground) {
      drawCover(renderer, uiBackground, SDL_Rect{0, 0, displayLayout.logicalWidth, displayLayout.logicalHeight});
      fillRect(renderer, SDL_Rect{0, 0, displayLayout.logicalWidth, displayLayout.logicalHeight}, SDL_Color{3, 16, 29, 88});
      fillRect(renderer, SDL_Rect{82, 0, displayLayout.logicalWidth - 82, displayLayout.logicalHeight}, SDL_Color{4, 19, 33, 64});
    } else {
      fillRect(renderer, SDL_Rect{0, 0, displayLayout.logicalWidth, displayLayout.logicalHeight}, SDL_Color{8, 27, 47, 255});
    }

    SDL_Rect uiViewport{0, displayLayout.uiOffsetY, WIDTH, HEIGHT};
    if (uiShell.sectionTransitionActive &&
        uiShell.sectionTransitionFrom == MainSection::Home &&
        uiShell.sectionTransitionTo == MainSection::Library) {
      constexpr float SECTION_TRANSITION_MS = 260.0f;
      const float raw = std::clamp(
        static_cast<float>(now - uiShell.sectionTransitionStartedAt) / SECTION_TRANSITION_MS,
        0.0f, 1.0f);
      const float inv = 1.0f - raw;
      const float eased = 1.0f - inv * inv * inv;
      const int slideY = static_cast<int>(std::lround(eased * static_cast<float>(HEIGHT)));

      SDL_Rect homeViewport{0, displayLayout.uiOffsetY - slideY, WIDTH, HEIGHT};
      SDL_RenderSetViewport(renderer, &homeViewport);
      drawHome();

      SDL_Rect libraryViewport{0, displayLayout.uiOffsetY + HEIGHT - slideY, WIDTH, HEIGHT};
      SDL_RenderSetViewport(renderer, &libraryViewport);
      drawLibrary();

      if (raw >= 1.0f) uiShell.sectionTransitionActive = false;
    } else if (uiShell.libraryPageTransitionActive && uiShell.section == MainSection::Library) {
      constexpr float LIBRARY_PAGE_TRANSITION_MS = 260.0f;
      const float raw = std::clamp(
        static_cast<float>(now - uiShell.libraryPageTransitionStartedAt) / LIBRARY_PAGE_TRANSITION_MS,
        0.0f, 1.0f);
      const float inv = 1.0f - raw;
      const float eased = 1.0f - inv * inv * inv;
      const int slideY = static_cast<int>(std::lround(eased * static_cast<float>(HEIGHT)));
      const int direction = uiShell.libraryPageTransitionDirection;
      const std::size_t liveSelected = selected;
      const int liveScroll = scroll;

      SDL_RenderSetViewport(renderer, &uiViewport);
      drawLibraryHeader();
      const SDL_Rect libraryContentClip{92, 118, WIDTH - 92, 532};
      SDL_RenderSetClipRect(renderer, &libraryContentClip);

      selected = uiShell.libraryPageTransitionFromSelected;
      scroll = uiShell.libraryPageTransitionFromScroll;
      drawLibraryContent(-direction * slideY);

      selected = uiShell.libraryPageTransitionToSelected;
      scroll = uiShell.libraryPageTransitionToScroll;
      drawLibraryContent(direction * (HEIGHT - slideY));

      selected = liveSelected;
      scroll = liveScroll;
      SDL_RenderSetClipRect(renderer, nullptr);
      if (raw >= 1.0f) uiShell.libraryPageTransitionActive = false;
    } else {
      uiShell.sectionTransitionActive = false;
      SDL_RenderSetViewport(renderer, &uiViewport);
      if (uiShell.section == MainSection::Home) drawHome();
      else if (uiShell.section == MainSection::Library) drawLibrary();
      else drawSettingsScreen();
    }

    SDL_RenderSetViewport(renderer, &uiViewport);
    drawSidebar();
    drawBottomHints();

    if (exitModal) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      fillRect(renderer, SDL_Rect{0, 0, WIDTH, HEIGHT}, SDL_Color{0, 0, 0, 174});
      fillRoundedRect(renderer, SDL_Rect{360, 220, 560, 270}, 20, SDL_Color{7, 24, 41, 246});
      strokeRoundedRect(renderer, SDL_Rect{360, 220, 560, 270}, 20, SDL_Color{102, 184, 230, 110}, 1);
      drawText(renderer, fonts.normal, tr(UiKey::ExitQuestion), WIDTH / 2, 276, WHITE, true);
      const SDL_Rect yes{450, 345, 170, 62};
      const SDL_Rect no{660, 345, 170, 62};
      fillRoundedRect(renderer, yes, 14, exitYes ? SELECT_BG : SDL_Color{40, 53, 69, 230});
      fillRoundedRect(renderer, no, 14, !exitYes ? SELECT_BG : SDL_Color{40, 53, 69, 230});
      drawText(renderer, fonts.medium, tr(UiKey::Yes), yes.x + yes.w / 2, yes.y + 18, exitYes ? SELECT_TEXT : WHITE, true);
      drawText(renderer, fonts.medium, tr(UiKey::No), no.x + no.w / 2, no.y + 18, !exitYes ? SELECT_TEXT : WHITE, true);
      drawText(renderer, fonts.small, tr(UiKey::ExitHelp), WIDTH / 2, 438, SDL_Color{157, 168, 182, 255}, true);
    }

    if (folderPicker.active) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      fillRect(renderer, SDL_Rect{0, 0, WIDTH, HEIGHT}, SDL_Color{0, 0, 0, 188});
      const SDL_Rect panel{180, 72, 920, 588};
      fillRoundedRect(renderer, panel, 20, SDL_Color{7, 24, 41, 242});
      strokeRoundedRect(renderer, panel, 20, SDL_Color{102, 184, 230, 105}, 1);
      drawText(renderer, fonts.normal, tr(UiKey::FolderPickerTitle), WIDTH / 2, 104, WHITE, true);
      drawText(renderer, fonts.small, tr(UiKey::FolderPickerCurrent), 220, 151, MUTED2);
      drawText(renderer, fonts.small,
               truncateText(fonts.small, folderPicker.current.string(), 680),
               1038, 151, WHITE, false, true);

      const int rowX = 230;
      const int rowY = 198;
      const int rowW = 820;
      const int rowH = 39;
      const int visible = 9;
      if (folderPicker.children.empty()) {
        drawText(renderer, fonts.small, tr(UiKey::FolderPickerEmpty), WIDTH / 2, 260, MUTED, true);
      } else {
        for (int slot = 0; slot < visible; ++slot) {
          const int index = folderPicker.scroll + slot;
          if (index >= static_cast<int>(folderPicker.children.size())) break;
          const bool active = static_cast<std::size_t>(index) == folderPicker.selected;
          const SDL_Rect row{rowX, rowY + slot * rowH, rowW, rowH - 2};
          if (active) fillRoundedRect(renderer, row, 10, SELECT_BG);
          const std::string label = "▸  " + folderPicker.children[index].filename().string();
          drawText(renderer, fonts.medium, truncateText(fonts.medium, label, rowW - 28),
                   row.x + 14, row.y + 7, active ? SELECT_TEXT : WHITE);
        }
      }

      const SDL_Rect useButton{370, 574, 250, 52};
      const SDL_Rect upButton{640, 574, 190, 52};
      const SDL_Rect cancelButton{850, 574, 150, 52};
      fillRoundedRect(renderer, useButton, 14, SELECT_BG);
      fillRoundedRect(renderer, upButton, 14, CARD_INNER);
      strokeRoundedRect(renderer, useButton, 14, SDL_Color{140, 218, 255, 180}, 1);
      strokeRoundedRect(renderer, upButton, 14, BORDER, 1);
      drawText(renderer, fonts.small, std::string("X  ") + tr(UiKey::FolderPickerUse),
               useButton.x + useButton.w / 2, useButton.y + 15, SELECT_TEXT, true);
      drawText(renderer, fonts.small, std::string("B  ") + tr(UiKey::FolderPickerUp),
               upButton.x + upButton.w / 2, upButton.y + 15, WHITE, true);
      if (!folderPicker.mandatory) {
        fillRoundedRect(renderer, cancelButton, 14, CARD_INNER);
        strokeRoundedRect(renderer, cancelButton, 14, BORDER, 1);
        drawText(renderer, fonts.small, std::string("Y  ") + tr(UiKey::FolderPickerCancel),
                 cancelButton.x + cancelButton.w / 2, cancelButton.y + 15, WHITE, true);
      }
      drawText(renderer, fonts.small, tr(UiKey::FolderPickerHelp), WIDTH / 2, 635, MUTED2, true);
    }

    if (filterSort.active) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      fillRect(renderer, SDL_Rect{0, 0, WIDTH, HEIGHT}, SDL_Color{0, 0, 0, 188});
      const SDL_Rect panel{210, 90, 860, 540};
      fillRoundedRect(renderer, panel, 20, SDL_Color{7, 24, 41, 242});
      strokeRoundedRect(renderer, panel, 20, SDL_Color{102, 184, 230, 105}, 1);
      drawText(renderer, fonts.normal, tr(UiKey::FilterSortTitle), WIDTH / 2, 118, WHITE, true);

      drawText(renderer, fonts.small, tr(UiKey::ShowGames), 280, 175, MUTED2);
      const int filterX = 286;
      const int filterY = 200;
      const int filterW = 82;
      const int filterGap = 6;
      for (int i = 0; i < 8; ++i) {
        const SDL_Rect chip{filterX + i * (filterW + filterGap), filterY, filterW, 48};
        const bool chosen = filterSort.filterIndex == i;
        const bool focused = filterSort.row == 0 && chosen;
        fillRoundedRect(renderer, chip, 14, chosen ? SELECT_BG : CARD_INNER);
        strokeRoundedRect(renderer, chip, 14, focused ? CURSOR_BORDER : (chosen ? SDL_Color{140, 218, 255, 160} : BORDER), focused ? 2 : 1);
        drawText(renderer, fonts.small, filterLabelAt(i), chip.x + chip.w / 2, chip.y + 13,
                 chosen ? SELECT_TEXT : WHITE, true);
        if (focused) drawText(renderer, fonts.small, "▶", chip.x - 10, chip.y + 13, CURSOR_BORDER, false, true);
      }

      drawText(renderer, fonts.small, tr(UiKey::SortMethod), 280, 285, MUTED2);
      const SDL_Rect typeSort{420, 310, 200, 52};
      const SDL_Rect nameSort{660, 310, 200, 52};
      const SDL_Rect sortRects[] = {typeSort, nameSort};
      const UiKey sortKeys[] = {UiKey::SortByType, UiKey::SortByName};
      for (int i = 0; i < 2; ++i) {
        const bool chosen = filterSort.sortIndex == i;
        const bool focused = filterSort.row == 1 && chosen;
        fillRoundedRect(renderer, sortRects[i], 14, chosen ? SELECT_BG : CARD_INNER);
        strokeRoundedRect(renderer, sortRects[i], 14, focused ? CURSOR_BORDER : (chosen ? SDL_Color{140, 218, 255, 160} : BORDER), focused ? 2 : 1);
        drawText(renderer, fonts.medium, tr(sortKeys[i]),
                 sortRects[i].x + sortRects[i].w / 2, sortRects[i].y + 14,
                 chosen ? SELECT_TEXT : WHITE, true);
        if (focused) drawText(renderer, fonts.small, "▶", sortRects[i].x - 14, sortRects[i].y + 16, CURSOR_BORDER, false, true);
      }

      const SDL_Rect applyButton{445, 405, 180, 52};
      const SDL_Rect cancelButton{655, 405, 180, 52};
      const SDL_Rect buttonRects[] = {applyButton, cancelButton};
      const UiKey buttonKeys[] = {UiKey::Apply, UiKey::FolderPickerCancel};
      for (int i = 0; i < 2; ++i) {
        const bool active = filterSort.row == 2 && filterSort.buttonIndex == i;
        fillRoundedRect(renderer, buttonRects[i], 14, active ? SELECT_BG : CARD_INNER);
        strokeRoundedRect(renderer, buttonRects[i], 14, active ? CURSOR_BORDER : BORDER, active ? 2 : 1);
        drawText(renderer, fonts.small, tr(buttonKeys[i]),
                 buttonRects[i].x + buttonRects[i].w / 2, buttonRects[i].y + 15,
                 active ? SELECT_TEXT : WHITE, true);
        if (active) drawText(renderer, fonts.small, "▶", buttonRects[i].x - 14, buttonRects[i].y + 15, CURSOR_BORDER, false, true);
      }

      const SDL_Rect folderButton{420, 485, 440, 52};
      const bool folderActive = filterSort.row == 3;
      fillRoundedRect(renderer, folderButton, 14, CARD_INNER);
      strokeRoundedRect(renderer, folderButton, 14, folderActive ? CURSOR_BORDER : BORDER, folderActive ? 2 : 1);
      drawText(renderer, fonts.medium, tr(UiKey::GameFolderButton),
               folderButton.x + folderButton.w / 2, folderButton.y + 14, WHITE, true);
      if (folderActive) drawText(renderer, fonts.small, "▶", folderButton.x - 14, folderButton.y + 16, CURSOR_BORDER, false, true);

      drawText(renderer, fonts.small, tr(UiKey::FilterSortHelp), WIDTH / 2, 565, MUTED2, true);
    }

    if (search.active) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      fillRect(renderer, SDL_Rect{0, 0, WIDTH, HEIGHT}, SDL_Color{0, 0, 0, 188});
      const SDL_Rect panel{270, 190, 740, 330};
      fillRoundedRect(renderer, panel, 20, SDL_Color{7, 24, 41, 242});
      strokeRoundedRect(renderer, panel, 20, SDL_Color{102, 184, 230, 105}, 1);
      drawText(renderer, fonts.normal, tr(UiKey::SearchTitle), WIDTH / 2, 220, WHITE, true);
      drawText(renderer, fonts.small, tr(UiKey::SearchHint), WIDTH / 2, 257, MUTED2, true);
      const SDL_Rect input{350, 286, 580, 62};
      fillRoundedRect(renderer, input, 15, SDL_Color{9, 26, 42, 230});
      strokeRoundedRect(renderer, input, 15, SDL_Color{137, 211, 255, 150}, 1);
      const std::string shown = search.text.empty() ? std::string("_") : search.text + " _";
      drawText(renderer, fonts.medium, truncateText(fonts.medium, shown, 540), 372, 302, WHITE);
      drawText(renderer, fonts.small,
               std::to_string(games.size()) + " / " + std::to_string(allGames.size()),
               915, 358, MUTED2, false, true);
      const SDL_Rect clearButton{430, 390, 190, 52};
      const SDL_Rect closeButton{660, 390, 190, 52};
      fillRoundedRect(renderer, clearButton, 14, CARD_INNER);
      fillRoundedRect(renderer, closeButton, 14, SELECT_BG);
      strokeRoundedRect(renderer, clearButton, 14, BORDER, 1);
      strokeRoundedRect(renderer, closeButton, 14, SDL_Color{140, 218, 255, 180}, 1);
      drawText(renderer, fonts.small, std::string("X  ") + tr(UiKey::SearchClear),
               clearButton.x + clearButton.w / 2, clearButton.y + 15, WHITE, true);
      drawText(renderer, fonts.small, tr(UiKey::SearchClose),
               closeButton.x + closeButton.w / 2, closeButton.y + 15, SELECT_TEXT, true);
      drawText(renderer, fonts.small, tr(UiKey::SearchHelp), WIDTH / 2, 468, MUTED2, true);
    }

    if (gameSettings.active && !games.empty()) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      fillRect(renderer, SDL_Rect{0, 0, WIDTH, HEIGHT}, SDL_Color{0, 0, 0, 122});

      const GameInfo& game = games[selected];
      const SDL_Rect panel{88, 68, 1104, 570};
      const SDL_Rect gamePanel{108, 88, 310, 530};
      const SDL_Rect settingsPanel{438, 88, 734, 530};

      fillRoundedRect(renderer, panel, 24, SDL_Color{5, 20, 36, 202});
      strokeRoundedRect(renderer, panel, 24, SDL_Color{105, 171, 214, 70}, 1);
      fillRoundedRect(renderer, gamePanel, 20, SDL_Color{6, 24, 41, 172});
      strokeRoundedRect(renderer, gamePanel, 20, SDL_Color{93, 148, 184, 48}, 1);
      fillRoundedRect(renderer, settingsPanel, 20, SDL_Color{6, 24, 41, 172});
      strokeRoundedRect(renderer, settingsPanel, 20, SDL_Color{93, 148, 184, 48}, 1);

      SDL_Texture* cover = thumbs.get(thumbnailPathFor(gameRoot, catalog, game));
      const SDL_Rect coverRect{128, 110, 270, 312};
      if (cover) drawCoverRounded(renderer, cover, coverRect, 18);
      else fillRoundedRect(renderer, coverRect, 18, SDL_Color{22, 48, 72, 220});
      strokeRoundedRect(renderer, coverRect, 18, SDL_Color{115, 188, 229, 78}, 1);

      drawText(renderer, fonts.normal,
               truncateText(fonts.normal, displayNameFor(game, catalog), gamePanel.w - 40),
               gamePanel.x + 22, 446, WHITE);
      const SDL_Rect engineTag{gamePanel.x + 22, 492, 96, 28};
      fillRoundedRect(renderer, engineTag, 14, SDL_Color{82, 88, 96, 102});
      drawText(renderer, fonts.small, compactEngineLabel(game.engine),
               engineTag.x + engineTag.w / 2, engineTag.y + 4,
               SDL_Color{211, 236, 250, 255}, true);
      drawText(renderer, fonts.small,
               uiWord("이 게임에만 적용되는 설정", "Settings for this game only", "このゲームだけに適用"),
               gamePanel.x + 22, 540, SDL_Color{133, 165, 189, 255});

      drawText(renderer, fonts.normal,
               gameSettings.editingName ? tr(UiKey::NameEditTitle) : tr(UiKey::GameSettingsTitle),
               settingsPanel.x + 26, 112, WHITE);
      drawText(renderer, fonts.small,
               std::string(tr(UiKey::Engine)) + ":  " + engineLabel(game.engine),
               settingsPanel.x + 28, 150, SDL_Color{141, 173, 197, 255});

      auto drawSettingRow = [&](const SDL_Rect& row, bool active,
                                const std::string& label, const std::string& value,
                                bool showArrows = false) {
        if (active) {
          fillRoundedRect(renderer, SDL_Rect{row.x - 2, row.y - 2, row.w + 4, row.h + 4},
                          17, SDL_Color{73, 181, 255, 52});
          fillRoundedRect(renderer, row, 15, SDL_Color{28, 88, 141, 202});
          strokeRoundedRect(renderer, row, 15, SDL_Color{127, 214, 255, 150}, 1);
        } else {
          fillRoundedRect(renderer, row, 15, SDL_Color{9, 33, 54, 166});
          strokeRoundedRect(renderer, row, 15, SDL_Color{87, 133, 165, 42}, 1);
        }

        const SDL_Color labelColor = active ? SDL_Color{208, 239, 255, 255}
                                            : SDL_Color{148, 177, 198, 255};
        const SDL_Color valueColor = active ? WHITE : SDL_Color{221, 232, 241, 255};
        drawText(renderer, fonts.small, label, row.x + 18,
                 centeredTextY(fonts.small, row, label), labelColor);

        const int valueRight = row.x + row.w - 23;
        const std::string shownValue = truncateText(fonts.medium, value, showArrows ? 330 : 380);
        drawText(renderer, fonts.medium, shownValue, valueRight,
                 centeredTextY(fonts.medium, row, shownValue), valueColor, false, true);
        if (showArrows && active) {
          const SDL_Rect leftArrowBox{row.x + row.w - 400, row.y, 30, row.h};
          const SDL_Rect rightArrowBox{row.x + row.w - 20, row.y, 16, row.h};
          drawText(renderer, fonts.small, "<", leftArrowBox.x + leftArrowBox.w / 2,
                   centeredTextY(fonts.small, leftArrowBox, "<"), SDL_Color{188, 231, 255, 255}, true);
          drawText(renderer, fonts.small, ">", rightArrowBox.x + rightArrowBox.w / 2,
                   centeredTextY(fonts.small, rightArrowBox, ">"), SDL_Color{188, 231, 255, 255}, true);
        }
      };

      const SDL_Rect nameRow{460, 176, 680, 52};
      const bool nameActive = gameSettings.row == 0 || gameSettings.editingName;
      if (nameActive) {
        fillRoundedRect(renderer, SDL_Rect{nameRow.x - 2, nameRow.y - 2, nameRow.w + 4, nameRow.h + 4},
                        17, SDL_Color{73, 181, 255, 52});
        fillRoundedRect(renderer, nameRow, 15, SDL_Color{28, 88, 141, 202});
        strokeRoundedRect(renderer, nameRow, 15, SDL_Color{127, 214, 255, 150}, 1);
      } else {
        fillRoundedRect(renderer, nameRow, 15, SDL_Color{9, 33, 54, 166});
        strokeRoundedRect(renderer, nameRow, 15, SDL_Color{87, 133, 165, 42}, 1);
      }

      drawText(renderer, fonts.small, tr(UiKey::GameName), nameRow.x + 18,
               centeredTextY(fonts.small, nameRow, tr(UiKey::GameName)),
               nameActive ? SDL_Color{208, 239, 255, 255} : SDL_Color{148, 177, 198, 255});

      const SDL_Rect nameValue{650, 180, 479, 44};
      if (gameSettings.editingName) {
        fillRoundedRect(renderer, nameValue, 12, SDL_Color{7, 25, 42, 228});
        strokeRoundedRect(renderer, nameValue, 12, SDL_Color{137, 211, 255, 145}, 1);
      }
      const std::string shownName = gameSettings.editingName ? gameSettings.editText
                                                             : displayNameFor(game, catalog);
      const std::string nameText = truncateText(fonts.medium,
                                                 shownName + (gameSettings.editingName ? " _" : ""),
                                                 430);
      drawText(renderer, fonts.medium, nameText,
               nameValue.x + nameValue.w - 12,
               centeredTextY(fonts.medium, nameValue, nameText),
               WHITE, false, true);

      clampSettingsScroll();
      const bool protonMode = supportsProtonCompatibility(game) && protonCompatibilityEnabled(game);
      const int doneRow = settingsDoneRow();

      auto drawActualSettingRow = [&](int actualRow, const SDL_Rect& rowRect) {
        const bool active = gameSettings.row == actualRow && !gameSettings.editingName;
        if (actualRow == 1) {
          drawSettingRow(rowRect, active,
                         uiWord("로케일", "Locale", "ロケール"),
                         gameLocaleUiLabel(gameSettings.locale), true);
          return;
        }

        if (isRgssEngine(game.engine)) {
          if (actualRow == 2) {
            drawSettingRow(rowRect, active, tr(UiKey::RubyRuntime), rubySettingText(game), true);
            return;
          }
          if (actualRow == 3) {
            drawSettingRow(rowRect, active,
                           uiWord("호환성 모드", "Compatibility Mode", "互換モード"),
                           compatibilityModeLabel(game), true);
            return;
          }
          if (protonMode && actualRow == 4) {
            drawSettingRow(rowRect, active, "Proton", gameProtonLabel(game), true);
            return;
          }
          if (protonMode && actualRow == 5) {
            std::string envValue = protonEnvPresetLabel(game);
            const std::size_t customCount = customProtonEnvironmentCount(game);
            if (customCount > 0)
              envValue += uiWord(" + 사용자 ", " + custom ", " + custom ") + std::to_string(customCount);
            drawSettingRow(rowRect, active,
                           uiWord("환경변수 프리셋", "Environment preset", "環境変数プリセット"),
                           envValue, true);
            return;
          }
          const int scanRow = protonMode ? 6 : 4;
          if (actualRow == scanRow) {
            const std::string value = gameSettings.rubyScanResult.empty()
              ? uiWord("A 눌러 검사", "Press A to scan", "A で検査")
              : gameSettings.rubyScanResult;
            drawSettingRow(rowRect, active, rubyDeepScanLabel(), value);
            return;
          }
          const int remapRow = protonMode ? 7 : 5;
          if (actualRow == remapRow) {
            drawSettingRow(rowRect, active,
                           uiWord("패드 키 설정", "Gamepad Mapping", "ゲームパッド設定"),
                           protonMode
                             ? uiWord("Proton 모드 사용 불가", "Unavailable in Proton mode", "Proton モードでは使用不可")
                             : uiWord("A 눌러 설정", "Press A", "A で設定"));
            return;
          }
        } else if (isWebEngine(game.engine)) {
          if (actualRow == 2) {
            drawSettingRow(rowRect, active, "NW.js", nwjsSettingText(game), true);
            return;
          }
          if (actualRow == 3) {
            drawSettingRow(rowRect, active,
                           uiWord("호환성 모드", "Compatibility Mode", "互換モード"),
                           compatibilityModeLabel(game), true);
            return;
          }
          if (protonMode && actualRow == 4) {
            drawSettingRow(rowRect, active, "Proton", gameProtonLabel(game), true);
            return;
          }
          if (protonMode && actualRow == 5) {
            std::string envValue = protonEnvPresetLabel(game);
            const std::size_t customCount = customProtonEnvironmentCount(game);
            if (customCount > 0)
              envValue += uiWord(" + 사용자 ", " + custom ", " + custom ") + std::to_string(customCount);
            drawSettingRow(rowRect, active,
                           uiWord("환경변수 프리셋", "Environment preset", "環境変数プリセット"),
                           envValue, true);
            return;
          }
          const int scanRow = protonMode ? 6 : 4;
          if (actualRow == scanRow) {
            const std::string value = gameSettings.nwjsScanResult.empty()
              ? uiWord("A 눌러 검사", "Press A to scan", "A で検査")
              : gameSettings.nwjsScanResult;
            drawSettingRow(rowRect, active, nwjsDeepScanLabel(), value);
            return;
          }
        } else if (isEasyRpgEngine(game.engine)) {
          if (actualRow == 2) {
            drawSettingRow(rowRect, active,
                           uiWord("인코딩", "Encoding", "エンコーディング"),
                           easyRpgEncodingLabel(game), true);
            return;
          }
          if (actualRow == 3) {
            drawSettingRow(rowRect, active, "MIDI SoundFont",
                           easyRpgChoiceLabel(game, EasyRpgChoiceKind::Soundfont), true);
            return;
          }
          if (actualRow == 4) {
            drawSettingRow(rowRect, active, "Font 1",
                           easyRpgChoiceLabel(game, EasyRpgChoiceKind::Font1), true);
            return;
          }
          if (actualRow == 5) {
            drawSettingRow(rowRect, active, "Font 2",
                           easyRpgChoiceLabel(game, EasyRpgChoiceKind::Font2), true);
            return;
          }
        } else if (isWolfRpgEngine(game.engine)) {
          if (actualRow == 2) {
            drawSettingRow(rowRect, active, "Proton", wolfProtonLabel(game), true);
            return;
          }
          if (actualRow == 3) {
            const bool hasConfig = findWolfExecutable(game, true).has_value();
            drawSettingRow(rowRect, active, "Config.exe",
                           hasConfig ? uiWord("A 눌러 실행", "Press A to run", "A で実行")
                                     : uiWord("없음", "Not found", "なし"));
            return;
          }
        }
      };

      for (int slot = 0; slot < SETTINGS_VISIBLE_MIDDLE_ROWS; ++slot) {
        const int actualRow = gameSettings.scrollFirstRow + slot;
        if (actualRow >= doneRow) break;
        drawActualSettingRow(actualRow, SDL_Rect{460, 236 + slot * 44, 680, 40});
      }

      const int middleRows = std::max(0, doneRow - 1);
      if (middleRows > SETTINGS_VISIBLE_MIDDLE_ROWS) {
        const SDL_Rect track{1149, 236, 5, 216};
        fillRoundedRect(renderer, track, 3, SDL_Color{90, 126, 153, 52});
        const int maxFirst = middleRows - SETTINGS_VISIBLE_MIDDLE_ROWS + 1;
        const int thumbH = std::max(42, track.h * SETTINGS_VISIBLE_MIDDLE_ROWS / middleRows);
        const int travel = track.h - thumbH;
        const int offset = maxFirst > 1
          ? travel * (gameSettings.scrollFirstRow - 1) / (maxFirst - 1)
          : 0;
        fillRoundedRect(renderer, SDL_Rect{track.x, track.y + offset, track.w, thumbH}, 3,
                        SDL_Color{119, 196, 239, 155});
      }

      const SDL_Rect doneButton{860, 468, 240, 42};
      const bool doneActive = gameSettings.row == doneRow && !gameSettings.editingName;
      fillRoundedRect(renderer, doneButton, 15,
                      doneActive ? SDL_Color{34, 110, 177, 220} : SDL_Color{11, 38, 61, 180});
      strokeRoundedRect(renderer, doneButton, 15,
                        doneActive ? SDL_Color{126, 214, 255, 165} : SDL_Color{88, 132, 164, 54}, 1);
      drawText(renderer, fonts.medium, tr(UiKey::Done),
               doneButton.x + doneButton.w / 2,
               centeredTextY(fonts.medium, doneButton, tr(UiKey::Done)),
               doneActive ? WHITE : SDL_Color{190, 209, 225, 255}, true);

      if (protonMode) {
        const char* protonHelp = gUiLanguage == UiLanguage::Korean
          ? "ENV 직접 설정: mkxp-proton-env.txt (KEY=VALUE) · Proton은 SteamOS 컨트롤러 설정 사용"
          : (gUiLanguage == UiLanguage::Japanese
             ? "ENV 手動設定: mkxp-proton-env.txt (KEY=VALUE) · Proton は SteamOS コントローラー設定を使用"
             : "Custom ENV: mkxp-proton-env.txt (KEY=VALUE) · Proton uses the SteamOS controller layout");
        drawText(renderer, fonts.small, protonHelp,
                 settingsPanel.x + settingsPanel.w / 2, 526,
                 SDL_Color{142, 175, 199, 255}, true);
      }
      drawText(renderer, fonts.small,
               gameSettings.editingName ? tr(UiKey::NameEditHelp) : tr(UiKey::SettingsHelp),
               settingsPanel.x + settingsPanel.w / 2, 552,
               SDL_Color{128, 159, 184, 255}, true);
    }
    if (keyRemap.active && !games.empty()) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      fillRect(renderer, SDL_Rect{0, 0, WIDTH, HEIGHT}, SDL_Color{0, 0, 0, 204});
      const SDL_Rect panel{260, 100, 760, 520};
      fillRoundedRect(renderer, panel, 20, SDL_Color{7, 24, 41, 242});
      strokeRoundedRect(renderer, panel, 20, SDL_Color{102, 184, 230, 105}, 1);

      const char* title = gUiLanguage == UiLanguage::Korean ? "게임별 패드 키 설정" :
                          (gUiLanguage == UiLanguage::Japanese ? "ゲーム別ゲームパッド設定" : "Per-game Gamepad Mapping");
      drawText(renderer, fonts.normal, title, WIDTH / 2, 130, WHITE, true);
      const char* explain = gUiLanguage == UiLanguage::Korean
        ? "현재 버튼에 실제로 묶인 기능을 새 물리 버튼으로 이동합니다"
        : (gUiLanguage == UiLanguage::Japanese
           ? "現在のボタン機能を新しい物理ボタンへ移動します"
           : "Moves the action currently bound to one physical button to another");
      drawText(renderer, fonts.small, explain, WIDTH / 2, 170, MUTED2, true);

      const char* defaults1 = "MKXP: A→C   B→B   X→A   Y→X";
      const char* defaults2 = "      L3→Y  R3→Z  L1→L  R1→R";
      drawText(renderer, fonts.small, defaults1, WIDTH / 2, 205, MUTED2, true);
      drawText(renderer, fonts.small, defaults2, WIDTH / 2, 232, MUTED2, true);

      int y = 278;
      if (keyRemap.entries.empty()) {
        const char* none = gUiLanguage == UiLanguage::Korean ? "저장된 게임별 매핑 없음" :
                           (gUiLanguage == UiLanguage::Japanese ? "保存されたゲーム別マッピングなし" : "No saved per-game mappings");
        drawText(renderer, fonts.medium, none, WIDTH / 2, y, MUTED, true);
      } else {
        for (std::size_t i = 0; i < keyRemap.entries.size() && i < 8; ++i) {
          const std::string text = std::string(controllerButtonShortName(keyRemap.entries[i].from)) +
                                   "  →  " + controllerButtonShortName(keyRemap.entries[i].to);
          drawText(renderer, fonts.medium, text, WIDTH / 2, y + static_cast<int>(i) * 30, WHITE, true);
        }
      }

      std::string prompt;
      if (keyRemap.stage == 1) {
        prompt = gUiLanguage == UiLanguage::Korean ? "현재(바꿀) 버튼을 누르세요" :
                 (gUiLanguage == UiLanguage::Japanese ? "現在のボタンを押してください" : "Press the current/source button");
      } else if (keyRemap.stage == 2) {
        prompt = (gUiLanguage == UiLanguage::Korean ? "새 버튼을 누르세요: " :
                  (gUiLanguage == UiLanguage::Japanese ? "新しいボタンを押してください: " : "Press new button for: ")) +
                 std::string(controllerButtonShortName(keyRemap.source));
      } else {
        prompt = gUiLanguage == UiLanguage::Korean ? "A 새 매핑    X 전체 초기화    B 닫기" :
                 (gUiLanguage == UiLanguage::Japanese ? "A 新規   X 全初期化   B 閉じる" : "A New mapping   X Reset all   B Close");
      }
      drawText(renderer, fonts.medium, prompt, WIDTH / 2, 545, keyRemap.stage ? SELECT_BG : WHITE, true);
      if (keyRemap.stage != 0) {
        const char* cancel = gUiLanguage == UiLanguage::Korean ? "START: 입력 취소" :
                             (gUiLanguage == UiLanguage::Japanese ? "START: 入力取消" : "START: Cancel capture");
        drawText(renderer, fonts.small, cancel, WIDTH / 2, 580, MUTED2, true);
      } else if (!keyRemap.message.empty()) {
        drawText(renderer, fonts.small, keyRemap.message, WIDTH / 2, 580, SELECT_BG, true);
      }
    }

    SDL_RenderPresent(renderer);
    nextIdleRedrawAt = SDL_GetTicks64() + 750;
    }

    const Uint64 loopElapsed = SDL_GetTicks64() - loopStartedAt;
    const Uint64 targetLoopMs = timedUiActive ? 8 : 50;
    if (loopElapsed < targetLoopMs) SDL_Delay(static_cast<Uint32>(targetLoopMs - loopElapsed));
  }

  if (controller) SDL_GameControllerClose(controller);
  closeFonts(fonts);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  IMG_Quit();
  SDL_Quit();
  return 0;
}

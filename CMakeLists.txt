cmake_minimum_required(VERSION 3.16)
project(MKXP_Launcher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)

add_executable(MKXP_Launcher
  src/main.cpp
  src/ui_sound.cpp
  src/game_scanner.cpp
  src/catalog.cpp
  src/game_view.cpp
  src/ruby_detector.cpp
  src/launcher_config.cpp
)

target_include_directories(MKXP_Launcher PRIVATE src)
target_link_libraries(MKXP_Launcher PRIVATE
  PkgConfig::SDL2
  PkgConfig::SDL2_TTF
  PkgConfig::SDL2_IMAGE
  ${CMAKE_DL_LIBS}
)

target_compile_options(MKXP_Launcher PRIVATE -Wall -Wextra -Wpedantic)

add_executable(MKXP_Scanner_Smoke
  tests/scanner_smoke.cpp
  src/game_scanner.cpp
  src/catalog.cpp
)
target_include_directories(MKXP_Scanner_Smoke PRIVATE src)
target_compile_options(MKXP_Scanner_Smoke PRIVATE -Wall -Wextra -Wpedantic)

add_executable(MKXP_Ruby_Detector_Smoke
  tests/ruby_detector_smoke.cpp
  src/ruby_detector.cpp
)
target_include_directories(MKXP_Ruby_Detector_Smoke PRIVATE src)
target_compile_options(MKXP_Ruby_Detector_Smoke PRIVATE -Wall -Wextra -Wpedantic)

add_executable(MKXP_Launcher_Config_Smoke
  tests/launcher_config_smoke.cpp
  src/launcher_config.cpp
)
target_include_directories(MKXP_Launcher_Config_Smoke PRIVATE src)
target_compile_options(MKXP_Launcher_Config_Smoke PRIVATE -Wall -Wextra -Wpedantic)

add_executable(MKXP_Game_View_Smoke
  tests/game_view_smoke.cpp
  src/game_view.cpp
  src/catalog.cpp
)
target_include_directories(MKXP_Game_View_Smoke PRIVATE src)
target_compile_options(MKXP_Game_View_Smoke PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS MKXP_Launcher RUNTIME DESTINATION .)

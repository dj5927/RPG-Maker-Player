#pragma once

#include "game_scanner.hpp"

#include <filesystem>
#include <functional>
#include <string>

std::string detectRubyRuntime(const std::filesystem::path& launcherRoot,
                              const GameInfo& game,
                              std::string& diagnostic);

std::string scanRubyRuntimeDeep(const std::filesystem::path& launcherRoot,
                                const GameInfo& game,
                                std::string& diagnostic,
                                const std::function<void(const std::string&)>& progress = {});


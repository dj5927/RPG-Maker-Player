#include "ruby_detector.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  if (argc < 2) return 64;
  const fs::path root = fs::absolute(argv[1]);
  const fs::path fixtureRoot = root / "_dev/tests/ruby_probe_fixtures";

  struct Case { const char* file; const char* expected; };
  const std::vector<Case> cases = {
    {"ruby18.rxdata", "ruby18"},
    {"ruby19.rvdata", "ruby19"},
    {"ruby31.rvdata2", "ruby31"},
  };

  bool ok = true;
  for (const auto& test : cases) {
    GameInfo game;
    game.path = fixtureRoot;
    game.scriptArchive = fixtureRoot / test.file;

    std::string diagnostic;
    const std::string got = detectRubyRuntime(root, game, diagnostic);
    std::cout << test.file << " -> " << got << " (" << diagnostic << ")\n";
    if (got != test.expected) ok = false;

    std::string deepDiagnostic;
    const std::string deepGot = scanRubyRuntimeDeep(root, game, deepDiagnostic);
    std::cout << "  deep -> " << deepGot << " (" << deepDiagnostic << ")\n";
    if (deepGot != test.expected) ok = false;
  }
  return ok ? 0 : 1;
}

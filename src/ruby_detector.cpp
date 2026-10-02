#include "ruby_detector.hpp"

#include <algorithm>
#include <cctype>
#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string trim(std::string value) {
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) value.pop_back();
  std::size_t start = 0;
  while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) ++start;
  return value.substr(start);
}

bool validRuntime(const std::string& value) {
  return value == "ruby18" || value == "ruby19" || value == "ruby31";
}


std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string strongRuntimeSignature(const GameInfo& game, std::string& evidence) {
  std::error_code ec;
  if (!fs::is_directory(game.path, ec)) return {};

  for (const auto& entry : fs::directory_iterator(game.path, ec)) {
    if (ec || !entry.is_regular_file(ec)) continue;
    const std::string name = lowerAscii(entry.path().filename().string());

    if (name.find("ruby310.dll") != std::string::npos ||
        name.find("ruby31.dll") != std::string::npos ||
        name.find("ruby3.1") != std::string::npos) {
      evidence = entry.path().filename().string();
      return "ruby31";
    }
    if (name.find("ruby193.dll") != std::string::npos ||
        name.find("ruby191.dll") != std::string::npos ||
        name.find("ruby19.dll") != std::string::npos ||
        name.find("ruby1.9") != std::string::npos) {
      evidence = entry.path().filename().string();
      return "ruby19";
    }
    if (name.find("ruby187.dll") != std::string::npos ||
        name.find("ruby18.dll") != std::string::npos ||
        name.find("ruby1.8") != std::string::npos) {
      evidence = entry.path().filename().string();
      return "ruby18";
    }
  }
  return {};
}

std::string rubyCacheKey(const GameInfo& game) {
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

std::string rubyArchiveSignature(const GameInfo& game) {
  if (game.scriptArchive.empty()) return {};
  std::error_code ec;
  const auto size = fs::file_size(game.scriptArchive, ec);
  if (ec) return {};
  const auto stamp = fs::last_write_time(game.scriptArchive, ec);
  if (ec) return {};
  std::ostringstream out;
  out << fs::absolute(game.scriptArchive).generic_string() << '|' << size << '|'
      << stamp.time_since_epoch().count();
  return out.str();
}

bool readRubyCache(const fs::path& launcherRoot, const GameInfo& game, std::string& runtime) {
  const std::string signature = rubyArchiveSignature(game);
  if (signature.empty()) return false;
  std::ifstream in(launcherRoot / "cache/ruby_runtime" / (rubyCacheKey(game) + ".txt"), std::ios::binary);
  if (!in) return false;
  std::string cachedSignature;
  std::string cachedRuntime;
  std::getline(in, cachedSignature);
  std::getline(in, cachedRuntime);
  cachedSignature = trim(cachedSignature);
  cachedRuntime = trim(cachedRuntime);
  if (cachedSignature != signature || !validRuntime(cachedRuntime)) return false;
  runtime = cachedRuntime;
  return true;
}

void writeRubyCache(const fs::path& launcherRoot, const GameInfo& game, const std::string& runtime) {
  const std::string signature = rubyArchiveSignature(game);
  if (signature.empty() || !validRuntime(runtime)) return;
  std::error_code ec;
  const fs::path dir = launcherRoot / "cache/ruby_runtime";
  fs::create_directories(dir, ec);
  if (ec) return;
  std::ofstream out(dir / (rubyCacheKey(game) + ".txt"), std::ios::binary | std::ios::trunc);
  if (!out) return;
  out << signature << '\n' << runtime << '\n';
}

std::string manualOverride(const GameInfo& game) {
  const fs::path path = game.path / "mkxp-ruby.txt";
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::string value;
  std::getline(in, value);
  value = trim(value);
  return validRuntime(value) ? value : std::string{};
}

struct Candidate {
  const char* name;
  const char* folder;
};

#if defined(__linux__)
std::string portableCliRubyLib(const fs::path& rubyRoot) {
  const std::array<fs::path, 12> candidates{{
    rubyRoot / "lib/ruby/1.8",
    rubyRoot / "lib/ruby/1.8/x86_64-linux",
    rubyRoot / "lib/ruby/site_ruby/1.8",
    rubyRoot / "lib/ruby/site_ruby/1.8/x86_64-linux",
    rubyRoot / "lib/ruby/1.9.1",
    rubyRoot / "lib/ruby/1.9.1/x86_64-linux",
    rubyRoot / "lib/ruby/site_ruby/1.9.1",
    rubyRoot / "lib/ruby/site_ruby/1.9.1/x86_64-linux",
    rubyRoot / "lib/ruby/3.1.0",
    rubyRoot / "lib/ruby/3.1.0/x86_64-linux",
    rubyRoot / "lib/ruby/site_ruby/3.1.0",
    rubyRoot / "lib/ruby/site_ruby/3.1.0/x86_64-linux",
  }};

  std::string out;
  for (const auto& path : candidates) {
    std::error_code ec;
    if (!fs::exists(path, ec)) continue;
    if (!out.empty()) out += ':';
    out += fs::absolute(path).string();
  }
  return out;
}

int runProbe(const fs::path& launcherRoot, const fs::path& ruby, const fs::path& archive,
             const fs::path& runtimeRoot, bool deep = false) {
  const fs::path probe = launcherRoot / "runtime/ruby_probe.rb";
  if (!fs::is_regular_file(ruby) || !fs::is_regular_file(probe) || !fs::is_regular_file(archive)) return 127;

  const pid_t pid = fork();
  if (pid == 0) {
    const fs::path rubyRoot = ruby.parent_path().parent_path();
    // Portable Ruby CLIs have different layouts across 1.8/1.9/3.1. Some
    // expect libruby next to the runtime root while others use ruby/lib.
    // Include both so SteamOS never falls back to the build-machine RPATH.
    std::string ld = runtimeRoot.string() + ":" +
                     (rubyRoot / "lib").string() + ":" +
                     (rubyRoot / "openssl/lib").string();
    if (const char* old = getenv("LD_LIBRARY_PATH")) {
      if (*old) ld += ":" + std::string(old);
    }
    setenv("LD_LIBRARY_PATH", ld.c_str(), 1);
    setenv("MKXP_PROBE_RUBY", ruby.c_str(), 1);

    const std::string rubyLib = portableCliRubyLib(rubyRoot);
    if (!rubyLib.empty()) setenv("RUBYLIB", rubyLib.c_str(), 1);
    setenv("MKXP_PROBE_DEBUG", "1", 1);
    if (deep) setenv("MKXP_PROBE_DEEP", "1", 1);
    else unsetenv("MKXP_PROBE_DEEP");

    const fs::path logPath = launcherRoot / "logs/ruby_probe.log";
    const int logfd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logfd >= 0) {
      dup2(logfd, STDOUT_FILENO);
      dup2(logfd, STDERR_FILENO);
      close(logfd);
    }
    execl(ruby.c_str(), ruby.c_str(), probe.c_str(), archive.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) return 126;
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) return 126;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 125;
}
#endif

} // namespace

std::string detectRubyRuntime(const fs::path& launcherRoot, const GameInfo& game, std::string& diagnostic) {
  const std::string overrideValue = manualOverride(game);
  if (!overrideValue.empty()) {
    diagnostic = "manual:" + overrideValue;
    return overrideValue;
  }

  std::string signatureEvidence;
  const std::string signatureRuntime = strongRuntimeSignature(game, signatureEvidence);
  if (!signatureRuntime.empty()) {
    diagnostic = "signature:" + signatureRuntime + ":" + signatureEvidence;
    writeRubyCache(launcherRoot, game, signatureRuntime);
    return signatureRuntime;
  }

  if (game.scriptArchive.empty()) {
    diagnostic = "probe:no-script-archive";
    return "ruby31";
  }

  std::string cachedRuntime;
  if (readRubyCache(launcherRoot, game, cachedRuntime)) {
    diagnostic = "cache:" + cachedRuntime;
    return cachedRuntime;
  }

#if defined(__linux__)
  std::ostringstream attempts;
  constexpr std::array<Candidate, 3> candidates{{
    {"ruby18", "ruby18"},
    {"ruby19", "ruby19"},
    {"ruby31", "ruby31"},
  }};
  for (const auto& candidate : candidates) {
    const fs::path runtimeRoot = launcherRoot / "runtime" / candidate.folder;
    fs::path ruby = runtimeRoot / "ruby/bin/ruby";
    if (std::string(candidate.name) == "ruby19") {
      const fs::path safeRuby = runtimeRoot / "nothreaded_ruby/bin/ruby";
      std::error_code ec;
      if (fs::is_regular_file(safeRuby, ec)) ruby = safeRuby;
    }
    const int code = runProbe(launcherRoot, ruby, game.scriptArchive, runtimeRoot);
    if (code == 0) {
      diagnostic = std::string("probe:") + candidate.name;
      writeRubyCache(launcherRoot, game, candidate.name);
      return candidate.name;
    }
    if (attempts.tellp() > 0) attempts << ',';
    attempts << candidate.name << '=' << code;
  }
  diagnostic = "probe:failed[" + attempts.str() + "]-fallback-ruby31";
  return "ruby31";
#endif

  diagnostic = "probe:unsupported-platform-fallback-ruby31";
  return "ruby31";
}

std::string scanRubyRuntimeDeep(const fs::path& launcherRoot, const GameInfo& game,
                                std::string& diagnostic,
                                const std::function<void(const std::string&)>& progress) {
  if (game.scriptArchive.empty()) {
    diagnostic = "deep:no-script-archive";
    return "ruby31";
  }

#if defined(__linux__)
  constexpr std::array<Candidate, 3> candidates{{
    {"ruby18", "ruby18"},
    {"ruby19", "ruby19"},
    {"ruby31", "ruby31"},
  }};
  std::array<int, 3> codes{{127, 127, 127}};
  int firstPassing = -1;
  std::string signatureEvidence;
  const std::string signatureRuntime = strongRuntimeSignature(game, signatureEvidence);

  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& candidate = candidates[i];
    if (progress) progress(candidate.name);
    const fs::path runtimeRoot = launcherRoot / "runtime" / candidate.folder;
    fs::path ruby = runtimeRoot / "ruby/bin/ruby";
    if (std::string(candidate.name) == "ruby19") {
      const fs::path nonThreaded = runtimeRoot / "nothreaded_ruby/bin/ruby";
      std::error_code ec;
      if (fs::is_regular_file(nonThreaded, ec)) ruby = nonThreaded;
    }
    codes[i] = runProbe(launcherRoot, ruby, game.scriptArchive, runtimeRoot, true);
    if (codes[i] == 0 && firstPassing < 0) firstPassing = static_cast<int>(i);
  }

  std::ostringstream detail;
  detail << "deep[ruby18=" << (codes[0] == 0 ? "ok" : std::to_string(codes[0]))
         << ",ruby19=" << (codes[1] == 0 ? "ok" : std::to_string(codes[1]))
         << ",ruby31=" << (codes[2] == 0 ? "ok" : std::to_string(codes[2])) << "]";

  if (firstPassing >= 0) {
    std::string runtime;
    if (!signatureRuntime.empty()) {
      runtime = signatureRuntime;
    } else if (codes[0] == 0 && codes[1] != 0 && codes[2] == 0) {
      runtime = "ruby31";
    } else {
      runtime = candidates[static_cast<std::size_t>(firstPassing)].name;
    }
    diagnostic = detail.str();
    if (!signatureRuntime.empty())
      diagnostic += "+signature[" + signatureEvidence + "]->" + runtime;
    else if (codes[0] == 0 && codes[1] != 0 && codes[2] == 0)
      diagnostic += "+nonmonotonic->" + runtime;
    else
      diagnostic += "->" + runtime;
    writeRubyCache(launcherRoot, game, runtime);
    return runtime;
  }

  diagnostic = detail.str() + "->fallback-ruby31";
  return "ruby31";
#else
  diagnostic = "deep:unsupported-platform-fallback-ruby31";
  return "ruby31";
#endif
}

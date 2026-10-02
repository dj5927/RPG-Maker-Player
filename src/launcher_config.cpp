#include "launcher_config.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string escapeJson(const std::string& text) {
  std::string out;
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          const char* hex = "0123456789ABCDEF";
          out += "\\u00";
          out.push_back(hex[(c >> 4) & 0xF]);
          out.push_back(hex[c & 0xF]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

void appendUtf8(std::string& out, unsigned value) {
  if (value <= 0x7F) out.push_back(static_cast<char>(value));
  else if (value <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (value >> 6)));
    out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xE0 | (value >> 12)));
    out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  }
}

bool parseHex4(const std::string& s, std::size_t& i, unsigned& out) {
  if (i + 4 > s.size()) return false;
  out = 0;
  for (int n = 0; n < 4; ++n) {
    const char c = s[i++];
    out <<= 4;
    if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(10 + c - 'a');
    else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(10 + c - 'A');
    else return false;
  }
  return true;
}

bool parseJsonString(const std::string& s, std::size_t& i, std::string& out) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  if (i >= s.size() || s[i] != '"') return false;
  ++i;
  out.clear();
  while (i < s.size()) {
    const char c = s[i++];
    if (c == '"') return true;
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (i >= s.size()) return false;
    const char e = s[i++];
    switch (e) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        unsigned value = 0;
        if (!parseHex4(s, i, value)) return false;
        appendUtf8(out, value);
        break;
      }
      default: return false;
    }
  }
  return false;
}

bool parseGameRoot(const std::string& text, std::string& gameRoot) {
  const std::string key = "\"gameRoot\"";
  std::size_t pos = text.find(key);
  if (pos == std::string::npos) return false;
  pos += key.size();
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
  if (pos >= text.size() || text[pos++] != ':') return false;
  return parseJsonString(text, pos, gameRoot);
}
bool parseNamedString(const std::string& text, const std::string& name, std::string& value) {
  const std::string key = "\"" + name + "\"";
  std::size_t pos = text.find(key);
  if (pos == std::string::npos) return false;
  pos += key.size();
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
  if (pos >= text.size() || text[pos++] != ':') return false;
  return parseJsonString(text, pos, value);
}

std::string normalizedAbsolute(const fs::path& path) {
  std::error_code ec;
  const fs::path absolute = fs::absolute(path, ec);
  return (ec ? path : absolute).lexically_normal().generic_string();
}

std::string fnv1a64Hex(const std::string& text) {
  std::uint64_t hash = 14695981039346656037ull;
  for (unsigned char c : text) {
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ull;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}

} // namespace

LauncherConfig loadLauncherConfig(const fs::path& launcherRoot, std::string& diagnostic) {
  LauncherConfig config;
  const fs::path path = launcherRoot / "config/launcher.json";
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    diagnostic = "config missing";
    return config;
  }

  std::ostringstream buffer;
  buffer << in.rdbuf();
  std::string rawGameRoot;
  if (!parseGameRoot(buffer.str(), rawGameRoot) || rawGameRoot.empty()) {
    diagnostic = "config parse failed";
    return config;
  }

  config.gameRoot = fs::path(rawGameRoot).lexically_normal();
  config.hasGameRoot = true;
  std::string engineFilter;
  if (parseNamedString(buffer.str(), "engineFilter", engineFilter)) {
    if (engineFilter == "all" || engineFilter == "easyrpg" || engineFilter == "wolf" || engineFilter == "xp" || engineFilter == "vx" ||
        engineFilter == "vxace" || engineFilter == "mv" || engineFilter == "mz") {
      config.engineFilter = engineFilter;
    }
  }
  std::string sortMode;
  if (parseNamedString(buffer.str(), "sortMode", sortMode)) {
    if (sortMode == "type" || sortMode == "name") config.sortMode = sortMode;
  }
  std::string viewMode;
  if (parseNamedString(buffer.str(), "viewMode", viewMode)) {
    if (viewMode == "grid" || viewMode == "list") config.viewMode = viewMode;
  }
  diagnostic = "config ready";
  return config;
}

bool saveLauncherConfig(const fs::path& launcherRoot,
                        const LauncherConfig& config,
                        std::string& diagnostic) {
  if (!config.hasGameRoot || config.gameRoot.empty()) {
    diagnostic = "config gameRoot missing";
    return false;
  }

  std::error_code ec;
  const fs::path configDir = launcherRoot / "config";
  fs::create_directories(configDir, ec);
  if (ec) {
    diagnostic = "config directory create failed";
    return false;
  }

  const fs::path path = configDir / "launcher.json";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    diagnostic = "config write failed";
    return false;
  }
  out << "{\n"
      << "  \"gameRoot\": \"" << escapeJson(normalizedAbsolute(config.gameRoot)) << "\",\n"
      << "  \"engineFilter\": \"" << escapeJson(config.engineFilter) << "\",\n"
      << "  \"sortMode\": \"" << escapeJson(config.sortMode) << "\",\n"
      << "  \"viewMode\": \"" << escapeJson(config.viewMode) << "\"\n"
      << "}\n";
  diagnostic = out ? "config saved" : "config write failed";
  return static_cast<bool>(out);
}

fs::path catalogStorageRoot(const fs::path& launcherRoot, const fs::path& gameRoot) {
  return launcherRoot / "config/catalogs" / fnv1a64Hex(normalizedAbsolute(gameRoot));
}


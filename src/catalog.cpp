#include "catalog.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

void skipWs(const std::string& s, std::size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
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
  skipWs(s, i);
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

bool parseFlatObject(const std::string& text, std::map<std::string, std::string>& out) {
  std::size_t i = 0;
  skipWs(text, i);
  if (i >= text.size() || text[i++] != '{') return false;
  skipWs(text, i);
  if (i < text.size() && text[i] == '}') {
    ++i;
    skipWs(text, i);
    return i == text.size();
  }

  while (i < text.size()) {
    std::string key;
    std::string value;
    if (!parseJsonString(text, i, key)) return false;
    skipWs(text, i);
    if (i >= text.size() || text[i++] != ':') return false;
    if (!parseJsonString(text, i, value)) return false;
    out[key] = value;
    skipWs(text, i);
    if (i >= text.size()) return false;
    if (text[i] == '}') {
      ++i;
      skipWs(text, i);
      return i == text.size();
    }
    if (text[i++] != ',') return false;
  }
  return false;
}

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

bool readAll(const fs::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

bool writeCatalog(const fs::path& path, const std::map<std::string, std::string>& names) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << "{\n";
  std::size_t index = 0;
  for (const auto& [folder, display] : names) {
    out << "  \"" << escapeJson(folder) << "\": \"" << escapeJson(display) << "\"";
    if (++index < names.size()) out << ',';
    out << "\n";
  }
  out << "}\n";
  return static_cast<bool>(out);
}

} // namespace

LauncherCatalog prepareCatalog(const fs::path& gameRoot,
                               const std::vector<GameInfo>& games,
                               std::string& diagnostic) {
  LauncherCatalog catalog;
  catalog.listPath = gameRoot / "gamelist.json";
  catalog.imageRoot = gameRoot / "_image";
  std::error_code ec;
  fs::create_directories(catalog.imageRoot, ec);

  bool changed = false;
  if (fs::exists(catalog.listPath, ec)) {
    std::string text;
    if (!readAll(catalog.listPath, text) || !parseFlatObject(text, catalog.displayNames)) {
      catalog.valid = false;
      diagnostic = "gamelist.json parse failed; existing file was not overwritten";
      return catalog;
    }
  } else {
    changed = true;
  }

  for (const auto& game : games) {
    if (catalog.displayNames.find(game.folderName) == catalog.displayNames.end()) {
      catalog.displayNames[game.folderName] = game.folderName;
      changed = true;
    }
  }

  if (changed && !writeCatalog(catalog.listPath, catalog.displayNames)) {
    diagnostic = "gamelist.json write failed";
  } else if (diagnostic.empty()) {
    diagnostic = "catalog ready";
  }
  return catalog;
}

std::string displayNameFor(const GameInfo& game, const LauncherCatalog& catalog) {
  const auto it = catalog.displayNames.find(game.folderName);
  if (it != catalog.displayNames.end() && !it->second.empty()) return it->second;
  return game.folderName;
}

bool setDisplayName(const GameInfo& game,
                    LauncherCatalog& catalog,
                    const std::string& displayName,
                    std::string& diagnostic) {
  if (!catalog.valid) {
    diagnostic = "catalog invalid";
    return false;
  }
  const std::string value = displayName.empty() ? game.folderName : displayName;
  catalog.displayNames[game.folderName] = value;
  if (!writeCatalog(catalog.listPath, catalog.displayNames)) {
    diagnostic = "gamelist.json write failed";
    return false;
  }
  diagnostic = "catalog name saved";
  return true;
}

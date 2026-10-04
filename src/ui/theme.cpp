#include "ui/theme.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/log.h"

namespace overlay::ui {

namespace {

struct StyleEntry {
  std::map<std::string, Color> colors;
  std::map<std::string, float> numbers;
};

StyleEntry g_global_entry;
std::map<std::string, StyleEntry> g_class_entries;
std::map<std::string, StyleEntry> g_id_entries;

std::string trim(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return text.substr(begin, end - begin);
}

std::string lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

bool parse_channel(const std::string& text, float& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  const float value = std::strtof(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0') return false;
  out = value > 1.0f ? value / 255.0f : value;
  return true;
}

std::vector<std::string> split(const std::string& text, char separator) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    const size_t position = text.find(separator, start);
    if (position == std::string::npos) {
      parts.push_back(text.substr(start));
      break;
    }
    parts.push_back(text.substr(start, position - start));
    start = position + 1;
  }
  return parts;
}

bool parse_hex(const std::string& text, Color& out) {
  std::string hex = text.substr(1);
  const size_t length = hex.size();
  if (length != 3 && length != 6 && length != 8) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::vector<int> values;
  if (length == 3) {
    for (char c : hex) {
      const int value = nibble(c);
      if (value < 0) return false;
      values.push_back(value * 17);
    }
    values.push_back(255);
  } else {
    for (size_t i = 0; i < length; i += 2) {
      const int high = nibble(hex[i]);
      const int low = nibble(hex[i + 1]);
      if (high < 0 || low < 0) return false;
      values.push_back(high * 16 + low);
    }
    if (length == 6) values.push_back(255);
  }
  out = {values[0] / 255.0f, values[1] / 255.0f, values[2] / 255.0f, values[3] / 255.0f};
  return true;
}

bool parse_color_value(const std::string& text, Color& out) {
  if (!text.empty() && text[0] == '#') return parse_hex(text, out);
  const size_t open = text.find('(');
  const size_t close = text.rfind(')');
  if (open == std::string::npos || close == std::string::npos || close < open) return false;
  const std::string name = lower(trim(text.substr(0, open)));
  if (name != "rgb" && name != "rgba") return false;
  const std::vector<std::string> parts = split(text.substr(open + 1, close - open - 1), ',');
  if (parts.size() != 3 && parts.size() != 4) return false;
  out = {0, 0, 0, 1};
  float* channels[4] = {&out.r, &out.g, &out.b, &out.a};
  for (size_t i = 0; i < parts.size(); ++i) {
    if (!parse_channel(trim(parts[i]), *channels[i])) return false;
  }
  return true;
}

bool is_color_property(const std::string& property) {
  static const char* kProperties[] = {"accent",   "track",     "knob",       "text",     "text-dim",
                                      "panel",    "border",    "window-bg",  "row-hover", "nav-selected",
                                      "button",   "button-hover", "field"};
  for (const char* name : kProperties)
    if (property == name) return true;
  return false;
}

void store_entry(StyleEntry& entry, const std::string& property, const std::string& value, int line) {
  if (property == "rounding") {
    char* end = nullptr;
    const float number = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
      log("style: line %d: bad number '%s'", line, value.c_str());
      return;
    }
    entry.numbers[property] = number;
    return;
  }
  if (!is_color_property(property)) return;
  Color color;
  if (!parse_color_value(value, color)) {
    log("style: line %d: bad color '%s'", line, value.c_str());
    return;
  }
  entry.colors[property] = color;
}

bool resolve(const std::string& class_name, const std::string& id, const std::string& property, Color fallback,
             Color& out) {
  if (!id.empty()) {
    const auto found = g_id_entries.find(id);
    if (found != g_id_entries.end()) {
      const auto color = found->second.colors.find(property);
      if (color != found->second.colors.end()) {
        out = color->second;
        return true;
      }
    }
  }
  const auto klass = g_class_entries.find(class_name);
  if (klass != g_class_entries.end()) {
    const auto color = klass->second.colors.find(property);
    if (color != klass->second.colors.end()) {
      out = color->second;
      return true;
    }
  }
  const auto global = g_global_entry.colors.find(property);
  if (global != g_global_entry.colors.end()) {
    out = global->second;
    return true;
  }
  out = fallback;
  return false;
}

bool resolve_number(const std::string& class_name, const std::string& id, const std::string& property, float fallback,
                    float& out) {
  if (!id.empty()) {
    const auto found = g_id_entries.find(id);
    if (found != g_id_entries.end()) {
      const auto number = found->second.numbers.find(property);
      if (number != found->second.numbers.end()) {
        out = number->second;
        return true;
      }
    }
  }
  const auto klass = g_class_entries.find(class_name);
  if (klass != g_class_entries.end()) {
    const auto number = klass->second.numbers.find(property);
    if (number != klass->second.numbers.end()) {
      out = number->second;
      return true;
    }
  }
  const auto global = g_global_entry.numbers.find(property);
  if (global != g_global_entry.numbers.end()) {
    out = global->second;
    return true;
  }
  out = fallback;
  return false;
}

}  // namespace

Theme& theme() {
  static Theme instance;
  return instance;
}

Color theme_color(const char* property, Color fallback) {
  Color out = fallback;
  resolve("", "", property, fallback, out);
  return out;
}

float theme_rounding(float fallback) {
  float out = fallback;
  resolve_number("", "", "rounding", fallback, out);
  return out;
}

Color style_color(const Widget& widget, const char* property, Color fallback) {
  Color out = fallback;
  resolve(widget_class_name(widget.klass), widget.id, property, theme_color(property, fallback), out);
  return out;
}

float style_rounding(const Widget& widget, float fallback) {
  float out = fallback;
  resolve_number(widget_class_name(widget.klass), widget.id, "rounding", theme_rounding(fallback), out);
  return out;
}

void style_clear() {
  g_global_entry = {};
  g_class_entries.clear();
  g_id_entries.clear();
}

bool style_parse(const char* css) {
  if (!css) return false;
  style_clear();

  std::string source(css);
  for (size_t position = source.find("/*"); position != std::string::npos; position = source.find("/*")) {
    const size_t end = source.find("*/", position + 2);
    if (end == std::string::npos) break;
    source.erase(position, end - position + 2);
  }

  int line = 1;
  size_t cursor = 0;
  while (cursor < source.size()) {
    const size_t open = source.find('{', cursor);
    if (open == std::string::npos) {
      const std::string trailing = trim(source.substr(cursor));
      if (!trailing.empty()) {
        log("style: line %d: expected '{'", line);
        return false;
      }
      break;
    }
    const size_t close = source.find('}', open);
    if (close == std::string::npos) {
      log("style: line %d: missing '}'", line);
      return false;
    }

    const std::string selector = trim(source.substr(cursor, open - cursor));
    const int rule_line = line;
    for (size_t i = cursor; i < open; ++i)
      if (source[i] == '\n') ++line;

    StyleEntry* entry = nullptr;
    if (selector == "menu") {
      entry = &g_global_entry;
    } else if (!selector.empty() && selector[0] == '#') {
      entry = &g_id_entries[selector.substr(1)];
    } else if (!selector.empty() && (std::isalpha(static_cast<unsigned char>(selector[0])) || selector[0] == '-' ||
                                     selector[0] == '_')) {
      entry = &g_class_entries[lower(selector)];
    } else {
      log("style: line %d: bad selector '%s'", rule_line, selector.c_str());
      return false;
    }

    for (const std::string& declaration : split(source.substr(open + 1, close - open - 1), ';')) {
      int declaration_line = rule_line;
      for (size_t i = open + 1; i < close; ++i)
        if (source[i] == '\n') ++declaration_line;
      const std::string text = trim(declaration);
      if (text.empty()) continue;
      const size_t colon = text.find(':');
      if (colon == std::string::npos) {
        log("style: line %d: expected 'property: value'", declaration_line);
        return false;
      }
      store_entry(*entry, lower(trim(text.substr(0, colon))), trim(text.substr(colon + 1)), declaration_line);
    }

    cursor = close + 1;
  }
  return true;
}

bool style_load_file(const std::wstring& path) {
  FILE* file = _wfopen(path.c_str(), L"rb");
  if (!file) return false;
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size <= 0 || size > 1024 * 1024) {
    std::fclose(file);
    return false;
  }
  std::string text(static_cast<size_t>(size), '\0');
  const size_t read = std::fread(text.data(), 1, text.size(), file);
  std::fclose(file);
  if (read != text.size()) return false;
  const bool ok = style_parse(text.c_str());
  log("style: loaded %ls (%s)", path.c_str(), ok ? "ok" : "parse error");
  return ok;
}

}  // namespace overlay::ui

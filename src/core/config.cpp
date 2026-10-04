#include "core/config.h"

#include <windows.h>
#include <yyjson.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "core/log.h"
#include "core/paths.h"
#include "ui/menu_model.h"
#include "ui/widget.h"

namespace overlay::config {

namespace {

constexpr size_t kMaxNameLength = 64;
constexpr long kMaxFileBytes = 4 * 1024 * 1024;

bool valid_name(const char* name) {
  if (!name || !*name) return false;
  size_t length = 0;
  for (const char* c = name; *c; ++c, ++length) {
    const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' ||
                    *c == '-';
    if (!ok) return false;
    if (length >= kMaxNameLength) return false;
  }
  return true;
}

std::wstring directory() { return overlay::module_directory() + L"configs"; }

std::wstring file_path(const char* name) {
  std::wstring wide(name, name + std::strlen(name));
  return directory() + L"\\" + wide + L".json";
}

bool ensure_directory() {
  const std::wstring dir = directory();
  if (CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
  log("config: cannot create %ls", dir.c_str());
  return false;
}

bool read_file(const std::wstring& path, std::string& out) {
  FILE* file = _wfopen(path.c_str(), L"rb");
  if (!file) return false;
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size < 0 || size > kMaxFileBytes) {
    std::fclose(file);
    return false;
  }
  out.resize(static_cast<size_t>(size));
  const size_t read = size > 0 ? std::fread(out.data(), 1, out.size(), file) : 0;
  std::fclose(file);
  return read == out.size();
}

bool write_file(const std::wstring& path, const char* data, size_t size) {
  FILE* file = _wfopen(path.c_str(), L"wb");
  if (!file) return false;
  const size_t written = std::fwrite(data, 1, size, file);
  std::fclose(file);
  return written == size;
}

void add_value(yyjson_mut_doc* doc, yyjson_mut_val* values, const std::string& id, const overlay::ui::Widget& widget) {
  if (const bool* v = std::get_if<bool>(&widget.value)) {
    yyjson_mut_obj_add_bool(doc, values, id.c_str(), *v);
  } else if (const float* v = std::get_if<float>(&widget.value)) {
    yyjson_mut_obj_add_real(doc, values, id.c_str(), static_cast<double>(*v));
  } else if (const int* v = std::get_if<int>(&widget.value)) {
    yyjson_mut_obj_add_int(doc, values, id.c_str(), *v);
  } else if (const std::string* v = std::get_if<std::string>(&widget.value)) {
    yyjson_mut_obj_add_str(doc, values, id.c_str(), v->c_str());
  } else if (const overlay::ui::Color* v = std::get_if<overlay::ui::Color>(&widget.value)) {
    yyjson_mut_val* array = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_real(doc, array, static_cast<double>(v->r));
    yyjson_mut_arr_add_real(doc, array, static_cast<double>(v->g));
    yyjson_mut_arr_add_real(doc, array, static_cast<double>(v->b));
    yyjson_mut_arr_add_real(doc, array, static_cast<double>(v->a));
    yyjson_mut_obj_add_val(doc, values, id.c_str(), array);
  } else if (const overlay::ui::KeyCombo* v = std::get_if<overlay::ui::KeyCombo>(&widget.value)) {
    yyjson_mut_obj_add_int(doc, values, id.c_str(), v->code);
  }
}

void apply_value(overlay::ui::Widget& widget, yyjson_val* value) {
  if (bool* target = std::get_if<bool>(&widget.value)) {
    if (yyjson_is_bool(value)) *target = yyjson_get_bool(value);
  } else if (float* target = std::get_if<float>(&widget.value)) {
    if (yyjson_is_num(value)) *target = static_cast<float>(yyjson_get_num(value));
  } else if (int* target = std::get_if<int>(&widget.value)) {
    if (yyjson_is_num(value)) *target = static_cast<int>(yyjson_get_num(value));
  } else if (std::string* target = std::get_if<std::string>(&widget.value)) {
    if (yyjson_is_str(value)) *target = yyjson_get_str(value);
  } else if (overlay::ui::Color* target = std::get_if<overlay::ui::Color>(&widget.value)) {
    if (yyjson_is_arr(value) && yyjson_arr_size(value) >= 4) {
      target->r = static_cast<float>(yyjson_get_num(yyjson_arr_get(value, 0)));
      target->g = static_cast<float>(yyjson_get_num(yyjson_arr_get(value, 1)));
      target->b = static_cast<float>(yyjson_get_num(yyjson_arr_get(value, 2)));
      target->a = static_cast<float>(yyjson_get_num(yyjson_arr_get(value, 3)));
    }
  } else if (overlay::ui::KeyCombo* target = std::get_if<overlay::ui::KeyCombo>(&widget.value)) {
    if (yyjson_is_num(value)) target->code = static_cast<int>(yyjson_get_num(value));
  }
}

template <typename Fn>
void for_each_widget(Fn fn) {
  for (overlay::ui::Page& page : overlay::ui::menu().pages)
    for (overlay::ui::Section& section : page.sections)
      for (overlay::ui::Widget& widget : section.widgets) fn(widget);
}

}  // namespace

bool save(const char* name) {
  if (!valid_name(name)) {
    log("config: invalid name");
    return false;
  }
  if (!ensure_directory()) return false;

  std::map<std::string, const overlay::ui::Widget*> widgets;
  for_each_widget([&](overlay::ui::Widget& widget) { widgets[widget.id] = &widget; });

  yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
  if (!doc) return false;
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);
  yyjson_mut_obj_add_int(doc, root, "version", 1);
  yyjson_mut_val* values = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, root, "values", values);
  for (const auto& entry : widgets) add_value(doc, values, entry.first, *entry.second);

  size_t size = 0;
  char* json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &size);
  yyjson_mut_doc_free(doc);
  if (!json) return false;

  const bool ok = write_file(file_path(name), json, size);
  free(json);
  if (!ok) log("config: failed to write '%s'", name);
  return ok;
}

bool load(const char* name) {
  if (!valid_name(name)) {
    log("config: invalid name");
    return false;
  }
  std::string text;
  if (!read_file(file_path(name), text)) {
    log("config: '%s' not found or unreadable", name);
    return false;
  }

  yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
  if (!doc) {
    log("config: '%s' is corrupt, ignoring", name);
    return false;
  }
  yyjson_val* root = yyjson_doc_get_root(doc);
  yyjson_val* values = root ? yyjson_obj_get(root, "values") : nullptr;
  if (!values || !yyjson_is_obj(values)) {
    yyjson_doc_free(doc);
    log("config: '%s' has no values object", name);
    return false;
  }

  std::map<std::string, yyjson_val*> parsed;
  yyjson_obj_iter iter = yyjson_obj_iter_with(values);
  yyjson_val* key = nullptr;
  while ((key = yyjson_obj_iter_next(&iter))) {
    const char* id = yyjson_get_str(key);
    if (id) parsed[id] = yyjson_obj_iter_get_val(key);
  }

  for_each_widget([&](overlay::ui::Widget& widget) {
    const auto found = parsed.find(widget.id);
    if (found != parsed.end()) apply_value(widget, found->second);
  });

  yyjson_doc_free(doc);
  log("config: loaded '%s'", name);
  return true;
}

bool remove(const char* name) {
  if (!valid_name(name)) return false;
  return DeleteFileW(file_path(name).c_str()) != 0;
}

void list(void (*callback)(void* user, const char* name), void* user) {
  if (!callback) return;
  const std::wstring pattern = directory() + L"\\*.json";
  WIN32_FIND_DATAW data = {};
  HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
  if (handle == INVALID_HANDLE_VALUE) return;
  do {
    std::wstring filename = data.cFileName;
    const size_t dot = filename.find_last_of(L'.');
    if (dot == std::wstring::npos) continue;
    filename.resize(dot);
    if (filename.empty()) continue;
    char narrow[128] = {};
    const int length = WideCharToMultiByte(CP_ACP, 0, filename.c_str(), static_cast<int>(filename.size()), narrow,
                                           sizeof(narrow) - 1, nullptr, nullptr);
    if (length <= 0) continue;
    narrow[length] = '\0';
    callback(user, narrow);
  } while (FindNextFileW(handle, &data));
  FindClose(handle);
}

std::string active_name() {
  std::string name = "default";
  for_each_widget([&](overlay::ui::Widget& widget) {
    if (widget.id == "config_name") {
      if (const std::string* value = std::get_if<std::string>(&widget.value)) name = *value;
    }
  });
  return name;
}

}  // namespace overlay::config

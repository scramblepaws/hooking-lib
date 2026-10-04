#include "core/paths.h"

#include <windows.h>

#include <cstdint>

namespace overlay {

std::wstring module_directory() {
  HMODULE self = nullptr;
  const auto address = reinterpret_cast<uintptr_t>(&module_directory);
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(address), &self);
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(self, path, MAX_PATH);
  std::wstring dir(path);
  const size_t slash = dir.find_last_of(L"\\/");
  return slash == std::wstring::npos ? dir : dir.substr(0, slash + 1);
}

}  // namespace overlay

#include "core/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "core/paths.h"

namespace overlay {

namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;

FILE* file() {
  if (g_file) return g_file;
  g_file = _wfopen((module_directory() + L"overlay.log").c_str(), L"a");
  return g_file;
}

}  // namespace

void log(const char* fmt, ...) {
  std::lock_guard<std::mutex> lock(g_mutex);
  FILE* f = file();
  if (!f) return;
  SYSTEMTIME t = {};
  GetLocalTime(&t);
  std::fprintf(f, "[%02u:%02u:%02u] ", t.wHour, t.wMinute, t.wSecond);
  va_list args;
  va_start(args, fmt);
  std::vfprintf(f, fmt, args);
  va_end(args);
  std::fputc('\n', f);
  std::fflush(f);
}

}  // namespace overlay

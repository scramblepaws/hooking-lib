#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <initializer_list>

namespace {

bool g_wait = true;

bool stdin_is_console() {
  HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  return handle && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode);
}

void pause_if_console() {
  if (!g_wait || !stdin_is_console()) return;
  std::printf("\nPress Enter to exit...");
  std::fflush(stdout);
  char line[8];
  std::fgets(line, sizeof(line), stdin);
}

int finish(int code, const char* message = nullptr) {
  if (message) std::printf("%s%s\n", code ? "[!] " : "", message);
  pause_if_console();
  return code;
}

int fail(const char* hint) {
  std::printf("FAILED (error %lu)\n", GetLastError());
  return finish(1, hint);
}

void print_target_name(HANDLE process) {
  wchar_t path[MAX_PATH] = {};
  DWORD size = MAX_PATH;
  if (QueryFullProcessImageNameW(process, 0, path, &size))
    std::printf(" -> %ls\n", path);
  else
    std::printf(" (name unavailable)\n");
}

bool read_line(const char* prompt, char* out, size_t capacity) {
  std::printf("%s", prompt);
  std::fflush(stdout);
  if (!std::fgets(out, static_cast<int>(capacity), stdin)) return false;
  size_t length = std::strlen(out);
  while (length && (out[length - 1] == '\n' || out[length - 1] == '\r')) out[--length] = '\0';
  return true;
}

bool is_digits(const char* text) {
  if (!*text) return false;
  for (; *text; ++text)
    if (*text < '0' || *text > '9') return false;
  return true;
}

DWORD find_process_by_name(const char* name) {
  wchar_t wide[128] = {};
  if (!MultiByteToWideChar(CP_ACP, 0, name, -1, wide, 128)) return 0;
  if (!std::wcschr(wide, L'.')) std::wcsncat(wide, L".exe", 128 - std::wcslen(wide) - 1);
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W entry = {};
  entry.dwSize = sizeof(entry);
  DWORD pid = 0;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (CompareStringOrdinal(entry.szExeFile, -1, wide, -1, TRUE) == CSTR_EQUAL) {
        pid = entry.th32ProcessID;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return pid;
}

bool default_dll_next_to_injector(char* out, size_t capacity) {
  if (!GetModuleFileNameA(nullptr, out, static_cast<DWORD>(capacity))) return false;
  char* slash = std::strrchr(out, '\\');
  if (!slash) return false;
  slash[1] = '\0';
  const size_t base = std::strlen(out);
  for (const char* name : {"liboverlay.dll", "overlay.dll"}) {
    std::snprintf(out + base, capacity - base, "%s", name);
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
  }
  out[base] = '\0';
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("overlay injector\n");

  int arg = 1;
  if (arg < argc && std::strcmp(argv[arg], "--no-wait") == 0) {
    g_wait = false;
    ++arg;
  }

  char target[260] = {};
  char dll[MAX_PATH] = {};
  if (argc - arg >= 1) std::snprintf(target, sizeof(target), "%s", argv[arg]);
  if (argc - arg >= 2) std::snprintf(dll, sizeof(dll), "%s", argv[arg + 1]);

  if (!target[0]) {
    if (!stdin_is_console()) {
      std::printf("usage: injector [--no-wait] <pid or exe name> [path-to-overlay.dll]\n");
      return 2;
    }
    if (!read_line("target process (pid or exe name): ", target, sizeof(target)) || !target[0])
      return finish(2, "no target given");
  }

  DWORD pid = 0;
  if (is_digits(target)) {
    pid = static_cast<DWORD>(std::strtoul(target, nullptr, 10));
  } else {
    pid = find_process_by_name(target);
    if (!pid) return finish(1, "process not found - is it running? pass the exe name or a pid");
  }
  std::printf("[1/6] target pid %lu (%s)\n", pid, target);

  if (!dll[0] && !default_dll_next_to_injector(dll, sizeof(dll))) {
    if (!stdin_is_console() ||
        !read_line("path to overlay dll: ", dll, sizeof(dll)) || !dll[0])
      return finish(1, "no dll given and none found next to the injector");
  }

  char full[MAX_PATH] = {};
  if (!GetFullPathNameA(dll, MAX_PATH, full, nullptr)) return fail("could not resolve the dll path");
  std::printf("[2/6] dll: %s\n", full);

  std::printf("[3/6] opening target ... ");
  HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                   PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, pid);
  if (!process) return fail("OpenProcess failed - wrong pid? elevated target needs an elevated injector");
  print_target_name(process);
  std::fflush(stdout);

  const SIZE_T size = std::strlen(full) + 1;
  void* remote = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!remote) return fail("VirtualAllocEx failed");
  std::printf("[4/6] remote memory at 0x%p, writing dll path ... ", remote);
  if (!WriteProcessMemory(process, remote, full, size, nullptr)) return fail("WriteProcessMemory failed");
  std::printf("ok\n");

  auto load_library =
      reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryA"));
  if (!load_library) return fail("GetProcAddress(LoadLibraryA) failed");
  std::printf("[5/6] LoadLibraryA at 0x%p, spawning remote thread ... ", reinterpret_cast<void*>(load_library));
  HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr);
  if (!thread) return fail("CreateRemoteThread failed");
  std::printf("ok, waiting for it to return ...\n");

  if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0) return fail("remote LoadLibraryA did not finish in 10s");

  DWORD module = 0;
  GetExitCodeThread(thread, &module);
  if (!module) {
    std::printf("[6/6] LoadLibraryA returned 0 inside the target\n");
    return fail("check the dll exists, is 64-bit, and matches the target");
  }
  std::printf("[6/6] injected: module base 0x%llx\n", static_cast<unsigned long long>(module));

  CloseHandle(thread);
  VirtualFreeEx(process, remote, 0, MEM_RELEASE);
  CloseHandle(process);
  return finish(0, "done - the target should render the overlay (Del = menu, End = eject)");
}

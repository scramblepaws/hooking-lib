#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <string>

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
  for (const char* name : {"overlay.dll", "liboverlay.dll"}) {
    std::snprintf(out + base, capacity - base, "%s", name);
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
  }
  out[base] = '\0';
  return false;
}

bool resolve_dll(const char* requested, char* out, size_t capacity) {
  char chosen[MAX_PATH] = {};
  if (requested && requested[0]) {
    std::snprintf(chosen, sizeof(chosen), "%s", requested);
  } else if (!default_dll_next_to_injector(chosen, sizeof(chosen))) {
    if (!stdin_is_console()) {
      std::printf("no overlay dll given and none found next to the injector\n");
      return false;
    }
    if (!read_line("path to overlay dll: ", chosen, sizeof(chosen)) || !chosen[0]) return false;
  }
  if (!GetFullPathNameA(chosen, static_cast<DWORD>(capacity), out, nullptr)) return false;
  if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES) {
    std::printf("[2/6] dll not found: %s\n", out);
    return false;
  }
  std::printf("[2/6] dll: %s\n", out);
  return true;
}

bool overlay_ready_event_exists(DWORD pid) {
  char name_narrow[64] = {};
  std::snprintf(name_narrow, sizeof(name_narrow), "overlay_ready_%lu", pid);
  wchar_t name[64] = {};
  MultiByteToWideChar(CP_ACP, 0, name_narrow, -1, name, 64);
  HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, name);
  if (!event) return false;
  CloseHandle(event);
  return true;
}

// Steps 3-6: allocate, write the path, LoadLibraryA remotely, report the base.
bool inject_into(HANDLE process, const char* full_dll) {
  std::printf("[3/6] opening target ... ");
  print_target_name(process);
  std::fflush(stdout);

  const SIZE_T size = std::strlen(full_dll) + 1;
  void* remote = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!remote) {
    fail("VirtualAllocEx failed");
    return false;
  }
  std::printf("[4/6] remote memory at 0x%p, writing dll path ... ", remote);
  if (!WriteProcessMemory(process, remote, full_dll, size, nullptr)) {
    fail("WriteProcessMemory failed");
    return false;
  }
  std::printf("ok\n");

  auto load_library =
      reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryA"));
  if (!load_library) {
    fail("GetProcAddress(LoadLibraryA) failed");
    return false;
  }
  std::printf("[5/6] LoadLibraryA at 0x%p, spawning remote thread ... ", reinterpret_cast<void*>(load_library));
  HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr);
  if (!thread) {
    fail("CreateRemoteThread failed");
    return false;
  }
  std::printf("ok, waiting for it to return ...\n");

  if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0) {
    fail("remote LoadLibraryA did not finish in 10s");
    return false;
  }

  DWORD module = 0;
  GetExitCodeThread(thread, &module);
  CloseHandle(thread);
  VirtualFreeEx(process, remote, 0, MEM_RELEASE);
  if (!module) {
    std::printf("[6/6] LoadLibraryA returned 0 inside the target\n");
    fail("check the dll exists, is 64-bit, and matches the target");
    return false;
  }
  std::printf("[6/6] injected: module base 0x%llx\n", static_cast<unsigned long long>(module));
  return true;
}

void wait_for_overlay_ready(DWORD pid) {
  char name_narrow[64] = {};
  std::snprintf(name_narrow, sizeof(name_narrow), "overlay_ready_%lu", pid);
  wchar_t name[64] = {};
  MultiByteToWideChar(CP_ACP, 0, name_narrow, -1, name, 64);

  HANDLE event = nullptr;
  for (int i = 0; i < 100 && !event; ++i) {
    event = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (!event) Sleep(50);
  }
  if (!event) {
    std::printf("[!] overlay did not signal readiness; continuing\n");
    return;
  }
  WaitForSingleObject(event, 10000);
  CloseHandle(event);
  std::printf("     overlay hooks installed\n");
}

std::string quoted(const char* text) {
  if (!std::strchr(text, ' ')) return text;
  std::string out = "\"";
  out += text;
  out += "\"";
  return out;
}

int run_launch(int argc, char** argv, int arg) {
  if (arg >= argc) return finish(2, "usage: injector --launch <exe> [args...]");

  char exe[MAX_PATH] = {};
  if (!GetFullPathNameA(argv[arg], MAX_PATH, exe, nullptr)) return fail("could not resolve the exe path");
  if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) {
    std::printf("exe not found: %s\n", exe);
    return finish(1);
  }

  char dll[MAX_PATH] = {};
  if (!resolve_dll(nullptr, dll, sizeof(dll))) return finish(1);

  std::string command = quoted(exe);
  for (int i = arg + 1; i < argc; ++i) {
    command += ' ';
    command += quoted(argv[i]);
  }
  std::printf("[0/6] launching suspended: %s\n", command.c_str());

  STARTUPINFOA startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info = {};
  if (!CreateProcessA(exe, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &startup,
                      &info))
    return fail("CreateProcess failed");

  std::printf("[1/6] target pid %lu (suspended)\n", info.dwProcessId);
  if (!inject_into(info.hProcess, dll)) {
    TerminateProcess(info.hProcess, 1);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return finish(1, "injection failed; the target was never resumed");
  }
  wait_for_overlay_ready(info.dwProcessId);
  if (ResumeThread(info.hThread) == static_cast<DWORD>(-1)) {
    const DWORD error = GetLastError();
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    std::printf("target exited before it could be resumed (error %lu)\n", error);
    return finish(1);
  }
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  std::printf("done - target resumed with the overlay loaded\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("overlay injector\n");

  int arg = 1;
  if (arg < argc && std::strcmp(argv[arg], "--no-wait") == 0) {
    g_wait = false;
    ++arg;
  }
  if (arg < argc && std::strcmp(argv[arg], "--launch") == 0) return run_launch(argc, argv, arg + 1);

  char target[260] = {};
  char dll[MAX_PATH] = {};
  if (argc - arg >= 1) std::snprintf(target, sizeof(target), "%s", argv[arg]);
  if (argc - arg >= 2) std::snprintf(dll, sizeof(dll), "%s", argv[arg + 1]);

  if (!target[0]) {
    if (!stdin_is_console()) {
      std::printf("usage: injector [--no-wait] <pid or exe name> [path-to-overlay.dll]\n");
      std::printf("       injector [--no-wait] --launch <exe> [args...]\n");
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
  if (overlay_ready_event_exists(pid)) {
    std::printf("overlay is already injected into pid %lu - eject it first (End key), then re-inject\n", pid);
    return finish(1);
  }

  char full[MAX_PATH] = {};
  if (!resolve_dll(dll, full, sizeof(full))) return finish(1);

  HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                   PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, pid);
  if (!process) return fail("OpenProcess failed - wrong pid? elevated target needs an elevated injector");

  if (!inject_into(process, full)) {
    CloseHandle(process);
    return finish(1);
  }
  CloseHandle(process);
  return finish(0, "done - the target should render the overlay (Del = menu, End = eject)");
}

#include "core/salvage.h"

#include <windows.h>

#include <csetjmp>

#include "core/log.h"

namespace overlay::salvage {

namespace {

struct Frame {
  jmp_buf jump;
  Frame* prev;
};

thread_local Frame* g_frame = nullptr;
PVOID g_handler = nullptr;

LONG CALLBACK handler(EXCEPTION_POINTERS* info) {
  // Only salvage faults raised on a thread currently inside a firewalled call.
  // Anything else (game threads, our own hook plumbing) crashes as usual. The
  // faulting code can be the host's (painters, callbacks), so no module check.
  if (!g_frame) return EXCEPTION_CONTINUE_SEARCH;
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
      code != EXCEPTION_INT_DIVIDE_BY_ZERO)
    return EXCEPTION_CONTINUE_SEARCH;
  log("salvage: contained exception 0x%08lx at %p", code,
      reinterpret_cast<void*>(info->ExceptionRecord->ExceptionAddress));
  Frame* frame = g_frame;
  g_frame = frame->prev;
  longjmp(frame->jump, 1);
  return EXCEPTION_CONTINUE_SEARCH;  // unreachable
}

}  // namespace

void install() {
  if (!g_handler) g_handler = AddVectoredExceptionHandler(1, handler);
}

void remove() {
  if (g_handler) {
    RemoveVectoredExceptionHandler(g_handler);
    g_handler = nullptr;
  }
}

bool run(void (*fn)(void*), void* user) {
  Frame frame;
  frame.prev = g_frame;
  g_frame = &frame;
  if (setjmp(frame.jump) != 0) return false;  // the handler already popped the frame

  bool ok = true;
  try {
    fn(user);
  } catch (...) {
    ok = false;
  }
  g_frame = frame.prev;
  return ok;
}

}  // namespace overlay::salvage

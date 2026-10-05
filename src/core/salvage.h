#pragma once

namespace overlay::salvage {

// Installs a first-chance vectored exception handler used by run() to contain
// faults raised inside this module. Safe to call repeatedly.
void install();
void remove();

// Runs fn with containment: an access violation or similar raised inside this
// module's code unwinds back here and returns false, leaving the process alive.
// Returns true when fn completed.
bool run(void (*fn)(void*), void* user);

}  // namespace overlay::salvage

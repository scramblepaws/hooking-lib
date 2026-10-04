#pragma once

namespace overlay::ui {
class Canvas;
}

namespace overlay::script {

// Creates the VM, applies the sandbox and loads <module dir>/scripts/*.lua. Idempotent.
void start();

// Advances the scheduler, runs button callbacks, picks up hot reloads.
void tick(double now);

// Runs one chunk on the host VM (used by the C API and tests).
bool execute(const char* source, const char* chunk_name);

// Queues a full teardown + reload of every script on the next tick.
void request_reload();

// Paints script Drawing objects (ESP primitives) into the given canvas.
void render_drawing(overlay::ui::Canvas& canvas);

}  // namespace overlay::script

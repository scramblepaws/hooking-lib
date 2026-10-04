#pragma once

namespace overlay::render {

// Decodes an image file (PNG/JPG/BMP/GIF via WIC) into a backend texture and
// caches it by path. Relative paths resolve against the scripts folder.
// Returns an ImTextureID-compatible handle, or null when unsupported
// (non-DX11 backend) or the file cannot be read.
void* texture_for(const char* utf8_path);

// Releases every cached texture (called on render shutdown/device loss).
void release_textures();

}  // namespace overlay::render

#define INITGUID
#include <windows.h>
#include <wincodec.h>

#include <d3d11.h>

#include <map>
#include <string>
#include <vector>

#include "core/paths.h"
#include "render/dx11.h"
#include "render/dx12.h"
#include "render/textures.h"
#include "render/vulkan.h"

namespace overlay::render {

namespace {

struct Entry {
  void* texture = nullptr;
  unsigned long long retry_at = 0;
};

std::map<std::string, Entry> g_cache;

// Missing or broken paths are retried at most this often, never per frame.
constexpr unsigned long long kRetryMs = 2000;

std::wstring to_wide(const char* utf8) {
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  if (length <= 1) return {};
  std::wstring wide(static_cast<size_t>(length - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), length);
  return wide;
}

std::wstring resolve_path(const char* path) {
  std::wstring wide = to_wide(path);
  if (wide.size() >= 2 && wide[1] == L':') return wide;  // absolute drive path
  if (!wide.empty() && (wide[0] == L'\\' || wide[0] == L'/')) return wide;
  return module_directory() + L"scripts\\" + wide;
}

bool decode_rgba(const std::wstring& path, std::vector<unsigned char>& rgba, uint32_t& width, uint32_t& height) {
  // WIC needs an apartment; the Target's render thread may never have called
  // CoInitializeEx itself. RPC_E_CHANGED_MODE means COM is already up (MTA).
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool owned_com = SUCCEEDED(com);

  IWICImagingFactory* factory = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICFormatConverter* converter = nullptr;
  bool ok = false;
  if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
      factory && SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &decoder)) &&
      decoder && SUCCEEDED(decoder->GetFrame(0, &frame)) && frame &&
      SUCCEEDED(factory->CreateFormatConverter(&converter)) && converter &&
      SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                      WICBitmapPaletteTypeCustom))) {
    converter->GetSize(&width, &height);
    rgba.resize(static_cast<size_t>(width) * height * 4);
    ok = width && height &&
         SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(rgba.size()), rgba.data()));
  }

  if (converter) converter->Release();
  if (frame) frame->Release();
  if (decoder) decoder->Release();
  if (factory) factory->Release();
  if (owned_com) CoUninitialize();
  return ok;
}

void* upload_dx11(ID3D11Device* device, const void* rgba, uint32_t width, uint32_t height) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data = {};
  data.pSysMem = rgba;
  data.SysMemPitch = width * 4;
  ID3D11Texture2D* texture = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, &data, &texture)) || !texture) return nullptr;

  D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
  view.Format = desc.Format;
  view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  view.Texture2D.MipLevels = 1;
  ID3D11ShaderResourceView* srv = nullptr;
  const HRESULT hr = device->CreateShaderResourceView(texture, &view, &srv);
  texture->Release();
  if (FAILED(hr)) return nullptr;
  return srv;
}

void* upload_texture(const void* rgba, uint32_t width, uint32_t height) {
  if (ID3D11Device* device = dx11::device()) return upload_dx11(device, rgba, width, height);
  if (dx12::ready()) return dx12::create_texture(rgba, width, height);
  if (vulkan::active()) return vulkan::create_texture(rgba, width, height);
  return nullptr;
}

}  // namespace

void* texture_for(const char* utf8_path) {
  if (!utf8_path || !*utf8_path) return nullptr;
  const unsigned long long now = GetTickCount64();
  const auto cached = g_cache.find(utf8_path);
  if (cached != g_cache.end()) {
    if (cached->second.texture) return cached->second.texture;
    if (now < cached->second.retry_at) return nullptr;
  }
  if (!dx11::device() && !dx12::ready() && !vulkan::active()) return nullptr;  // no upload path yet

  std::vector<unsigned char> rgba;
  uint32_t width = 0;
  uint32_t height = 0;
  void* texture = nullptr;
  if (decode_rgba(resolve_path(utf8_path), rgba, width, height))
    texture = upload_texture(rgba.data(), width, height);
  if (!texture) {
    g_cache[utf8_path] = Entry{nullptr, now + kRetryMs};
    return nullptr;
  }
  g_cache[utf8_path] = Entry{texture, 0};
  return texture;
}

void release_textures() {
  // DX11 SRVs belong to the cache; DX12 resources are owned by the backend and
  // released on backend shutdown.
  for (auto& entry : g_cache)
    if (entry.second.texture && dx11::device())
      static_cast<ID3D11ShaderResourceView*>(entry.second.texture)->Release();
  g_cache.clear();
}

}  // namespace overlay::render

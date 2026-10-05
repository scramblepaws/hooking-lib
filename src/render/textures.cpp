#define INITGUID
#include <windows.h>
#include <wincodec.h>

#include <d3d11.h>

#include <map>
#include <string>
#include <vector>

#include "core/paths.h"
#include "render/dx11.h"
#include "render/textures.h"

namespace overlay::render {

namespace {

struct Entry {
  ID3D11ShaderResourceView* srv = nullptr;
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

ID3D11ShaderResourceView* create_texture(ID3D11Device* device, const std::wstring& path) {
  // WIC needs an apartment; the Target's render thread may never have called
  // CoInitializeEx itself. RPC_E_CHANGED_MODE means COM is already up (MTA).
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool owned_com = SUCCEEDED(com);

  IWICImagingFactory* factory = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICFormatConverter* converter = nullptr;
  ID3D11Texture2D* texture = nullptr;
  ID3D11ShaderResourceView* srv = nullptr;

  UINT width = 0;
  UINT height = 0;
  std::vector<unsigned char> rgba;
  if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
      factory && SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &decoder)) &&
      decoder && SUCCEEDED(decoder->GetFrame(0, &frame)) && frame &&
      SUCCEEDED(factory->CreateFormatConverter(&converter)) && converter &&
      SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                      WICBitmapPaletteTypeCustom))) {
    converter->GetSize(&width, &height);
    rgba.resize(static_cast<size_t>(width) * height * 4);
    if (SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(rgba.size()), rgba.data())) && width &&
        height) {
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
      data.pSysMem = rgba.data();
      data.SysMemPitch = width * 4;
      if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)) && texture) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
        view.Format = desc.Format;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(texture, &view, &srv))) srv = nullptr;
      }
    }
  }

  if (texture) texture->Release();
  if (converter) converter->Release();
  if (frame) frame->Release();
  if (decoder) decoder->Release();
  if (factory) factory->Release();
  if (owned_com) CoUninitialize();
  return srv;
}

}  // namespace

void* texture_for(const char* utf8_path) {
  if (!utf8_path || !*utf8_path) return nullptr;
  const unsigned long long now = GetTickCount64();
  const auto cached = g_cache.find(utf8_path);
  if (cached != g_cache.end()) {
    if (cached->second.srv) return cached->second.srv;
    if (now < cached->second.retry_at) return nullptr;
  }

  ID3D11Device* device = dx11::device();
  if (!device) return nullptr;  // non-DX11 backends: images are deferred

  ID3D11ShaderResourceView* srv = create_texture(device, resolve_path(utf8_path));
  if (!srv) {
    g_cache[utf8_path] = Entry{nullptr, now + kRetryMs};
    return nullptr;
  }
  g_cache[utf8_path] = Entry{srv, 0};
  return srv;
}

void release_textures() {
  for (auto& entry : g_cache)
    if (entry.second.srv) entry.second.srv->Release();
  g_cache.clear();
}

}  // namespace overlay::render

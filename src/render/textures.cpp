#define INITGUID
#include <windows.h>
#include <wincodec.h>

#include <d3d11.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/paths.h"
#include "render/dx11.h"
#include "render/textures.h"

namespace overlay::render {

namespace {

std::map<std::string, ID3D11ShaderResourceView*> g_cache;

bool decode_image(const std::wstring& path, UINT& width, UINT& height, std::vector<unsigned char>& rgba) {
  IWICImagingFactory* factory = nullptr;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory))) ||
      !factory)
    return false;

  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICFormatConverter* converter = nullptr;
  bool ok = false;

  if (SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                   WICDecodeMetadataCacheOnDemand, &decoder)) &&
      decoder && SUCCEEDED(decoder->GetFrame(0, &frame)) && frame &&
      SUCCEEDED(factory->CreateFormatConverter(&converter)) && converter &&
      SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                      WICBitmapPaletteTypeCustom))) {
    converter->GetSize(&width, &height);
    rgba.resize(static_cast<size_t>(width) * height * 4);
    ok = SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(rgba.size()), rgba.data()));
  }

  if (converter) converter->Release();
  if (frame) frame->Release();
  if (decoder) decoder->Release();
  factory->Release();
  return ok;
}

std::wstring resolve_path(const char* path) {
  std::wstring wide(path, path + std::strlen(path));
  if (wide.size() >= 2 && wide[1] == L':') return wide;  // absolute drive path
  if (!wide.empty() && (wide[0] == L'\\' || wide[0] == L'/')) return wide;
  return module_directory() + L"scripts\\" + wide;
}

}  // namespace

void* texture_for(const char* utf8_path) {
  if (!utf8_path || !*utf8_path) return nullptr;
  const auto cached = g_cache.find(utf8_path);
  if (cached != g_cache.end()) return cached->second;

  ID3D11Device* device = dx11::device();
  if (!device) return nullptr;  // non-DX11 backends: images are deferred

  UINT width = 0;
  UINT height = 0;
  std::vector<unsigned char> rgba;
  if (!decode_image(resolve_path(utf8_path), width, height, rgba) || width == 0 || height == 0) return nullptr;

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

  ID3D11Texture2D* texture = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, &data, &texture)) || !texture) return nullptr;

  D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
  view.Format = desc.Format;
  view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  view.Texture2D.MipLevels = 1;
  ID3D11ShaderResourceView* srv = nullptr;
  const HRESULT hr = device->CreateShaderResourceView(texture, &view, &srv);
  texture->Release();
  if (FAILED(hr) || !srv) return nullptr;

  g_cache[utf8_path] = srv;
  return srv;
}

void release_textures() {
  for (auto& entry : g_cache)
    if (entry.second) entry.second->Release();
  g_cache.clear();
}

}  // namespace overlay::render

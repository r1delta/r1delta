#pragma once

#include "d3d11_device_child.h"
#include "d3d11_include.h"

namespace dxvk {

  class D3D11Device;

  template<typename D3D11Interface>
  class D3D11Shader : public D3D11DeviceChild<D3D11Interface> {
  public:

    D3D11Shader(D3D11Device* device)
    : D3D11DeviceChild<D3D11Interface>(device) { }

    ~D3D11Shader() { }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) final {
      *ppvObject = nullptr;

      if (riid == __uuidof(IUnknown)
       || riid == __uuidof(ID3D11DeviceChild)
       || riid == __uuidof(D3D11Interface)) {
        *ppvObject = ref(this);
        return S_OK;
      }

      DXVK_LOG_UNK_IFACE(riid);
      return E_NOINTERFACE;
    }

  };

  using D3D11VertexShader   = D3D11Shader<ID3D11VertexShader>;
  using D3D11HullShader     = D3D11Shader<ID3D11HullShader>;
  using D3D11DomainShader   = D3D11Shader<ID3D11DomainShader>;
  using D3D11GeometryShader = D3D11Shader<ID3D11GeometryShader>;
  using D3D11PixelShader    = D3D11Shader<ID3D11PixelShader>;
  using D3D11ComputeShader  = D3D11Shader<ID3D11ComputeShader>;

}

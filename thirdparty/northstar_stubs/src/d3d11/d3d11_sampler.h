#pragma once

#include "d3d11_device_child.h"
#include "d3d11_include.h"

namespace dxvk {

  class D3D11Device;

  class D3D11SamplerState : public D3D11StateObject<ID3D11SamplerState> {

  public:

    using DescType = D3D11_SAMPLER_DESC;

    D3D11SamplerState(
            D3D11Device*        device,
      const D3D11_SAMPLER_DESC& desc);
    ~D3D11SamplerState();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID  riid,
            void**  ppvObject) final;

    void STDMETHODCALLTYPE GetDesc(
            D3D11_SAMPLER_DESC* pDesc) final;

    static HRESULT NormalizeDesc(
            D3D11_SAMPLER_DESC* pDesc);

  private:

    D3D11_SAMPLER_DESC m_desc;

  };

}

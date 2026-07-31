#pragma once

#include "d3d11_include.h"
#include "d3d11_state.h"

namespace dxvk {

  class D3D11Device;

  class D3D11DepthStencilState : public D3D11StateObject<ID3D11DepthStencilState> {

  public:

    using DescType = D3D11_DEPTH_STENCIL_DESC;

    D3D11DepthStencilState(
            D3D11Device*              device,
      const D3D11_DEPTH_STENCIL_DESC& desc);
    ~D3D11DepthStencilState();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID  riid,
            void**  ppvObject) final;

    void STDMETHODCALLTYPE GetDesc(
            D3D11_DEPTH_STENCIL_DESC* pDesc) final;

  private:

    D3D11_DEPTH_STENCIL_DESC  m_desc;

  };
}

#pragma once

#include "d3d11_device_child.h"
#include "d3d11_include.h"

namespace dxvk {

  class D3D11Device;

  class D3D11BlendState : public D3D11StateObject<ID3D11BlendState1> {

  public:

    using DescType = D3D11_BLEND_DESC1;

    D3D11BlendState(
            D3D11Device*       device,
      const D3D11_BLEND_DESC1& desc);
    ~D3D11BlendState();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID  riid,
            void**  ppvObject) final;

    void STDMETHODCALLTYPE GetDesc(
            D3D11_BLEND_DESC* pDesc) final;

    void STDMETHODCALLTYPE GetDesc1(
            D3D11_BLEND_DESC1* pDesc) final;

    static D3D11_BLEND_DESC1 PromoteDesc(
      const D3D11_BLEND_DESC*   pSrcDesc);

  private:

    D3D11_BLEND_DESC1             m_desc;

  };

}

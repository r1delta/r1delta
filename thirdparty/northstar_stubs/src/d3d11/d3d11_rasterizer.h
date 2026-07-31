#pragma once

#include "d3d11_device_child.h"
#include "d3d11_include.h"

namespace dxvk {

  class D3D11Device;

  class D3D11RasterizerState : public D3D11StateObject<ID3D11RasterizerState2> {

  public:

    using DescType = D3D11_RASTERIZER_DESC2;

    D3D11RasterizerState(
            D3D11Device*                    device,
      const D3D11_RASTERIZER_DESC2&         desc);
    ~D3D11RasterizerState();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID  riid,
            void**  ppvObject) final;

    void STDMETHODCALLTYPE GetDesc(
            D3D11_RASTERIZER_DESC* pDesc) final;

    void STDMETHODCALLTYPE GetDesc1(
            D3D11_RASTERIZER_DESC1* pDesc) final;

    void STDMETHODCALLTYPE GetDesc2(
            D3D11_RASTERIZER_DESC2* pDesc) final;

    static D3D11_RASTERIZER_DESC2 PromoteDesc(
      const D3D11_RASTERIZER_DESC*  pDesc);

    static D3D11_RASTERIZER_DESC2 PromoteDesc(
      const D3D11_RASTERIZER_DESC1* pDesc);

  private:

    D3D11_RASTERIZER_DESC2 m_desc;

  };

}

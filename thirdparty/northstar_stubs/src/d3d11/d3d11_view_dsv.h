#pragma once

#include "d3d11_device_child.h"
#include "d3d11_include.h"

namespace dxvk {

  class D3D11Device;

  class D3D11DepthStencilView : public D3D11DeviceChild<ID3D11DepthStencilView> {

  public:

    D3D11DepthStencilView(
            D3D11Device*                      pDevice,
            ID3D11Resource*                   pResource,
      const D3D11_DEPTH_STENCIL_VIEW_DESC*    pDesc);

    ~D3D11DepthStencilView();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) final;

    void STDMETHODCALLTYPE GetResource(ID3D11Resource** ppResource) final;

    void STDMETHODCALLTYPE GetDesc(D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc) final;

    static HRESULT GetDescFromResource(
            ID3D11Resource*                   pResource,
            D3D11_DEPTH_STENCIL_VIEW_DESC*    pDesc);

  private:

    ID3D11Resource*                   m_resource;
    D3D11_DEPTH_STENCIL_VIEW_DESC     m_desc;

  };

}

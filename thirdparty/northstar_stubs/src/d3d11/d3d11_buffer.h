#pragma once

#include "d3d11_device_child.h"
#include "d3d11_include.h"
#include "d3d11_resource.h"

namespace dxvk {

  class D3D11Device;
  class D3D11DeviceContext;

  class D3D11Buffer : public D3D11DeviceChild<ID3D11Buffer> {
  public:

    D3D11Buffer(
            D3D11Device*                pDevice,
      const D3D11_BUFFER_DESC*          pDesc);
    ~D3D11Buffer();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID  riid,
            void**  ppvObject) final;

    void STDMETHODCALLTYPE GetType(
            D3D11_RESOURCE_DIMENSION *pResourceDimension) final;

    UINT STDMETHODCALLTYPE GetEvictionPriority() final;

    void STDMETHODCALLTYPE SetEvictionPriority(UINT EvictionPriority) final;

    void STDMETHODCALLTYPE GetDesc(
            D3D11_BUFFER_DESC *pDesc) final;

    const D3D11_BUFFER_DESC* Desc() const {
      return &m_desc;
    }

  private:

    const D3D11_BUFFER_DESC     m_desc;
    D3D11DXGIResource           m_resource;

  };

  D3D11Buffer* GetCommonBuffer(
          ID3D11Resource*       pResource);

}

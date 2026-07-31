#pragma once

#include "d3d11_context.h"
#include "d3d11_include.h"

namespace dxvk {

  class D3D11CommandList : public D3D11DeviceChild<ID3D11CommandList> {

  public:

    D3D11CommandList(
            D3D11Device*  pDevice,
            UINT          ContextFlags);

    ~D3D11CommandList();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID  riid,
            void**  ppvObject) final;

    UINT STDMETHODCALLTYPE GetContextFlags() final;

  private:

    UINT         const m_contextFlags;

  };

}

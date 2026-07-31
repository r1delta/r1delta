#pragma once

#include "d3d11_device_child.h"
#include "d3d11_include.h"

namespace dxvk {

  class D3D11Device;

  class D3D11InputLayout : public D3D11DeviceChild<ID3D11InputLayout> {

  public:

    D3D11InputLayout(D3D11Device* pDevice);

    ~D3D11InputLayout();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID                riid,
            void**                ppvObject) final;

  };

}

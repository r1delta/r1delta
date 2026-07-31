#pragma once

#include "d3d11_context_state.h"
#include "d3d11_device_child.h"
#include "d3d11_include.h"

namespace dxvk {

  class D3D11Device;

  class D3D11DeviceContextState : public D3D11DeviceChild<ID3DDeviceContextState> {

  public:

    D3D11DeviceContextState(
            D3D11Device*         pDevice);

    ~D3D11DeviceContextState();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID                riid,
            void**                ppvObject);

    void SetState(const D3D11ContextState& State) {
      m_state = State;
    }

    void GetState(D3D11ContextState& State) const {
      State = m_state;
    }

  private:

    D3D11ContextState m_state;

  };

}

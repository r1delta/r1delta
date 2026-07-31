#include "d3d11_device_child.h"
#include "d3d11_include.h"
#include "d3d11_state_object.h"

namespace dxvk {

  D3D11DeviceContextState::D3D11DeviceContextState(
          D3D11Device*         pDevice)
  : D3D11DeviceChild<ID3DDeviceContextState>(pDevice) {

  }


  D3D11DeviceContextState::~D3D11DeviceContextState() {

  }


  HRESULT STDMETHODCALLTYPE D3D11DeviceContextState::QueryInterface(
          REFIID                riid,
          void**                ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3DDeviceContextState)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }

}

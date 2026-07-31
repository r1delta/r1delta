#include "d3d11_device.h"
#include "d3d11_include.h"
#include "d3d11_input_layout.h"

namespace dxvk {

  D3D11InputLayout::D3D11InputLayout(D3D11Device* pDevice)
  : D3D11DeviceChild<ID3D11InputLayout>(pDevice) {
  }


  D3D11InputLayout::~D3D11InputLayout() {
  }


  HRESULT STDMETHODCALLTYPE D3D11InputLayout::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11InputLayout)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }

}

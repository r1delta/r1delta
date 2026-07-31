#include "d3d11_depth_stencil.h"
#include "d3d11_device.h"
#include "d3d11_include.h"

namespace dxvk {

  D3D11DepthStencilState::D3D11DepthStencilState(
          D3D11Device*              device,
    const D3D11_DEPTH_STENCIL_DESC& desc)
  : D3D11StateObject<ID3D11DepthStencilState>(device),
    m_desc(desc) {
  }


  D3D11DepthStencilState::~D3D11DepthStencilState() {
  }


  HRESULT STDMETHODCALLTYPE D3D11DepthStencilState::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11DepthStencilState)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  void STDMETHODCALLTYPE D3D11DepthStencilState::GetDesc(D3D11_DEPTH_STENCIL_DESC* pDesc) {
    *pDesc = m_desc;
  }

}

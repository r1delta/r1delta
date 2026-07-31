#include "d3d11_class_linkage.h"
#include "d3d11_device.h"
#include "d3d11_include.h"

namespace dxvk {

  D3D11ClassLinkage::D3D11ClassLinkage(
          D3D11Device*                pDevice)
  : D3D11DeviceChild<ID3D11ClassLinkage>(pDevice) {
  }


  D3D11ClassLinkage::~D3D11ClassLinkage() {
  }


  HRESULT STDMETHODCALLTYPE D3D11ClassLinkage::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11ClassLinkage)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  HRESULT STDMETHODCALLTYPE D3D11ClassLinkage::CreateClassInstance(
          LPCSTR              pClassTypeName,
          UINT                ConstantBufferOffset,
          UINT                ConstantVectorOffset,
          UINT                TextureOffset,
          UINT                SamplerOffset,
          ID3D11ClassInstance **ppInstance) {
    InitReturnPtr(ppInstance);
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE D3D11ClassLinkage::GetClassInstance(
          LPCSTR              pClassInstanceName,
          UINT                InstanceIndex,
          ID3D11ClassInstance **ppInstance) {
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }

}

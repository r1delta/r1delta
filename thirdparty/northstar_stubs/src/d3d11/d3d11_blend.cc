#include "d3d11_blend.h"
#include "d3d11_include.h"
#include "d3d11_device.h"

namespace dxvk {

  D3D11BlendState::D3D11BlendState(
          D3D11Device*        device,
    const D3D11_BLEND_DESC1&  desc)
  : D3D11StateObject<ID3D11BlendState1>(device),
    m_desc(desc) {
  }


  D3D11BlendState::~D3D11BlendState() {
  }


  HRESULT STDMETHODCALLTYPE D3D11BlendState::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11BlendState)
     || riid == __uuidof(ID3D11BlendState1)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  void STDMETHODCALLTYPE D3D11BlendState::GetDesc(D3D11_BLEND_DESC* pDesc) {
    pDesc->AlphaToCoverageEnable  = m_desc.AlphaToCoverageEnable;
    pDesc->IndependentBlendEnable = m_desc.IndependentBlendEnable;

    for (uint32_t i = 0; i < 8; i++) {
      pDesc->RenderTarget[i].BlendEnable           = m_desc.RenderTarget[i].BlendEnable;
      pDesc->RenderTarget[i].SrcBlend              = m_desc.RenderTarget[i].SrcBlend;
      pDesc->RenderTarget[i].DestBlend             = m_desc.RenderTarget[i].DestBlend;
      pDesc->RenderTarget[i].BlendOp               = m_desc.RenderTarget[i].BlendOp;
      pDesc->RenderTarget[i].SrcBlendAlpha         = m_desc.RenderTarget[i].SrcBlendAlpha;
      pDesc->RenderTarget[i].DestBlendAlpha        = m_desc.RenderTarget[i].DestBlendAlpha;
      pDesc->RenderTarget[i].BlendOpAlpha          = m_desc.RenderTarget[i].BlendOpAlpha;
      pDesc->RenderTarget[i].RenderTargetWriteMask = m_desc.RenderTarget[i].RenderTargetWriteMask;
    }
  }


  void STDMETHODCALLTYPE D3D11BlendState::GetDesc1(D3D11_BLEND_DESC1* pDesc) {
    *pDesc = m_desc;
  }

  D3D11_BLEND_DESC1 D3D11BlendState::PromoteDesc(const D3D11_BLEND_DESC* pSrcDesc) {
    D3D11_BLEND_DESC1 dstDesc;
    dstDesc.AlphaToCoverageEnable  = pSrcDesc->AlphaToCoverageEnable;
    dstDesc.IndependentBlendEnable = pSrcDesc->IndependentBlendEnable;

    for (uint32_t i = 0; i < 8; i++) {
      dstDesc.RenderTarget[i].BlendEnable           = pSrcDesc->RenderTarget[i].BlendEnable;
      dstDesc.RenderTarget[i].LogicOpEnable         = FALSE;
      dstDesc.RenderTarget[i].SrcBlend              = pSrcDesc->RenderTarget[i].SrcBlend;
      dstDesc.RenderTarget[i].DestBlend             = pSrcDesc->RenderTarget[i].DestBlend;
      dstDesc.RenderTarget[i].BlendOp               = pSrcDesc->RenderTarget[i].BlendOp;
      dstDesc.RenderTarget[i].SrcBlendAlpha         = pSrcDesc->RenderTarget[i].SrcBlendAlpha;
      dstDesc.RenderTarget[i].DestBlendAlpha        = pSrcDesc->RenderTarget[i].DestBlendAlpha;
      dstDesc.RenderTarget[i].BlendOpAlpha          = pSrcDesc->RenderTarget[i].BlendOpAlpha;
      dstDesc.RenderTarget[i].LogicOp               = D3D11_LOGIC_OP_NOOP;
      dstDesc.RenderTarget[i].RenderTargetWriteMask = pSrcDesc->RenderTarget[i].RenderTargetWriteMask;
    }

    return dstDesc;
  }

}

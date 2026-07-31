#include "d3d11_device.h"
#include "d3d11_include.h"
#include "d3d11_rasterizer.h"

namespace dxvk {

  D3D11RasterizerState::D3D11RasterizerState(
          D3D11Device*                    device,
    const D3D11_RASTERIZER_DESC2&         desc)
  : D3D11StateObject<ID3D11RasterizerState2>(device),
    m_desc(desc) {
  }


  D3D11RasterizerState::~D3D11RasterizerState() {
  }


  HRESULT STDMETHODCALLTYPE D3D11RasterizerState::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11RasterizerState)
     || riid == __uuidof(ID3D11RasterizerState1)
     || riid == __uuidof(ID3D11RasterizerState2)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  void STDMETHODCALLTYPE D3D11RasterizerState::GetDesc(D3D11_RASTERIZER_DESC* pDesc) {
    pDesc->FillMode               = m_desc.FillMode;
    pDesc->CullMode               = m_desc.CullMode;
    pDesc->FrontCounterClockwise  = m_desc.FrontCounterClockwise;
    pDesc->DepthBias              = m_desc.DepthBias;
    pDesc->DepthBiasClamp         = m_desc.DepthBiasClamp;
    pDesc->SlopeScaledDepthBias   = m_desc.SlopeScaledDepthBias;
    pDesc->DepthClipEnable        = m_desc.DepthClipEnable;
    pDesc->ScissorEnable          = m_desc.ScissorEnable;
    pDesc->MultisampleEnable      = m_desc.MultisampleEnable;
    pDesc->AntialiasedLineEnable  = m_desc.AntialiasedLineEnable;
  }


  void STDMETHODCALLTYPE D3D11RasterizerState::GetDesc1(D3D11_RASTERIZER_DESC1* pDesc) {
    pDesc->FillMode               = m_desc.FillMode;
    pDesc->CullMode               = m_desc.CullMode;
    pDesc->FrontCounterClockwise  = m_desc.FrontCounterClockwise;
    pDesc->DepthBias              = m_desc.DepthBias;
    pDesc->DepthBiasClamp         = m_desc.DepthBiasClamp;
    pDesc->SlopeScaledDepthBias   = m_desc.SlopeScaledDepthBias;
    pDesc->DepthClipEnable        = m_desc.DepthClipEnable;
    pDesc->ScissorEnable          = m_desc.ScissorEnable;
    pDesc->MultisampleEnable      = m_desc.MultisampleEnable;
    pDesc->AntialiasedLineEnable  = m_desc.AntialiasedLineEnable;
    pDesc->ForcedSampleCount      = m_desc.ForcedSampleCount;
  }


  void STDMETHODCALLTYPE D3D11RasterizerState::GetDesc2(D3D11_RASTERIZER_DESC2* pDesc) {
    *pDesc = m_desc;
  }

  D3D11_RASTERIZER_DESC2 D3D11RasterizerState::PromoteDesc(
    const D3D11_RASTERIZER_DESC*  pSrcDesc) {
    D3D11_RASTERIZER_DESC2 dstDesc;
    dstDesc.FillMode              = pSrcDesc->FillMode;
    dstDesc.CullMode              = pSrcDesc->CullMode;
    dstDesc.FrontCounterClockwise = pSrcDesc->FrontCounterClockwise;
    dstDesc.DepthBias             = pSrcDesc->DepthBias;
    dstDesc.DepthBiasClamp        = pSrcDesc->DepthBiasClamp;
    dstDesc.SlopeScaledDepthBias  = pSrcDesc->SlopeScaledDepthBias;
    dstDesc.DepthClipEnable       = pSrcDesc->DepthClipEnable;
    dstDesc.ScissorEnable         = pSrcDesc->ScissorEnable;
    dstDesc.MultisampleEnable     = pSrcDesc->MultisampleEnable;
    dstDesc.AntialiasedLineEnable = pSrcDesc->AntialiasedLineEnable;
    dstDesc.ForcedSampleCount     = 0;
    dstDesc.ConservativeRaster    = D3D11_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return dstDesc;
  }

  D3D11_RASTERIZER_DESC2 D3D11RasterizerState::PromoteDesc(
    const D3D11_RASTERIZER_DESC1*  pSrcDesc) {
    D3D11_RASTERIZER_DESC2 dstDesc;
    dstDesc.FillMode              = pSrcDesc->FillMode;
    dstDesc.CullMode              = pSrcDesc->CullMode;
    dstDesc.FrontCounterClockwise = pSrcDesc->FrontCounterClockwise;
    dstDesc.DepthBias             = pSrcDesc->DepthBias;
    dstDesc.DepthBiasClamp        = pSrcDesc->DepthBiasClamp;
    dstDesc.SlopeScaledDepthBias  = pSrcDesc->SlopeScaledDepthBias;
    dstDesc.DepthClipEnable       = pSrcDesc->DepthClipEnable;
    dstDesc.ScissorEnable         = pSrcDesc->ScissorEnable;
    dstDesc.MultisampleEnable     = pSrcDesc->MultisampleEnable;
    dstDesc.AntialiasedLineEnable = pSrcDesc->AntialiasedLineEnable;
    dstDesc.ForcedSampleCount     = 0;
    dstDesc.ConservativeRaster    = D3D11_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return dstDesc;
  }

}

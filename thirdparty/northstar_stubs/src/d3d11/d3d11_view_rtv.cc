#include "d3d11_buffer.h"
#include "d3d11_device.h"
#include "d3d11_device_child.h"
#include "d3d11_include.h"
#include "d3d11_resource.h"
#include "d3d11_texture.h"
#include "d3d11_view_rtv.h"

namespace dxvk {

  D3D11RenderTargetView::D3D11RenderTargetView(
          D3D11Device*                      pDevice,
          ID3D11Resource*                   pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC1*   pDesc)
  : D3D11DeviceChild<ID3D11RenderTargetView1>(pDevice),
    m_resource(pResource), m_desc(*pDesc) {
    ResourceAddRefPrivate(m_resource);
  }


  D3D11RenderTargetView::~D3D11RenderTargetView() {
    ResourceReleasePrivate(m_resource);
  }


  HRESULT STDMETHODCALLTYPE D3D11RenderTargetView::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11View)
     || riid == __uuidof(ID3D11RenderTargetView)
     || riid == __uuidof(ID3D11RenderTargetView1)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  void STDMETHODCALLTYPE D3D11RenderTargetView::GetResource(ID3D11Resource** ppResource) {
    *ppResource = ref(m_resource);
  }


  void STDMETHODCALLTYPE D3D11RenderTargetView::GetDesc(D3D11_RENDER_TARGET_VIEW_DESC* pDesc) {
    pDesc->Format            = m_desc.Format;
    pDesc->ViewDimension     = m_desc.ViewDimension;

    switch (m_desc.ViewDimension) {
      case D3D11_RTV_DIMENSION_UNKNOWN:
        break;

      case D3D11_RTV_DIMENSION_BUFFER:
        pDesc->Buffer = m_desc.Buffer;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE1D:
        pDesc->Texture1D = m_desc.Texture1D;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
        pDesc->Texture1DArray = m_desc.Texture1DArray;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE2D:
        pDesc->Texture2D.MipSlice = m_desc.Texture2D.MipSlice;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
        pDesc->Texture2DArray.MipSlice        = m_desc.Texture2DArray.MipSlice;
        pDesc->Texture2DArray.FirstArraySlice = m_desc.Texture2DArray.FirstArraySlice;
        pDesc->Texture2DArray.ArraySize       = m_desc.Texture2DArray.ArraySize;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE2DMS:
        pDesc->Texture2DMS = m_desc.Texture2DMS;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
        pDesc->Texture2DMSArray = m_desc.Texture2DMSArray;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE3D:
        pDesc->Texture3D = m_desc.Texture3D;
        break;
    }
  }


  void STDMETHODCALLTYPE D3D11RenderTargetView::GetDesc1(D3D11_RENDER_TARGET_VIEW_DESC1* pDesc) {
    *pDesc = m_desc;
  }


  HRESULT D3D11RenderTargetView::GetDescFromResource(
          ID3D11Resource*                   pResource,
          D3D11_RENDER_TARGET_VIEW_DESC1*   pDesc) {
    D3D11_RESOURCE_DIMENSION resourceDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    pResource->GetType(&resourceDim);

    switch (resourceDim) {
      case D3D11_RESOURCE_DIMENSION_TEXTURE1D: {
        D3D11_TEXTURE1D_DESC resourceDesc;
        static_cast<D3D11Texture1D*>(pResource)->GetDesc(&resourceDesc);

        pDesc->Format = resourceDesc.Format;

        if (resourceDesc.ArraySize == 1) {
          pDesc->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1D;
          pDesc->Texture1D.MipSlice = 0;
        } else {
          pDesc->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
          pDesc->Texture1DArray.MipSlice        = 0;
          pDesc->Texture1DArray.FirstArraySlice = 0;
          pDesc->Texture1DArray.ArraySize       = resourceDesc.ArraySize;
        }
      } return S_OK;

      case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
        D3D11_TEXTURE2D_DESC resourceDesc;
        static_cast<D3D11Texture2D*>(pResource)->GetDesc(&resourceDesc);

        pDesc->Format = resourceDesc.Format;

        if (resourceDesc.SampleDesc.Count == 1) {
          if (resourceDesc.ArraySize == 1) {
            pDesc->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            pDesc->Texture2D.MipSlice   = 0;
            pDesc->Texture2D.PlaneSlice = 0;
          } else {
            pDesc->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            pDesc->Texture2DArray.MipSlice        = 0;
            pDesc->Texture2DArray.FirstArraySlice = 0;
            pDesc->Texture2DArray.ArraySize       = resourceDesc.ArraySize;
            pDesc->Texture2DArray.PlaneSlice      = 0;
          }
        } else {
          if (resourceDesc.ArraySize == 1) {
            pDesc->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
          } else {
            pDesc->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
            pDesc->Texture2DMSArray.FirstArraySlice = 0;
            pDesc->Texture2DMSArray.ArraySize       = resourceDesc.ArraySize;
          }
        }
      } return S_OK;

      case D3D11_RESOURCE_DIMENSION_TEXTURE3D: {
        D3D11_TEXTURE3D_DESC resourceDesc;
        static_cast<D3D11Texture3D*>(pResource)->GetDesc(&resourceDesc);

        pDesc->Format         = resourceDesc.Format;
        pDesc->ViewDimension  = D3D11_RTV_DIMENSION_TEXTURE3D;
        pDesc->Texture3D.MipSlice    = 0;
        pDesc->Texture3D.FirstWSlice = 0;
        pDesc->Texture3D.WSize       = resourceDesc.Depth;
      } return S_OK;

      default:
        DXVK_LOG("err", "Unsupported dimension for render target view: %d", resourceDim);
        return E_INVALIDARG;
    }
  }


  D3D11_RENDER_TARGET_VIEW_DESC1 D3D11RenderTargetView::PromoteDesc(
    const D3D11_RENDER_TARGET_VIEW_DESC*    pDesc,
          UINT                              Plane) {
    D3D11_RENDER_TARGET_VIEW_DESC1 dstDesc;
    dstDesc.Format            = pDesc->Format;
    dstDesc.ViewDimension     = pDesc->ViewDimension;

    switch (pDesc->ViewDimension) {
      case D3D11_RTV_DIMENSION_UNKNOWN:
        break;

      case D3D11_RTV_DIMENSION_BUFFER:
        dstDesc.Buffer = pDesc->Buffer;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE1D:
        dstDesc.Texture1D = pDesc->Texture1D;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
        dstDesc.Texture1DArray = pDesc->Texture1DArray;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE2D:
        dstDesc.Texture2D.MipSlice   = pDesc->Texture2D.MipSlice;
        dstDesc.Texture2D.PlaneSlice = Plane;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
        dstDesc.Texture2DArray.MipSlice        = pDesc->Texture2DArray.MipSlice;
        dstDesc.Texture2DArray.FirstArraySlice = pDesc->Texture2DArray.FirstArraySlice;
        dstDesc.Texture2DArray.ArraySize       = pDesc->Texture2DArray.ArraySize;
        dstDesc.Texture2DArray.PlaneSlice      = Plane;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE2DMS:
        dstDesc.Texture2DMS = pDesc->Texture2DMS;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
        dstDesc.Texture2DMSArray = pDesc->Texture2DMSArray;
        break;

      case D3D11_RTV_DIMENSION_TEXTURE3D:
        dstDesc.Texture3D = pDesc->Texture3D;
        break;
    }

    return dstDesc;
  }

}

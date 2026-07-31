#include "d3d11_device.h"
#include "d3d11_include.h"
#include "d3d11_resource.h"
#include "d3d11_state.h"
#include "d3d11_texture.h"

namespace dxvk {

  D3D11CommonTexture::D3D11CommonTexture(
          D3D11Device*                pDevice,
    const D3D11_COMMON_TEXTURE_DESC*  pDesc,
          D3D11_RESOURCE_DIMENSION    Dimension)
  : m_device(pDevice), m_dimension(Dimension), m_desc(*pDesc) {
  }


  D3D11CommonTexture::~D3D11CommonTexture() {
  }


  D3D11DXGISurface::D3D11DXGISurface(
          ID3D11Resource*     pResource,
          D3D11CommonTexture* pTexture)
  : m_resource  (pResource),
    m_texture   (pTexture) {
  }


  D3D11DXGISurface::~D3D11DXGISurface() {
  }


  ULONG STDMETHODCALLTYPE D3D11DXGISurface::AddRef() {
    return m_resource->AddRef();
  }


  ULONG STDMETHODCALLTYPE D3D11DXGISurface::Release() {
    return m_resource->Release();
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::QueryInterface(
          REFIID                  riid,
          void**                  ppvObject) {
    return m_resource->QueryInterface(riid, ppvObject);
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::GetPrivateData(
          REFGUID                 Name,
          UINT*                   pDataSize,
          void*                   pData) {
    return m_resource->GetPrivateData(Name, pDataSize, pData);
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::SetPrivateData(
          REFGUID                 Name,
          UINT                    DataSize,
    const void*                   pData) {
    return m_resource->SetPrivateData(Name, DataSize, pData);
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::SetPrivateDataInterface(
          REFGUID                 Name,
    const IUnknown*               pUnknown) {
    return m_resource->SetPrivateDataInterface(Name, pUnknown);
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::GetParent(
          REFIID                  riid,
          void**                  ppParent) {
    return GetDevice(riid, ppParent);
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::GetDevice(
          REFIID                  riid,
          void**                  ppDevice) {
    Com<ID3D11Device> device;
    m_resource->GetDevice(&device);
    return device->QueryInterface(riid, ppDevice);
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::GetDesc(
          DXGI_SURFACE_DESC*      pDesc) {
    if (!pDesc)
      return DXGI_ERROR_INVALID_CALL;

    auto desc = m_texture->Desc();
    pDesc->Width      = desc->Width;
    pDesc->Height     = desc->Height;
    pDesc->Format     = desc->Format;
    pDesc->SampleDesc = desc->SampleDesc;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::Map(
            DXGI_MAPPED_RECT*       pLockedRect,
            UINT                    MapFlags) {
    Com<ID3D11Device>        device;
    Com<ID3D11DeviceContext> context;

    m_resource->GetDevice(&device);
    device->GetImmediateContext(&context);

    if (pLockedRect) {
      pLockedRect->Pitch = 0;
      pLockedRect->pBits = nullptr;
    }

    D3D11_MAP mapType;

    if (MapFlags & (DXGI_MAP_READ | DXGI_MAP_WRITE))
      mapType = D3D11_MAP_READ_WRITE;
    else if (MapFlags & DXGI_MAP_READ)
      mapType = D3D11_MAP_READ;
    else if (MapFlags & (DXGI_MAP_WRITE | DXGI_MAP_DISCARD))
      mapType = D3D11_MAP_WRITE_DISCARD;
    else if (MapFlags & DXGI_MAP_WRITE)
      mapType = D3D11_MAP_WRITE;
    else
      return DXGI_ERROR_INVALID_CALL;

    D3D11_MAPPED_SUBRESOURCE sr;
    HRESULT hr = context->Map(m_resource, 0,
      mapType, 0, pLockedRect ? &sr : nullptr);

    if (hr != S_OK)
      return hr;

    pLockedRect->Pitch = sr.RowPitch;
    pLockedRect->pBits = reinterpret_cast<unsigned char*>(sr.pData);
    return hr;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::Unmap() {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::GetDC(
          BOOL                    Discard,
          HDC*                    phdc) {
    DXVK_LOG_STUB();
    return DXGI_ERROR_INVALID_CALL;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::ReleaseDC(
          RECT*                   pDirtyRect) {
    DXVK_LOG_STUB();
    return DXGI_ERROR_INVALID_CALL;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGISurface::GetResource(
          REFIID                  riid,
          void**                  ppParentResource,
          UINT*                   pSubresourceIndex) {
    HRESULT hr = m_resource->QueryInterface(riid, ppParentResource);
    if (pSubresourceIndex)
      *pSubresourceIndex = 0;
    return hr;
  }


  ///////////////////////////////////////////
  //      D 3 D 1 1 T E X T U R E 1 D
  D3D11Texture1D::D3D11Texture1D(
          D3D11Device*                pDevice,
    const D3D11_COMMON_TEXTURE_DESC*  pDesc)
  : D3D11DeviceChild<ID3D11Texture1D>(pDevice),
    m_texture (pDevice, pDesc, D3D11_RESOURCE_DIMENSION_TEXTURE1D),
    m_surface (this, &m_texture),
    m_resource(this) {

  }


  D3D11Texture1D::~D3D11Texture1D() {
  }


  HRESULT STDMETHODCALLTYPE D3D11Texture1D::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11Resource)
     || riid == __uuidof(ID3D11Texture1D)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(IDXGISurface)
      || riid == __uuidof(IDXGISurface1)
      || riid == __uuidof(IDXGISurface2)) {
      *ppvObject = ref(&m_surface);
      return S_OK;
    }

    if (riid == __uuidof(IDXGIObject)
     || riid == __uuidof(IDXGIDeviceSubObject)
     || riid == __uuidof(IDXGIResource)
     || riid == __uuidof(IDXGIResource1)) {
       *ppvObject = ref(&m_resource);
       return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  void STDMETHODCALLTYPE D3D11Texture1D::GetType(D3D11_RESOURCE_DIMENSION *pResourceDimension) {
    *pResourceDimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
  }


  UINT STDMETHODCALLTYPE D3D11Texture1D::GetEvictionPriority() {
    return DXGI_RESOURCE_PRIORITY_NORMAL;
  }


  void STDMETHODCALLTYPE D3D11Texture1D::SetEvictionPriority(UINT EvictionPriority) {
  }


  void STDMETHODCALLTYPE D3D11Texture1D::GetDesc(D3D11_TEXTURE1D_DESC *pDesc) {
    pDesc->Width          = m_texture.Desc()->Width;
    pDesc->MipLevels      = m_texture.Desc()->MipLevels;
    pDesc->ArraySize      = m_texture.Desc()->ArraySize;
    pDesc->Format         = m_texture.Desc()->Format;
    pDesc->Usage          = m_texture.Desc()->Usage;
    pDesc->BindFlags      = m_texture.Desc()->BindFlags;
    pDesc->CPUAccessFlags = m_texture.Desc()->CPUAccessFlags;
    pDesc->MiscFlags      = m_texture.Desc()->MiscFlags;
  }


  ///////////////////////////////////////////
  //      D 3 D 1 1 T E X T U R E 2 D
  D3D11Texture2D::D3D11Texture2D(
          D3D11Device*                pDevice,
    const D3D11_COMMON_TEXTURE_DESC*  pDesc)
  : D3D11DeviceChild<ID3D11Texture2D1>(pDevice),
    m_texture (pDevice, pDesc, D3D11_RESOURCE_DIMENSION_TEXTURE2D),
    m_surface (this, &m_texture),
    m_resource(this) {

  }


  D3D11Texture2D::~D3D11Texture2D() {
  }


  HRESULT STDMETHODCALLTYPE D3D11Texture2D::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11Resource)
     || riid == __uuidof(ID3D11Texture2D)
     || riid == __uuidof(ID3D11Texture2D1)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(IDXGISurface)
      || riid == __uuidof(IDXGISurface1)
      || riid == __uuidof(IDXGISurface2)) {
      *ppvObject = ref(&m_surface);
      return S_OK;
    }

    if (riid == __uuidof(IDXGIObject)
     || riid == __uuidof(IDXGIDeviceSubObject)
     || riid == __uuidof(IDXGIResource)
     || riid == __uuidof(IDXGIResource1)) {
       *ppvObject = ref(&m_resource);
       return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  void STDMETHODCALLTYPE D3D11Texture2D::GetType(D3D11_RESOURCE_DIMENSION *pResourceDimension) {
    *pResourceDimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
  }


  UINT STDMETHODCALLTYPE D3D11Texture2D::GetEvictionPriority() {
    return DXGI_RESOURCE_PRIORITY_MAXIMUM;
  }


  void STDMETHODCALLTYPE D3D11Texture2D::SetEvictionPriority(UINT EvictionPriority) {
  }


  void STDMETHODCALLTYPE D3D11Texture2D::GetDesc(D3D11_TEXTURE2D_DESC* pDesc) {
    pDesc->Width          = m_texture.Desc()->Width;
    pDesc->Height         = m_texture.Desc()->Height;
    pDesc->MipLevels      = m_texture.Desc()->MipLevels;
    pDesc->ArraySize      = m_texture.Desc()->ArraySize;
    pDesc->Format         = m_texture.Desc()->Format;
    pDesc->SampleDesc     = m_texture.Desc()->SampleDesc;
    pDesc->Usage          = m_texture.Desc()->Usage;
    pDesc->BindFlags      = m_texture.Desc()->BindFlags;
    pDesc->CPUAccessFlags = m_texture.Desc()->CPUAccessFlags;
    pDesc->MiscFlags      = m_texture.Desc()->MiscFlags;
  }


  void STDMETHODCALLTYPE D3D11Texture2D::GetDesc1(D3D11_TEXTURE2D_DESC1* pDesc) {
    pDesc->Width          = m_texture.Desc()->Width;
    pDesc->Height         = m_texture.Desc()->Height;
    pDesc->MipLevels      = m_texture.Desc()->MipLevels;
    pDesc->ArraySize      = m_texture.Desc()->ArraySize;
    pDesc->Format         = m_texture.Desc()->Format;
    pDesc->SampleDesc     = m_texture.Desc()->SampleDesc;
    pDesc->Usage          = m_texture.Desc()->Usage;
    pDesc->BindFlags      = m_texture.Desc()->BindFlags;
    pDesc->CPUAccessFlags = m_texture.Desc()->CPUAccessFlags;
    pDesc->MiscFlags      = m_texture.Desc()->MiscFlags;
    pDesc->TextureLayout  = m_texture.Desc()->TextureLayout;
  }


  ///////////////////////////////////////////
  //      D 3 D 1 1 T E X T U R E 3 D
  D3D11Texture3D::D3D11Texture3D(
          D3D11Device*                pDevice,
    const D3D11_COMMON_TEXTURE_DESC*  pDesc)
  : D3D11DeviceChild<ID3D11Texture3D1>(pDevice),
    m_texture (pDevice, pDesc, D3D11_RESOURCE_DIMENSION_TEXTURE3D),
    m_surface (this, &m_texture),
    m_resource(this) {
  }


  D3D11Texture3D::~D3D11Texture3D() {
  }


  HRESULT STDMETHODCALLTYPE D3D11Texture3D::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11Resource)
     || riid == __uuidof(ID3D11Texture3D)
     || riid == __uuidof(ID3D11Texture3D1)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(IDXGIObject)
     || riid == __uuidof(IDXGIDeviceSubObject)
     || riid == __uuidof(IDXGIResource)
     || riid == __uuidof(IDXGIResource1)) {
       *ppvObject = ref(&m_resource);
       return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  void STDMETHODCALLTYPE D3D11Texture3D::GetType(D3D11_RESOURCE_DIMENSION *pResourceDimension) {
    *pResourceDimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
  }


  UINT STDMETHODCALLTYPE D3D11Texture3D::GetEvictionPriority() {
    return DXGI_RESOURCE_PRIORITY_MAXIMUM;
  }


  void STDMETHODCALLTYPE D3D11Texture3D::SetEvictionPriority(UINT EvictionPriority) {
  }


  void STDMETHODCALLTYPE D3D11Texture3D::GetDesc(D3D11_TEXTURE3D_DESC* pDesc) {
    pDesc->Width          = m_texture.Desc()->Width;
    pDesc->Height         = m_texture.Desc()->Height;
    pDesc->Depth          = m_texture.Desc()->Depth;
    pDesc->MipLevels      = m_texture.Desc()->MipLevels;
    pDesc->Format         = m_texture.Desc()->Format;
    pDesc->Usage          = m_texture.Desc()->Usage;
    pDesc->BindFlags      = m_texture.Desc()->BindFlags;
    pDesc->CPUAccessFlags = m_texture.Desc()->CPUAccessFlags;
    pDesc->MiscFlags      = m_texture.Desc()->MiscFlags;
  }


  void STDMETHODCALLTYPE D3D11Texture3D::GetDesc1(D3D11_TEXTURE3D_DESC1* pDesc) {
    pDesc->Width          = m_texture.Desc()->Width;
    pDesc->Height         = m_texture.Desc()->Height;
    pDesc->Depth          = m_texture.Desc()->Depth;
    pDesc->MipLevels      = m_texture.Desc()->MipLevels;
    pDesc->Format         = m_texture.Desc()->Format;
    pDesc->Usage          = m_texture.Desc()->Usage;
    pDesc->BindFlags      = m_texture.Desc()->BindFlags;
    pDesc->CPUAccessFlags = m_texture.Desc()->CPUAccessFlags;
    pDesc->MiscFlags      = m_texture.Desc()->MiscFlags;
  }


  D3D11CommonTexture* GetCommonTexture(ID3D11Resource* pResource) {
    D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    pResource->GetType(&dimension);

    switch (dimension) {
      case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        return static_cast<D3D11Texture1D*>(pResource)->GetCommonTexture();

      case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        return static_cast<D3D11Texture2D*>(pResource)->GetCommonTexture();

      case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
        return static_cast<D3D11Texture3D*>(pResource)->GetCommonTexture();

      default:
        return nullptr;
    }
  }

}

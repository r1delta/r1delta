#pragma once

#include "d3d11_device_child.h"
#include "d3d11_include.h"

namespace dxvk {

  class D3D11Device;

  enum D3D11_COMMON_TEXTURE_MAP_MODE {
    D3D11_COMMON_TEXTURE_MAP_MODE_NONE,     ///< Not mapped
    D3D11_COMMON_TEXTURE_MAP_MODE_BUFFER,   ///< Mapped through buffer
    D3D11_COMMON_TEXTURE_MAP_MODE_DIRECT,   ///< Directly mapped to host mem
    D3D11_COMMON_TEXTURE_MAP_MODE_STAGING,  ///< Buffer only, no image
  };

  struct D3D11_COMMON_TEXTURE_DESC {
    UINT             Width;
    UINT             Height;
    UINT             Depth;
    UINT             MipLevels;
    UINT             ArraySize;
    DXGI_FORMAT      Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D11_USAGE      Usage;
    UINT             BindFlags;
    UINT             CPUAccessFlags;
    UINT             MiscFlags;
    D3D11_TEXTURE_LAYOUT TextureLayout;
  };

  struct D3D11_COMMON_TEXTURE_SUBRESOURCE_LAYOUT {
    UINT64          Offset;
    UINT64          Size;
    UINT            RowPitch;
    UINT            DepthPitch;
  };

  class D3D11CommonTexture {

  public:

    D3D11CommonTexture(
            D3D11Device*                pDevice,
      const D3D11_COMMON_TEXTURE_DESC*  pDesc,
            D3D11_RESOURCE_DIMENSION    Dimension);

    ~D3D11CommonTexture();

    const D3D11_COMMON_TEXTURE_DESC* Desc() const {
      return &m_desc;
    }

  private:

    D3D11Device* const            m_device;
    D3D11_RESOURCE_DIMENSION      m_dimension;
    D3D11_COMMON_TEXTURE_DESC     m_desc;

  };


  class D3D11DXGISurface : public IDXGISurface2 {

  public:

    D3D11DXGISurface(
            ID3D11Resource*     pResource,
            D3D11CommonTexture* pTexture);

    ~D3D11DXGISurface();

    ULONG STDMETHODCALLTYPE AddRef();

    ULONG STDMETHODCALLTYPE Release();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID                  riid,
            void**                  ppvObject);

    HRESULT STDMETHODCALLTYPE GetPrivateData(
            REFGUID                 Name,
            UINT*                   pDataSize,
            void*                   pData);

    HRESULT STDMETHODCALLTYPE SetPrivateData(
            REFGUID                 Name,
            UINT                    DataSize,
      const void*                   pData);

    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
            REFGUID                 Name,
      const IUnknown*               pUnknown);

    HRESULT STDMETHODCALLTYPE GetParent(
            REFIID                  riid,
            void**                  ppParent);

    HRESULT STDMETHODCALLTYPE GetDevice(
            REFIID                  riid,
            void**                  ppDevice);

    HRESULT STDMETHODCALLTYPE GetDesc(
            DXGI_SURFACE_DESC*      pDesc);

    HRESULT STDMETHODCALLTYPE Map(
            DXGI_MAPPED_RECT*       pLockedRect,
            UINT                    MapFlags);

    HRESULT STDMETHODCALLTYPE Unmap();

    HRESULT STDMETHODCALLTYPE GetDC(
            BOOL                    Discard,
            HDC*                    phdc);

    HRESULT STDMETHODCALLTYPE ReleaseDC(
            RECT*                   pDirtyRect);

    HRESULT STDMETHODCALLTYPE GetResource(
            REFIID                  riid,
            void**                  ppParentResource,
            UINT*                   pSubresourceIndex);

  private:

    ID3D11Resource*     m_resource;
    D3D11CommonTexture* m_texture;

  };


  ///////////////////////////////////////////
  //      D 3 D 1 1 T E X T U R E 1 D
  class D3D11Texture1D : public D3D11DeviceChild<ID3D11Texture1D> {

  public:

    D3D11Texture1D(
            D3D11Device*                pDevice,
      const D3D11_COMMON_TEXTURE_DESC*  pDesc);

    ~D3D11Texture1D();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID  riid,
            void**  ppvObject) final;

    void STDMETHODCALLTYPE GetType(
            D3D11_RESOURCE_DIMENSION *pResourceDimension) final;

    UINT STDMETHODCALLTYPE GetEvictionPriority() final;

    void STDMETHODCALLTYPE SetEvictionPriority(UINT EvictionPriority) final;

    void STDMETHODCALLTYPE GetDesc(
            D3D11_TEXTURE1D_DESC *pDesc) final;

    D3D11CommonTexture* GetCommonTexture() {
      return &m_texture;
    }

  private:

    D3D11CommonTexture    m_texture;
    D3D11DXGISurface      m_surface;
    D3D11DXGIResource     m_resource;

  };


  ///////////////////////////////////////////
  //      D 3 D 1 1 T E X T U R E 2 D
  class D3D11Texture2D : public D3D11DeviceChild<ID3D11Texture2D1> {

  public:

    D3D11Texture2D(
            D3D11Device*                pDevice,
      const D3D11_COMMON_TEXTURE_DESC*  pDesc);

    ~D3D11Texture2D();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID  riid,
            void**  ppvObject) final;

    void STDMETHODCALLTYPE GetType(
            D3D11_RESOURCE_DIMENSION *pResourceDimension) final;

    UINT STDMETHODCALLTYPE GetEvictionPriority() final;

    void STDMETHODCALLTYPE SetEvictionPriority(UINT EvictionPriority) final;

    void STDMETHODCALLTYPE GetDesc(
            D3D11_TEXTURE2D_DESC* pDesc) final;

    void STDMETHODCALLTYPE GetDesc1(
            D3D11_TEXTURE2D_DESC1* pDesc) final;

    D3D11CommonTexture* GetCommonTexture() {
      return &m_texture;
    }

  private:

    D3D11CommonTexture    m_texture;
    D3D11DXGISurface      m_surface;
    D3D11DXGIResource     m_resource;

  };


  ///////////////////////////////////////////
  //      D 3 D 1 1 T E X T U R E 3 D
  class D3D11Texture3D : public D3D11DeviceChild<ID3D11Texture3D1> {

  public:

    D3D11Texture3D(
            D3D11Device*                pDevice,
      const D3D11_COMMON_TEXTURE_DESC*  pDesc);

    ~D3D11Texture3D();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID  riid,
            void**  ppvObject) final;

    void STDMETHODCALLTYPE GetType(
            D3D11_RESOURCE_DIMENSION *pResourceDimension) final;

    UINT STDMETHODCALLTYPE GetEvictionPriority() final;

    void STDMETHODCALLTYPE SetEvictionPriority(UINT EvictionPriority) final;

    void STDMETHODCALLTYPE GetDesc(
            D3D11_TEXTURE3D_DESC* pDesc) final;

    void STDMETHODCALLTYPE GetDesc1(
            D3D11_TEXTURE3D_DESC1* pDesc) final;

    D3D11CommonTexture* GetCommonTexture() {
      return &m_texture;
    }

  private:

    D3D11CommonTexture    m_texture;
    D3D11DXGISurface      m_surface;
    D3D11DXGIResource     m_resource;

  };


  D3D11CommonTexture* GetCommonTexture(
          ID3D11Resource*       pResource);

}

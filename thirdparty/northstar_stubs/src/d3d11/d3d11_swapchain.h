#pragma once

#include "d3d11_include.h"
#include "dxgi_object.h"

namespace dxvk {

  class D3D11Device;
  class D3D11Texture2D;

  class D3D11SwapChain : public DxgiObject<IDXGISwapChain4> {
  public:

    D3D11SwapChain(
            D3D11Device*            pDevice,
            IDXGIFactory*           pFactory,
            HWND                    hWnd,
      const DXGI_SWAP_CHAIN_DESC1*  pDesc,
      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*  pFullscreenDes);

    ~D3D11SwapChain();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID                    riid,
            void**                    ppvObject) final;

    HRESULT STDMETHODCALLTYPE GetParent(
            REFIID                    riid,
            void**                    ppParent) final;

    HRESULT STDMETHODCALLTYPE GetDevice(
            REFIID                    riid,
            void**                    ppDevice) final;

    HRESULT STDMETHODCALLTYPE GetBuffer(
            UINT                      Buffer,
            REFIID                    riid,
            void**                    ppSurface) final;

    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() final;

    HRESULT STDMETHODCALLTYPE GetContainingOutput(
            IDXGIOutput**             ppOutput) final;

    HRESULT STDMETHODCALLTYPE GetDesc(
            DXGI_SWAP_CHAIN_DESC*     pDesc) final;

    HRESULT STDMETHODCALLTYPE GetDesc1(
            DXGI_SWAP_CHAIN_DESC1*    pDesc) final;

    HRESULT STDMETHODCALLTYPE GetFullscreenState(
            BOOL*                     pFullscreen,
            IDXGIOutput**             ppTarget) final;

    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(
            DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) final;

    HRESULT STDMETHODCALLTYPE GetHwnd(
            HWND*                     pHwnd) final;

    HRESULT STDMETHODCALLTYPE GetCoreWindow(
            REFIID                    refiid,
            void**                    ppUnk) final;

    HRESULT STDMETHODCALLTYPE GetBackgroundColor(
            DXGI_RGBA*                pColor) final;

    HRESULT STDMETHODCALLTYPE GetRotation(
            DXGI_MODE_ROTATION*       pRotation) final;

    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(
            IDXGIOutput**             ppRestrictToOutput) final;

    HRESULT STDMETHODCALLTYPE GetFrameStatistics(
            DXGI_FRAME_STATISTICS*    pStats) final;

    HRESULT STDMETHODCALLTYPE GetLastPresentCount(
            UINT*                     pLastPresentCount) final;

    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() final;

    HRESULT STDMETHODCALLTYPE Present(
            UINT                      SyncInterval,
            UINT                      Flags) final;

    HRESULT STDMETHODCALLTYPE Present1(
            UINT                      SyncInterval,
            UINT                      PresentFlags,
      const DXGI_PRESENT_PARAMETERS*  pPresentParameters) final;

    HRESULT STDMETHODCALLTYPE ResizeBuffers(
            UINT                      BufferCount,
            UINT                      Width,
            UINT                      Height,
            DXGI_FORMAT               NewFormat,
            UINT                      SwapChainFlags) final;

    HRESULT STDMETHODCALLTYPE ResizeBuffers1(
            UINT                      BufferCount,
            UINT                      Width,
            UINT                      Height,
            DXGI_FORMAT               Format,
            UINT                      SwapChainFlags,
      const UINT*                     pCreationNodeMask,
            IUnknown* const*          ppPresentQueue) final;

    HRESULT STDMETHODCALLTYPE ResizeTarget(
      const DXGI_MODE_DESC*           pNewTargetParameters) final;

    HRESULT STDMETHODCALLTYPE SetFullscreenState(
            BOOL                      Fullscreen,
            IDXGIOutput*              pTarget) final;

    HRESULT STDMETHODCALLTYPE SetBackgroundColor(
      const DXGI_RGBA*                pColor) final;

    HRESULT STDMETHODCALLTYPE SetRotation(
            DXGI_MODE_ROTATION        Rotation) final;

    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() final;

    HRESULT STDMETHODCALLTYPE GetMatrixTransform(
            DXGI_MATRIX_3X2_F*        pMatrix) final;

    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(
            UINT*                     pMaxLatency) final;

    HRESULT STDMETHODCALLTYPE GetSourceSize(
            UINT*                     pWidth,
            UINT*                     pHeight) final;

    HRESULT STDMETHODCALLTYPE SetMatrixTransform(
      const DXGI_MATRIX_3X2_F*        pMatrix) final;

    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(
            UINT                      MaxLatency) final;

    HRESULT STDMETHODCALLTYPE SetSourceSize(
            UINT                      Width,
            UINT                      Height) final;

    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(
            DXGI_COLOR_SPACE_TYPE     ColorSpace,
            UINT*                     pColorSpaceSupport) final;

    HRESULT STDMETHODCALLTYPE SetColorSpace1(
            DXGI_COLOR_SPACE_TYPE     ColorSpace) final;

    HRESULT STDMETHODCALLTYPE SetHDRMetaData(
            DXGI_HDR_METADATA_TYPE    Type,
            UINT                      Size,
            void*                     pMetaData) final;

    HRESULT STDMETHODCALLTYPE SetGammaControl(
            UINT                      NumPoints,
      const DXGI_RGB*                 pGammaCurve);

  private:

    D3D11Device*                m_parent;
    Com<IDXGIFactory>           m_factory;

    HWND                            m_window;
    DXGI_SWAP_CHAIN_DESC1           m_desc;

    Com<IDXGIOutput>                m_target = nullptr;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC m_descFs;

    D3D11Texture2D*                 m_backBuffer = nullptr;

    void CreateBackBuffer();

  };

}
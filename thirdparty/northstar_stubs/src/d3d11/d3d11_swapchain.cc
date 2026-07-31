#include "d3d11_device.h"
#include "d3d11_include.h"
#include "d3d11_resource.h"
#include "d3d11_swapchain.h"
#include "d3d11_texture.h"

// note: for northstar, the dedicated server loop is separate from the render
// loop, and the minimized check is nopped, so we can essentially pretend that
// the window is never visible and ignore almost everything (we just keep the
// backbuffer so we don't get crashes when something tries to use it)

namespace dxvk {

  D3D11SwapChain::D3D11SwapChain(
          D3D11Device*            pDevice,
          IDXGIFactory*           pFactory,
          HWND                    hWnd,
    const DXGI_SWAP_CHAIN_DESC1*  pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*  pFullscreenDesc)
  : m_parent    (pDevice),
    m_factory   (pFactory),
    m_window    (hWnd),
    m_desc      (*pDesc),
    m_descFs    (*pFullscreenDesc) {
    CreateBackBuffer();
  }


  D3D11SwapChain::~D3D11SwapChain() {
    if (m_backBuffer)
      m_backBuffer->ReleasePrivate();
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(IDXGIObject)
     || riid == __uuidof(IDXGIDeviceSubObject)
     || riid == __uuidof(IDXGISwapChain)
     || riid == __uuidof(IDXGISwapChain1)
     || riid == __uuidof(IDXGISwapChain2)
     || riid == __uuidof(IDXGISwapChain3)
     || riid == __uuidof(IDXGISwapChain4)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetParent(REFIID riid, void** ppParent) {
    return m_factory->QueryInterface(riid, ppParent);
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetDevice(REFIID riid, void** ppDevice) {
    return m_parent->QueryInterface(riid, ppDevice);
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) {
    InitReturnPtr(ppSurface);
    return m_backBuffer->QueryInterface(riid, ppSurface);
  }


  UINT STDMETHODCALLTYPE D3D11SwapChain::GetCurrentBackBufferIndex() {
    return 0;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetContainingOutput(IDXGIOutput** ppOutput) {
    if (!ppOutput)
      return DXGI_ERROR_INVALID_CALL;

    HRESULT hr;

    Com<IDXGIDevice> device;
    if (FAILED((hr = m_parent->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&device)))))
      return hr;

    Com<IDXGIAdapter> adapter;
    if (FAILED((hr = device->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&adapter)))))
      return hr;

    return adapter->EnumOutputs(0, ppOutput);
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) {
    if (!pDesc)
      return E_INVALIDARG;

    pDesc->BufferDesc.Width     = m_desc.Width;
    pDesc->BufferDesc.Height    = m_desc.Height;
    pDesc->BufferDesc.RefreshRate = m_descFs.RefreshRate;
    pDesc->BufferDesc.Format    = m_desc.Format;
    pDesc->BufferDesc.ScanlineOrdering = m_descFs.ScanlineOrdering;
    pDesc->BufferDesc.Scaling   = m_descFs.Scaling;
    pDesc->SampleDesc           = m_desc.SampleDesc;
    pDesc->BufferUsage          = m_desc.BufferUsage;
    pDesc->BufferCount          = m_desc.BufferCount;
    pDesc->OutputWindow         = m_window;
    pDesc->Windowed             = m_descFs.Windowed;
    pDesc->SwapEffect           = m_desc.SwapEffect;
    pDesc->Flags                = m_desc.Flags;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc) {
    if (pDesc == nullptr)
      return E_INVALIDARG;

    *pDesc = m_desc;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetBackgroundColor(
          DXGI_RGBA*                pColor) {
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetRotation(
          DXGI_MODE_ROTATION*       pRotation) {
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetRestrictToOutput(
          IDXGIOutput**             ppRestrictToOutput) {
    InitReturnPtr(ppRestrictToOutput);
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) {
    if (!pStats)
      return E_INVALIDARG;

    *pStats = DXGI_FRAME_STATISTICS();
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetFullscreenState(
          BOOL*         pFullscreen,
          IDXGIOutput** ppTarget) {
    HRESULT hr = S_OK;

    if (pFullscreen != nullptr)
      *pFullscreen = !m_descFs.Windowed;

    if (ppTarget != nullptr)
      *ppTarget = m_target.ref();

    return hr;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetFullscreenDesc(
          DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) {
    if (pDesc == nullptr)
      return E_INVALIDARG;

    *pDesc = m_descFs;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetHwnd(
          HWND*                     pHwnd) {
    if (pHwnd == nullptr)
      return E_INVALIDARG;

    *pHwnd = m_window;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetCoreWindow(
          REFIID                    refiid,
          void**                    ppUnk) {
    InitReturnPtr(ppUnk);
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetLastPresentCount(UINT* pLastPresentCount) {
    if (pLastPresentCount == nullptr)
      return E_INVALIDARG;

    *pLastPresentCount = 0;
    return S_OK;
  }


  BOOL STDMETHODCALLTYPE D3D11SwapChain::IsTemporaryMonoSupported() {
    return FALSE;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::Present(UINT SyncInterval, UINT Flags) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::Present1(
          UINT                      SyncInterval,
          UINT                      PresentFlags,
    const DXGI_PRESENT_PARAMETERS*  pPresentParameters) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::ResizeBuffers(
          UINT        BufferCount,
          UINT        Width,
          UINT        Height,
          DXGI_FORMAT NewFormat,
          UINT        SwapChainFlags) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::ResizeBuffers1(
          UINT                      BufferCount,
          UINT                      Width,
          UINT                      Height,
          DXGI_FORMAT               Format,
          UINT                      SwapChainFlags,
    const UINT*                     pCreationNodeMask,
          IUnknown* const*          ppPresentQueue) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::SetFullscreenState(
          BOOL          Fullscreen,
          IDXGIOutput*  pTarget) {
    if (!Fullscreen && pTarget)
      return DXGI_ERROR_INVALID_CALL;

    m_descFs.Windowed = !Fullscreen;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::SetBackgroundColor(
    const DXGI_RGBA*                pColor) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::SetRotation(
          DXGI_MODE_ROTATION        Rotation) {
    return S_OK;
  }


  HANDLE STDMETHODCALLTYPE D3D11SwapChain::GetFrameLatencyWaitableObject() {
    DXVK_LOG_STUB();
    return nullptr;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetMatrixTransform(
          DXGI_MATRIX_3X2_F*        pMatrix) {
    DXVK_LOG_STUB();
    return DXGI_ERROR_INVALID_CALL;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetMaximumFrameLatency(
          UINT*                     pMaxLatency) {
    if (!(m_desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
      return DXGI_ERROR_INVALID_CALL;
    *pMaxLatency = 1;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::GetSourceSize(
          UINT*                     pWidth,
          UINT*                     pHeight) {
    if (pWidth)  *pWidth  = m_desc.Width;
    if (pHeight) *pHeight = m_desc.Height;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::SetMatrixTransform(
    const DXGI_MATRIX_3X2_F*        pMatrix) {
    DXVK_LOG_STUB();
    return DXGI_ERROR_INVALID_CALL;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::SetMaximumFrameLatency(
          UINT                      MaxLatency) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::SetSourceSize(
          UINT                      Width,
          UINT                      Height) {
    if (Width  == 0 || Width  > m_desc.Width
     || Height == 0 || Height > m_desc.Height)
      return E_INVALIDARG;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::CheckColorSpaceSupport(
          DXGI_COLOR_SPACE_TYPE           ColorSpace,
          UINT*                           pColorSpaceSupport) {
    if (!pColorSpaceSupport)
      return E_INVALIDARG;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::SetHDRMetaData(
          DXGI_HDR_METADATA_TYPE    Type,
          UINT                      Size,
          void*                     pMetaData) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11SwapChain::SetGammaControl(
          UINT                      NumPoints,
    const DXGI_RGB*                 pGammaCurve) {
    return S_OK;
  }


  void D3D11SwapChain::CreateBackBuffer() {
    if (m_backBuffer)
      m_backBuffer->ReleasePrivate();

    m_backBuffer        = nullptr;

    // Create new back buffer
    D3D11_COMMON_TEXTURE_DESC desc;
    desc.Width              = std::max(m_desc.Width,  1u);
    desc.Height             = std::max(m_desc.Height, 1u);
    desc.Depth              = 1;
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = m_desc.Format;
    desc.SampleDesc         = m_desc.SampleDesc;
    desc.Usage              = D3D11_USAGE_DEFAULT;
    desc.BindFlags          = 0;
    desc.CPUAccessFlags     = 0;
    desc.MiscFlags          = 0;
    desc.TextureLayout      = D3D11_TEXTURE_LAYOUT_UNDEFINED;

    if (m_desc.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT)
      desc.BindFlags |= D3D11_BIND_RENDER_TARGET;

    if (m_desc.BufferUsage & DXGI_USAGE_SHADER_INPUT)
      desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    if (m_desc.BufferUsage & DXGI_USAGE_UNORDERED_ACCESS)
      desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    if (m_desc.Flags & DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE)
      desc.MiscFlags |= D3D11_RESOURCE_MISC_GDI_COMPATIBLE;

    m_backBuffer = new D3D11Texture2D(m_parent, &desc);
    m_backBuffer->AddRefPrivate();
  }

}

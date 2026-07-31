#include "d3d11_include.h"
#include "d3d11_swapchain.h"
#include "dxgi_adapter.h"
#include "dxgi_factory.h"

namespace dxvk {

  DxgiFactory::DxgiFactory(UINT Flags)
  : m_flags (Flags) {
  }


  DxgiFactory::~DxgiFactory() {
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(IDXGIObject)
     || riid == __uuidof(IDXGIFactory)
     || riid == __uuidof(IDXGIFactory1)
     || riid == __uuidof(IDXGIFactory2)
     || riid == __uuidof(IDXGIFactory3)
     || riid == __uuidof(IDXGIFactory4)
     || riid == __uuidof(IDXGIFactory5)
     || riid == __uuidof(IDXGIFactory6)
     || riid == __uuidof(IDXGIFactory7)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::GetParent(REFIID riid, void** ppParent) {
    InitReturnPtr(ppParent);
    return E_NOINTERFACE;
  }


  BOOL STDMETHODCALLTYPE DxgiFactory::IsWindowedStereoEnabled() {
    return FALSE;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::CreateSoftwareAdapter(
          HMODULE         Module,
          IDXGIAdapter**  ppAdapter) {
    InitReturnPtr(ppAdapter);
    DXVK_LOG_STUB();
    return DXGI_ERROR_UNSUPPORTED;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::CreateSwapChain(
          IUnknown*             pDevice,
          DXGI_SWAP_CHAIN_DESC* pDesc,
          IDXGISwapChain**      ppSwapChain) {
    if (ppSwapChain == nullptr || pDesc == nullptr || pDevice == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    DXGI_SWAP_CHAIN_DESC1 desc;
    desc.Width              = pDesc->BufferDesc.Width;
    desc.Height             = pDesc->BufferDesc.Height;
    desc.Format             = pDesc->BufferDesc.Format;
    desc.Stereo             = FALSE;
    desc.SampleDesc         = pDesc->SampleDesc;
    desc.BufferUsage        = pDesc->BufferUsage;
    desc.BufferCount        = pDesc->BufferCount;
    desc.Scaling            = DXGI_SCALING_STRETCH;
    desc.SwapEffect         = pDesc->SwapEffect;
    desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags              = pDesc->Flags;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC descFs;
    descFs.RefreshRate      = pDesc->BufferDesc.RefreshRate;
    descFs.ScanlineOrdering = pDesc->BufferDesc.ScanlineOrdering;
    descFs.Scaling          = pDesc->BufferDesc.Scaling;
    descFs.Windowed         = pDesc->Windowed;

    IDXGISwapChain1* swapChain = nullptr;
    HRESULT hr = CreateSwapChainForHwnd(
      pDevice, pDesc->OutputWindow,
      &desc, &descFs, nullptr,
      &swapChain);

    *ppSwapChain = swapChain;
    return hr;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::CreateSwapChainForHwnd(
          IUnknown*             pDevice,
          HWND                  hWnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
          IDXGIOutput*          pRestrictToOutput,
          IDXGISwapChain1**     ppSwapChain) {
    InitReturnPtr(ppSwapChain);

    if (!ppSwapChain || !pDesc || !hWnd || !pDevice)
      return DXGI_ERROR_INVALID_CALL;

    RECT rect = { };
    ::GetClientRect(hWnd, &rect);

    DXGI_SWAP_CHAIN_DESC1 desc = *pDesc;

    if (desc.Width)
      desc.Width = rect.right - rect.left;

    if (desc.Height)
      desc.Height = rect.bottom - rect.top;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc;

    if (pFullscreenDesc) {
      fsDesc = *pFullscreenDesc;
    } else {
      fsDesc.RefreshRate      = { 0, 0 };
      fsDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
      fsDesc.Scaling          = DXGI_MODE_SCALING_UNSPECIFIED;
      fsDesc.Windowed         = TRUE;
    }

    HRESULT hr;
    D3D11Device *device;

    hr = pDevice->QueryInterface(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device));

    if (FAILED(hr))
      return hr;

    *ppSwapChain = ref(new D3D11SwapChain(device, this, hWnd, &desc, &fsDesc));

    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::CreateSwapChainForCoreWindow(
          IUnknown*             pDevice,
          IUnknown*             pWindow,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
          IDXGIOutput*          pRestrictToOutput,
          IDXGISwapChain1**     ppSwapChain) {
    InitReturnPtr(ppSwapChain);
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::CreateSwapChainForComposition(
          IUnknown*             pDevice,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
          IDXGIOutput*          pRestrictToOutput,
          IDXGISwapChain1**     ppSwapChain) {
    InitReturnPtr(ppSwapChain);
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::EnumAdapters(
          UINT            Adapter,
          IDXGIAdapter**  ppAdapter) {
    InitReturnPtr(ppAdapter);

    if (ppAdapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    IDXGIAdapter1* handle = nullptr;
    HRESULT hr = this->EnumAdapters1(Adapter, &handle);
    *ppAdapter = handle;
    return hr;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::EnumAdapters1(
          UINT            Adapter,
          IDXGIAdapter1** ppAdapter) {
    InitReturnPtr(ppAdapter);

    if (ppAdapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    if (Adapter != 0)
      return DXGI_ERROR_NOT_FOUND;

    *ppAdapter = ref(new DxgiAdapter(this, Adapter));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::EnumAdapterByLuid(
          LUID                  AdapterLuid,
          REFIID                riid,
          void**                ppvAdapter) {
    InitReturnPtr(ppvAdapter);

    if (ppvAdapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    return (new DxgiAdapter(this, 0))->QueryInterface(riid, ppvAdapter);
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::EnumAdapterByGpuPreference(
          UINT                  Adapter,
          DXGI_GPU_PREFERENCE   GpuPreference,
          REFIID                riid,
          void**                ppvAdapter) {
    InitReturnPtr(ppvAdapter);

    if (ppvAdapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    return (new DxgiAdapter(this, 0))->QueryInterface(riid, ppvAdapter);
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::EnumWarpAdapter(
          REFIID                riid,
          void**                ppvAdapter) {
    InitReturnPtr(ppvAdapter);

    if (ppvAdapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    return (new DxgiAdapter(this, 0))->QueryInterface(riid, ppvAdapter);
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::GetWindowAssociation(HWND *pWindowHandle) {
    if (pWindowHandle == nullptr)
      return DXGI_ERROR_INVALID_CALL;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::GetSharedResourceAdapterLuid(
          HANDLE                hResource,
          LUID*                 pLuid) {
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::MakeWindowAssociation(HWND WindowHandle, UINT Flags) {
    return S_OK;
  }


  BOOL STDMETHODCALLTYPE DxgiFactory::IsCurrent() {
    return TRUE;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::RegisterOcclusionStatusWindow(
          HWND                  WindowHandle,
          UINT                  wMsg,
          DWORD*                pdwCookie) {
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::RegisterStereoStatusEvent(
          HANDLE                hEvent,
          DWORD*                pdwCookie) {
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::RegisterStereoStatusWindow(
          HWND                  WindowHandle,
          UINT                  wMsg,
          DWORD*                pdwCookie) {
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::RegisterOcclusionStatusEvent(
          HANDLE                hEvent,
          DWORD*                pdwCookie) {
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  void STDMETHODCALLTYPE DxgiFactory::UnregisterStereoStatus(
          DWORD                 dwCookie) {
  }


  void STDMETHODCALLTYPE DxgiFactory::UnregisterOcclusionStatus(
          DWORD                 dwCookie) {
  }


  UINT STDMETHODCALLTYPE DxgiFactory::GetCreationFlags() {
    return m_flags;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::CheckFeatureSupport(
          DXGI_FEATURE          Feature,
          void*                 pFeatureSupportData,
          UINT                  FeatureSupportDataSize) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::RegisterAdaptersChangedEvent(
          HANDLE                hEvent,
          DWORD*                pdwCookie) {
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiFactory::UnregisterAdaptersChangedEvent(
          DWORD                 Cookie) {
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }

}

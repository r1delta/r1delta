#include "d3d11_include.h"
#include "dxgi_adapter.h"
#include "dxgi_factory.h"
#include "dxgi_output.h"

namespace dxvk {

  DxgiAdapter::DxgiAdapter(
          DxgiFactory*      factory,
          UINT              index)
  : m_factory (factory),
    m_index   (index) {
  }


  DxgiAdapter::~DxgiAdapter() {
  }


  ULONG STDMETHODCALLTYPE DxgiAdapter::AddRef() {
    return m_factory->AddRef();
  }


  ULONG STDMETHODCALLTYPE DxgiAdapter::Release() {
    return m_factory->Release();
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(IDXGIObject)
     || riid == __uuidof(IDXGIAdapter)
     || riid == __uuidof(IDXGIAdapter1)
     || riid == __uuidof(IDXGIAdapter2)
     || riid == __uuidof(IDXGIAdapter3)
     || riid == __uuidof(IDXGIAdapter4)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::GetParent(REFIID riid, void** ppParent) {
    return m_factory->QueryInterface(riid, ppParent);
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::CheckInterfaceSupport(
          REFGUID                   InterfaceName,
          LARGE_INTEGER*            pUMDVersion) {
    return DXGI_ERROR_UNSUPPORTED;
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::EnumOutputs(
          UINT                      Output,
          IDXGIOutput**             ppOutput) {
    InitReturnPtr(ppOutput);

    if (ppOutput == nullptr)
      return E_INVALIDARG;

    if (Output != 0)
      return DXGI_ERROR_NOT_FOUND;

    *ppOutput = ref(new DxgiOutput(m_factory, this));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::GetDesc(DXGI_ADAPTER_DESC* pDesc) {
    if (pDesc == nullptr)
      return E_INVALIDARG;

    *pDesc = DXGI_ADAPTER_DESC{
      .VendorId              = 0x10DE,
      .DeviceId              = 0x0A27,
      .SubSysId              = 0,
      .Revision              = 0,
      .DedicatedVideoMemory  = 1073741824, // 1 GB
      .DedicatedSystemMemory = 0,
      .SharedSystemMemory    = 0,
      .AdapterLuid           = LUID{0, 0},
    };

    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::GetDesc1(DXGI_ADAPTER_DESC1* pDesc) {
    if (pDesc == nullptr)
      return E_INVALIDARG;

    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::GetDesc2(DXGI_ADAPTER_DESC2* pDesc) {
    if (pDesc == nullptr)
      return E_INVALIDARG;

    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::GetDesc3(
          DXGI_ADAPTER_DESC3*       pDesc) {
    if (pDesc == nullptr)
      return E_INVALIDARG;

    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::QueryVideoMemoryInfo(
          UINT                          NodeIndex,
          DXGI_MEMORY_SEGMENT_GROUP     MemorySegmentGroup,
          DXGI_QUERY_VIDEO_MEMORY_INFO* pVideoMemoryInfo) {
    if (NodeIndex > 0 || !pVideoMemoryInfo)
      return E_INVALIDARG;

    if (MemorySegmentGroup != DXGI_MEMORY_SEGMENT_GROUP_LOCAL
     && MemorySegmentGroup != DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL)
      return E_INVALIDARG;

    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::SetVideoMemoryReservation(
          UINT                          NodeIndex,
          DXGI_MEMORY_SEGMENT_GROUP     MemorySegmentGroup,
          UINT64                        Reservation) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::RegisterHardwareContentProtectionTeardownStatusEvent(
          HANDLE                        hEvent,
          DWORD*                        pdwCookie) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE DxgiAdapter::RegisterVideoMemoryBudgetChangeNotificationEvent(
          HANDLE                        hEvent,
          DWORD*                        pdwCookie) {
    if (!hEvent || !pdwCookie)
      return E_INVALIDARG;
    return S_OK;
  }


  void STDMETHODCALLTYPE DxgiAdapter::UnregisterHardwareContentProtectionTeardownStatus(
          DWORD                         dwCookie) {
  }


  void STDMETHODCALLTYPE DxgiAdapter::UnregisterVideoMemoryBudgetChangeNotification(
          DWORD                         dwCookie) {
  }

}

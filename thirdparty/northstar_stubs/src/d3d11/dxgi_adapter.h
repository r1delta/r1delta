#pragma once

#include "dxgi_object.h"

namespace dxvk {

  class DxgiFactory;

  class DxgiAdapter : public DxgiObject<IDXGIAdapter4> {

  public:

    DxgiAdapter(
            DxgiFactory*              factory,
            UINT                      index);

    ~DxgiAdapter();

    ULONG STDMETHODCALLTYPE AddRef() final;

    ULONG STDMETHODCALLTYPE Release() final;

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID                    riid,
            void**                    ppvObject) final;

    HRESULT STDMETHODCALLTYPE GetParent(
            REFIID                    riid,
            void**                    ppParent) final;

    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(
            REFGUID                   InterfaceName,
            LARGE_INTEGER*            pUMDVersion) final;

    HRESULT STDMETHODCALLTYPE EnumOutputs(
            UINT                      Output,
            IDXGIOutput**             ppOutput) final;

    HRESULT STDMETHODCALLTYPE GetDesc(
            DXGI_ADAPTER_DESC*        pDesc) final;

    HRESULT STDMETHODCALLTYPE GetDesc1(
            DXGI_ADAPTER_DESC1*       pDesc) final;

    HRESULT STDMETHODCALLTYPE GetDesc2(
            DXGI_ADAPTER_DESC2*       pDesc) final;

    HRESULT STDMETHODCALLTYPE GetDesc3(
            DXGI_ADAPTER_DESC3*       pDesc) final;

    HRESULT STDMETHODCALLTYPE QueryVideoMemoryInfo(
            UINT                          NodeIndex,
            DXGI_MEMORY_SEGMENT_GROUP     MemorySegmentGroup,
            DXGI_QUERY_VIDEO_MEMORY_INFO* pVideoMemoryInfo) final;

    HRESULT STDMETHODCALLTYPE SetVideoMemoryReservation(
            UINT                          NodeIndex,
            DXGI_MEMORY_SEGMENT_GROUP     MemorySegmentGroup,
            UINT64                        Reservation) final;

    HRESULT STDMETHODCALLTYPE RegisterHardwareContentProtectionTeardownStatusEvent(
            HANDLE                        hEvent,
            DWORD*                        pdwCookie) final;

    HRESULT STDMETHODCALLTYPE RegisterVideoMemoryBudgetChangeNotificationEvent(
            HANDLE                        hEvent,
            DWORD*                        pdwCookie) final;

    void STDMETHODCALLTYPE UnregisterHardwareContentProtectionTeardownStatus(
            DWORD                         dwCookie) final;

    void STDMETHODCALLTYPE UnregisterVideoMemoryBudgetChangeNotification(
            DWORD                         dwCookie) final;

  private:

    Com<DxgiFactory>  m_factory;
    UINT              m_index;

  };

}

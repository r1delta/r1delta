#include "d3d11_context_def.h"
#include "d3d11_device.h"
#include "d3d11_dummydata.h"

namespace dxvk {

  D3D11DeferredContext::D3D11DeferredContext(D3D11Device* pParent, UINT ContextFlags)
  : D3D11DeviceContext(pParent),
    m_contextFlags(ContextFlags) {
  }


  D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE D3D11DeferredContext::GetType() {
    return D3D11_DEVICE_CONTEXT_DEFERRED;
  }


  UINT STDMETHODCALLTYPE D3D11DeferredContext::GetContextFlags() {
    return m_contextFlags;
  }


  HRESULT STDMETHODCALLTYPE D3D11DeferredContext::GetData(
          ID3D11Asynchronous*               pAsync,
          void*                             pData,
          UINT                              DataSize,
          UINT                              GetDataFlags) {
    return DXGI_ERROR_INVALID_CALL;
  }


  void STDMETHODCALLTYPE D3D11DeferredContext::Begin(
          ID3D11Asynchronous*         pAsync) {
  }


  void STDMETHODCALLTYPE D3D11DeferredContext::End(
          ID3D11Asynchronous*         pAsync) {
  }


  void STDMETHODCALLTYPE D3D11DeferredContext::Flush() {
  }


  void STDMETHODCALLTYPE D3D11DeferredContext::Flush1(
          D3D11_CONTEXT_TYPE          ContextType,
          HANDLE                      hEvent) {
  }


  HRESULT STDMETHODCALLTYPE D3D11DeferredContext::Signal(
          ID3D11Fence*                pFence,
          UINT64                      Value) {
    return DXGI_ERROR_INVALID_CALL;
  }


  HRESULT STDMETHODCALLTYPE D3D11DeferredContext::Wait(
          ID3D11Fence*                pFence,
          UINT64                      Value) {
    return DXGI_ERROR_INVALID_CALL;
  }


  void STDMETHODCALLTYPE D3D11DeferredContext::ExecuteCommandList(
          ID3D11CommandList*  pCommandList,
          BOOL                RestoreContextState) {
  }


  HRESULT STDMETHODCALLTYPE D3D11DeferredContext::FinishCommandList(
          BOOL                RestoreDeferredContextState,
          ID3D11CommandList   **ppCommandList) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DeferredContext::Map(
          ID3D11Resource*             pResource,
          UINT                        Subresource,
          D3D11_MAP                   MapType,
          UINT                        MapFlags,
          D3D11_MAPPED_SUBRESOURCE*   pMappedResource) {
    if (unlikely(!pResource || !pMappedResource))
      return E_INVALIDARG;

    D3D11_RESOURCE_DIMENSION resourceDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    pResource->GetType(&resourceDim);

    D3D11DeferredContextMapEntry entry;

    HRESULT status = resourceDim == D3D11_RESOURCE_DIMENSION_BUFFER
      ? MapBuffer(pResource,              MapType, MapFlags, &entry)
      : MapImage (pResource, Subresource, MapType, MapFlags, &entry);

    if (unlikely(FAILED(status))) {
      *pMappedResource = D3D11_MAPPED_SUBRESOURCE();
      return status;
    }

    pMappedResource->pData      = entry.MapPointer;
    pMappedResource->RowPitch   = entry.RowPitch;
    pMappedResource->DepthPitch = entry.DepthPitch;
    return S_OK;
  }


  void STDMETHODCALLTYPE D3D11DeferredContext::Unmap(
          ID3D11Resource*             pResource,
          UINT                        Subresource) {
  }


  void STDMETHODCALLTYPE D3D11DeferredContext::SwapDeviceContextState(
          ID3DDeviceContextState*           pState,
          ID3DDeviceContextState**          ppPreviousState) {
  }


  HRESULT D3D11DeferredContext::MapBuffer(
          ID3D11Resource*               pResource,
          D3D11_MAP                     MapType,
          UINT                          MapFlags,
          D3D11DeferredContextMapEntry* pMapEntry) {
    D3D11Buffer* pBuffer = static_cast<D3D11Buffer*>(pResource);
    pMapEntry->pResource    = pResource;
    pMapEntry->Subresource  = 0;
    pMapEntry->MapType      = D3D11_MAP_WRITE_DISCARD;
    pMapEntry->RowPitch     = pBuffer->Desc()->ByteWidth;
    pMapEntry->DepthPitch   = pBuffer->Desc()->ByteWidth;
    pMapEntry->MapPointer   = dummyDataPtr;
    return S_OK;
  }


  HRESULT D3D11DeferredContext::MapImage(
          ID3D11Resource*               pResource,
          UINT                          Subresource,
          D3D11_MAP                     MapType,
          UINT                          MapFlags,
          D3D11DeferredContextMapEntry* pMapEntry) {
    pMapEntry->pResource    = pResource;
    pMapEntry->Subresource  = Subresource;
    pMapEntry->MapType      = D3D11_MAP_WRITE_DISCARD;
    pMapEntry->RowPitch     = 1; // dummy
    pMapEntry->DepthPitch   = 1; // dummy
    pMapEntry->MapPointer   = dummyDataPtr;
    return S_OK;
  }

}
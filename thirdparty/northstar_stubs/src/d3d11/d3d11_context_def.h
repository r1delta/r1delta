#pragma once

#include "d3d11_buffer.h"
#include "d3d11_cmdlist.h"
#include "d3d11_context.h"

namespace dxvk {

  struct D3D11DeferredContextMapEntry {
    Com<ID3D11Resource>     pResource;
    UINT                    Subresource;
    D3D11_MAP               MapType;
    UINT                    RowPitch;
    UINT                    DepthPitch;
    void*                   MapPointer;
  };

  class D3D11DeferredContext : public D3D11DeviceContext {

  public:

    D3D11DeferredContext(D3D11Device* pParent, UINT ContextFlags);

    D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType();

    UINT STDMETHODCALLTYPE GetContextFlags();

    HRESULT STDMETHODCALLTYPE GetData(
            ID3D11Asynchronous*         pAsync,
            void*                       pData,
            UINT                        DataSize,
            UINT                        GetDataFlags);

    void STDMETHODCALLTYPE Begin(
            ID3D11Asynchronous*         pAsync);

    void STDMETHODCALLTYPE End(
            ID3D11Asynchronous*         pAsync);

    void STDMETHODCALLTYPE Flush();

    void STDMETHODCALLTYPE Flush1(
            D3D11_CONTEXT_TYPE          ContextType,
            HANDLE                      hEvent);

    HRESULT STDMETHODCALLTYPE Signal(
            ID3D11Fence*                pFence,
            UINT64                      Value);

    HRESULT STDMETHODCALLTYPE Wait(
            ID3D11Fence*                pFence,
            UINT64                      Value);

    void STDMETHODCALLTYPE ExecuteCommandList(
            ID3D11CommandList*          pCommandList,
            BOOL                        RestoreContextState);

    HRESULT STDMETHODCALLTYPE FinishCommandList(
            BOOL                        RestoreDeferredContextState,
            ID3D11CommandList**         ppCommandList);

    HRESULT STDMETHODCALLTYPE Map(
            ID3D11Resource*             pResource,
            UINT                        Subresource,
            D3D11_MAP                   MapType,
            UINT                        MapFlags,
            D3D11_MAPPED_SUBRESOURCE*   pMappedResource);

    void STDMETHODCALLTYPE Unmap(
            ID3D11Resource*             pResource,
            UINT                        Subresource);

    void STDMETHODCALLTYPE SwapDeviceContextState(
           ID3DDeviceContextState*           pState,
           ID3DDeviceContextState**          ppPreviousState);

  private:

    const UINT m_contextFlags;

    HRESULT MapBuffer(
            ID3D11Resource*               pResource,
            D3D11_MAP                     MapType,
            UINT                          MapFlags,
            D3D11DeferredContextMapEntry* pMapEntry);

    HRESULT MapImage(
            ID3D11Resource*               pResource,
            UINT                          Subresource,
            D3D11_MAP                     MapType,
            UINT                          MapFlags,
            D3D11DeferredContextMapEntry* pMapEntry);

  };

}

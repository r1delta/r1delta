#pragma once

#include "com_private_data.h"
#include "d3d11_include.h"
#include "d3d11_state_object.h"
#include "dxgi_object.h"

namespace dxvk {

  class D3D11DXGIDevice;
  class D3D11ImmediateContext;


  class D3D11Device final : public ID3D11Device5 {
  public:

    D3D11Device(
            D3D11DXGIDevice*        pContainer,
            D3D_FEATURE_LEVEL       FeatureLevel,
            UINT                    FeatureFlags);

    ~D3D11Device();

    ULONG STDMETHODCALLTYPE AddRef();

    ULONG STDMETHODCALLTYPE Release();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID                  riid,
            void**                  ppvObject);

    HRESULT STDMETHODCALLTYPE CreateBuffer(
      const D3D11_BUFFER_DESC*      pDesc,
      const D3D11_SUBRESOURCE_DATA* pInitialData,
            ID3D11Buffer**          ppBuffer);

    HRESULT STDMETHODCALLTYPE CreateTexture1D(
      const D3D11_TEXTURE1D_DESC*   pDesc,
      const D3D11_SUBRESOURCE_DATA* pInitialData,
            ID3D11Texture1D**       ppTexture1D);

    HRESULT STDMETHODCALLTYPE CreateTexture2D(
      const D3D11_TEXTURE2D_DESC*   pDesc,
      const D3D11_SUBRESOURCE_DATA* pInitialData,
            ID3D11Texture2D**       ppTexture2D);

    HRESULT STDMETHODCALLTYPE CreateTexture2D1(
      const D3D11_TEXTURE2D_DESC1*  pDesc,
      const D3D11_SUBRESOURCE_DATA* pInitialData,
            ID3D11Texture2D1**      ppTexture2D);

    HRESULT STDMETHODCALLTYPE CreateTexture3D(
      const D3D11_TEXTURE3D_DESC*   pDesc,
      const D3D11_SUBRESOURCE_DATA* pInitialData,
            ID3D11Texture3D**       ppTexture3D);

    HRESULT STDMETHODCALLTYPE CreateTexture3D1(
      const D3D11_TEXTURE3D_DESC1*  pDesc,
      const D3D11_SUBRESOURCE_DATA* pInitialData,
            ID3D11Texture3D1**      ppTexture3D);

    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(
            ID3D11Resource*                   pResource,
      const D3D11_SHADER_RESOURCE_VIEW_DESC*  pDesc,
            ID3D11ShaderResourceView**        ppSRView);

    HRESULT STDMETHODCALLTYPE CreateShaderResourceView1(
            ID3D11Resource*                   pResource,
      const D3D11_SHADER_RESOURCE_VIEW_DESC1* pDesc,
            ID3D11ShaderResourceView1**       ppSRView);

    HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(
            ID3D11Resource*                   pResource,
      const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,
            ID3D11UnorderedAccessView**       ppUAView);

    HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView1(
            ID3D11Resource*                   pResource,
      const D3D11_UNORDERED_ACCESS_VIEW_DESC1* pDesc,
            ID3D11UnorderedAccessView1**      ppUAView);

    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(
            ID3D11Resource*                   pResource,
      const D3D11_RENDER_TARGET_VIEW_DESC*    pDesc,
            ID3D11RenderTargetView**          ppRTView);

    HRESULT STDMETHODCALLTYPE CreateRenderTargetView1(
            ID3D11Resource*                   pResource,
      const D3D11_RENDER_TARGET_VIEW_DESC1*   pDesc,
            ID3D11RenderTargetView1**         ppRTView);

    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(
            ID3D11Resource*                   pResource,
      const D3D11_DEPTH_STENCIL_VIEW_DESC*    pDesc,
            ID3D11DepthStencilView**          ppDepthStencilView);

    HRESULT STDMETHODCALLTYPE CreateInputLayout(
      const D3D11_INPUT_ELEMENT_DESC*   pInputElementDescs,
            UINT                        NumElements,
      const void*                       pShaderBytecodeWithInputSignature,
            SIZE_T                      BytecodeLength,
            ID3D11InputLayout**         ppInputLayout);

    HRESULT STDMETHODCALLTYPE CreateVertexShader(
      const void*                       pShaderBytecode,
            SIZE_T                      BytecodeLength,
            ID3D11ClassLinkage*         pClassLinkage,
            ID3D11VertexShader**        ppVertexShader);

    HRESULT STDMETHODCALLTYPE CreateGeometryShader(
      const void*                       pShaderBytecode,
            SIZE_T                      BytecodeLength,
            ID3D11ClassLinkage*         pClassLinkage,
            ID3D11GeometryShader**      ppGeometryShader);

    HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(
      const void*                       pShaderBytecode,
            SIZE_T                      BytecodeLength,
      const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,
            UINT                        NumEntries,
      const UINT*                       pBufferStrides,
            UINT                        NumStrides,
            UINT                        RasterizedStream,
            ID3D11ClassLinkage*         pClassLinkage,
            ID3D11GeometryShader**      ppGeometryShader);

    HRESULT STDMETHODCALLTYPE CreatePixelShader(
      const void*                       pShaderBytecode,
            SIZE_T                      BytecodeLength,
            ID3D11ClassLinkage*         pClassLinkage,
            ID3D11PixelShader**         ppPixelShader);

    HRESULT STDMETHODCALLTYPE CreateHullShader(
      const void*                       pShaderBytecode,
            SIZE_T                      BytecodeLength,
            ID3D11ClassLinkage*         pClassLinkage,
            ID3D11HullShader**          ppHullShader);

    HRESULT STDMETHODCALLTYPE CreateDomainShader(
      const void*                       pShaderBytecode,
            SIZE_T                      BytecodeLength,
            ID3D11ClassLinkage*         pClassLinkage,
            ID3D11DomainShader**        ppDomainShader);

    HRESULT STDMETHODCALLTYPE CreateComputeShader(
      const void*                       pShaderBytecode,
            SIZE_T                      BytecodeLength,
            ID3D11ClassLinkage*         pClassLinkage,
            ID3D11ComputeShader**       ppComputeShader);

    HRESULT STDMETHODCALLTYPE CreateClassLinkage(
            ID3D11ClassLinkage**        ppLinkage);

    HRESULT STDMETHODCALLTYPE CreateBlendState(
      const D3D11_BLEND_DESC*           pBlendStateDesc,
            ID3D11BlendState**          ppBlendState);

    HRESULT STDMETHODCALLTYPE CreateBlendState1(
      const D3D11_BLEND_DESC1*          pBlendStateDesc,
            ID3D11BlendState1**         ppBlendState);

    HRESULT STDMETHODCALLTYPE CreateDepthStencilState(
      const D3D11_DEPTH_STENCIL_DESC*   pDepthStencilDesc,
            ID3D11DepthStencilState**   ppDepthStencilState);

    HRESULT STDMETHODCALLTYPE CreateRasterizerState(
      const D3D11_RASTERIZER_DESC*      pRasterizerDesc,
            ID3D11RasterizerState**     ppRasterizerState);

    HRESULT STDMETHODCALLTYPE CreateRasterizerState1(
      const D3D11_RASTERIZER_DESC1*     pRasterizerDesc,
            ID3D11RasterizerState1**    ppRasterizerState);

    HRESULT STDMETHODCALLTYPE CreateRasterizerState2(
      const D3D11_RASTERIZER_DESC2*     pRasterizerDesc,
            ID3D11RasterizerState2**    ppRasterizerState);

    HRESULT STDMETHODCALLTYPE CreateSamplerState(
      const D3D11_SAMPLER_DESC*         pSamplerDesc,
            ID3D11SamplerState**        ppSamplerState);

    HRESULT STDMETHODCALLTYPE CreateQuery(
      const D3D11_QUERY_DESC*           pQueryDesc,
            ID3D11Query**               ppQuery);

    HRESULT STDMETHODCALLTYPE CreateQuery1(
      const D3D11_QUERY_DESC1*          pQueryDesc,
            ID3D11Query1**              ppQuery);

    HRESULT STDMETHODCALLTYPE CreatePredicate(
      const D3D11_QUERY_DESC*           pPredicateDesc,
            ID3D11Predicate**           ppPredicate);

    HRESULT STDMETHODCALLTYPE CreateCounter(
      const D3D11_COUNTER_DESC*         pCounterDesc,
            ID3D11Counter**             ppCounter);

    HRESULT STDMETHODCALLTYPE CreateDeferredContext(
            UINT                        ContextFlags,
            ID3D11DeviceContext**       ppDeferredContext);

    HRESULT STDMETHODCALLTYPE CreateDeferredContext1(
            UINT                        ContextFlags,
            ID3D11DeviceContext1**      ppDeferredContext);

    HRESULT STDMETHODCALLTYPE CreateDeferredContext2(
            UINT                        ContextFlags,
            ID3D11DeviceContext2**      ppDeferredContext);

    HRESULT STDMETHODCALLTYPE CreateDeferredContext3(
            UINT                        ContextFlags,
            ID3D11DeviceContext3**      ppDeferredContext);

    HRESULT STDMETHODCALLTYPE CreateDeviceContextState(
            UINT                        Flags,
      const D3D_FEATURE_LEVEL*          pFeatureLevels,
            UINT                        FeatureLevels,
            UINT                        SDKVersion,
            REFIID                      EmulatedInterface,
            D3D_FEATURE_LEVEL*          pChosenFeatureLevel,
            ID3DDeviceContextState**    ppContextState);

    HRESULT STDMETHODCALLTYPE CreateFence(
            UINT64                      InitialValue,
            D3D11_FENCE_FLAG            Flags,
            REFIID                      ReturnedInterface,
            void**                      ppFence);

    void STDMETHODCALLTYPE ReadFromSubresource(
            void*                       pDstData,
            UINT                        DstRowPitch,
            UINT                        DstDepthPitch,
            ID3D11Resource*             pSrcResource,
            UINT                        SrcSubresource,
      const D3D11_BOX*                  pSrcBox);

    void STDMETHODCALLTYPE WriteToSubresource(
            ID3D11Resource*             pDstResource,
            UINT                        DstSubresource,
      const D3D11_BOX*                  pDstBox,
      const void*                       pSrcData,
            UINT                        SrcRowPitch,
            UINT                        SrcDepthPitch);

    HRESULT STDMETHODCALLTYPE OpenSharedResource(
            HANDLE      hResource,
            REFIID      ReturnedInterface,
            void**      ppResource);

    HRESULT STDMETHODCALLTYPE OpenSharedResource1(
            HANDLE      hResource,
            REFIID      returnedInterface,
            void**      ppResource);

    HRESULT STDMETHODCALLTYPE OpenSharedResourceByName(
            LPCWSTR     lpName,
            DWORD       dwDesiredAccess,
            REFIID      returnedInterface,
            void**      ppResource);

    HRESULT STDMETHODCALLTYPE OpenSharedFence(
            HANDLE      hFence,
            REFIID      ReturnedInterface,
            void**      ppFence);

    HRESULT STDMETHODCALLTYPE CheckFormatSupport(
            DXGI_FORMAT Format,
            UINT*       pFormatSupport);

    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(
            DXGI_FORMAT Format,
            UINT        SampleCount,
            UINT*       pNumQualityLevels);

    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels1(
            DXGI_FORMAT Format,
            UINT        SampleCount,
            UINT        Flags,
            UINT*       pNumQualityLevels);

    void STDMETHODCALLTYPE CheckCounterInfo(
            D3D11_COUNTER_INFO* pCounterInfo);

    HRESULT STDMETHODCALLTYPE CheckCounter(
      const D3D11_COUNTER_DESC* pDesc,
            D3D11_COUNTER_TYPE* pType,
            UINT*               pActiveCounters,
            LPSTR               szName,
            UINT*               pNameLength,
            LPSTR               szUnits,
            UINT*               pUnitsLength,
            LPSTR               szDescription,
            UINT*               pDescriptionLength);

    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(
            D3D11_FEATURE Feature,
            void*         pFeatureSupportData,
            UINT          FeatureSupportDataSize);

    HRESULT STDMETHODCALLTYPE GetPrivateData(
            REFGUID Name,
            UINT    *pDataSize,
            void    *pData);

    HRESULT STDMETHODCALLTYPE SetPrivateData(
            REFGUID Name,
            UINT    DataSize,
      const void    *pData);

    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
            REFGUID  Name,
      const IUnknown *pUnknown);

    D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel();

    UINT STDMETHODCALLTYPE GetCreationFlags();

    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason();

    void STDMETHODCALLTYPE GetImmediateContext(
            ID3D11DeviceContext** ppImmediateContext);

    void STDMETHODCALLTYPE GetImmediateContext1(
            ID3D11DeviceContext1** ppImmediateContext);

    void STDMETHODCALLTYPE GetImmediateContext2(
            ID3D11DeviceContext2** ppImmediateContext);

    void STDMETHODCALLTYPE GetImmediateContext3(
            ID3D11DeviceContext3** ppImmediateContext);

    HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT RaiseFlags);

    UINT STDMETHODCALLTYPE GetExceptionMode();

    void STDMETHODCALLTYPE GetResourceTiling(
            ID3D11Resource*           pTiledResource,
            UINT*                     pNumTilesForEntireResource,
            D3D11_PACKED_MIP_DESC*    pPackedMipDesc,
            D3D11_TILE_SHAPE*         pStandardTileShapeForNonPackedMips,
            UINT*                     pNumSubresourceTilings,
            UINT                      FirstSubresourceTilingToGet,
            D3D11_SUBRESOURCE_TILING* pSubresourceTilingsForNonPackedMips);

    HRESULT STDMETHODCALLTYPE RegisterDeviceRemovedEvent(
            HANDLE                    hEvent,
            DWORD*                    pdwCookie);

    void STDMETHODCALLTYPE UnregisterDeviceRemoved(
            DWORD                     dwCookie);

  private:

    IDXGIObject*                    m_container;

    D3D_FEATURE_LEVEL               m_featureLevel;
    UINT                            m_featureFlags;

    Com<D3D11ImmediateContext, false> m_context;

    D3D11StateObjectSet<D3D11BlendState>        m_bsStateObjects;
    D3D11StateObjectSet<D3D11DepthStencilState> m_dsStateObjects;
    D3D11StateObjectSet<D3D11RasterizerState>   m_rsStateObjects;
    D3D11StateObjectSet<D3D11SamplerState>      m_samplerObjects;

  };


  class D3D11DXGIDevice : public DxgiObject<IDXGIDevice4> {
  public:

    D3D11DXGIDevice(
            IDXGIAdapter*       pAdapter,
            D3D_FEATURE_LEVEL   FeatureLevel,
            UINT                FeatureFlags);

    ~D3D11DXGIDevice();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID                riid,
            void**                ppvObject);

    HRESULT STDMETHODCALLTYPE GetParent(
            REFIID                riid,
            void**                ppParent);

    HRESULT STDMETHODCALLTYPE CreateSurface(
      const DXGI_SURFACE_DESC*    pDesc,
            UINT                  NumSurfaces,
            DXGI_USAGE            Usage,
      const DXGI_SHARED_RESOURCE* pSharedResource,
            IDXGISurface**        ppSurface) final;

    HRESULT STDMETHODCALLTYPE GetAdapter(
            IDXGIAdapter**        pAdapter) final;

    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(
            INT*                  pPriority) final;

    HRESULT STDMETHODCALLTYPE QueryResourceResidency(
            IUnknown* const*      ppResources,
            DXGI_RESIDENCY*       pResidencyStatus,
            UINT                  NumResources) final;

    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(
            INT                   Priority) final;

    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(
            UINT*                 pMaxLatency) final;

    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(
            UINT                  MaxLatency) final;

    HRESULT STDMETHODCALLTYPE OfferResources(
            UINT                          NumResources,
            IDXGIResource* const*         ppResources,
            DXGI_OFFER_RESOURCE_PRIORITY  Priority) final;

    HRESULT STDMETHODCALLTYPE OfferResources1(
            UINT                          NumResources,
            IDXGIResource* const*         ppResources,
            DXGI_OFFER_RESOURCE_PRIORITY  Priority,
            UINT                          Flags) final;

    HRESULT STDMETHODCALLTYPE ReclaimResources(
            UINT                          NumResources,
            IDXGIResource* const*         ppResources,
            BOOL*                         pDiscarded) final;

    HRESULT STDMETHODCALLTYPE ReclaimResources1(
            UINT                          NumResources,
            IDXGIResource* const*         ppResources,
            DXGI_RECLAIM_RESOURCE_RESULTS* pResults) final;

    HRESULT STDMETHODCALLTYPE EnqueueSetEvent(
            HANDLE                hEvent) final;

    void STDMETHODCALLTYPE Trim() final;

  private:

    Com<IDXGIAdapter>        m_dxgiAdapter;
    D3D11Device              m_d3d11Device;

  };

}

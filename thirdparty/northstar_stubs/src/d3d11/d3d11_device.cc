#include "d3d11_buffer.h"
#include "d3d11_class_linkage.h"
#include "d3d11_context_def.h"
#include "d3d11_context_imm.h"
#include "d3d11_device.h"
#include "d3d11_include.h"
#include "d3d11_input_layout.h"
#include "d3d11_query.h"
#include "d3d11_resource.h"
#include "d3d11_sampler.h"
#include "d3d11_shader.h"
#include "d3d11_state_object.h"
#include "d3d11_swapchain.h"
#include "d3d11_texture.h"

namespace dxvk {

  D3D11Device::D3D11Device(
          D3D11DXGIDevice*    pContainer,
          D3D_FEATURE_LEVEL   FeatureLevel,
          UINT                FeatureFlags)
  : m_container     (pContainer),
    m_featureLevel  (FeatureLevel),
    m_featureFlags  (FeatureFlags) {
    m_context     = new D3D11ImmediateContext(this);
  }


  D3D11Device::~D3D11Device() {
    m_context = nullptr;
  }


  ULONG STDMETHODCALLTYPE D3D11Device::AddRef() {
    return m_container->AddRef();
  }


  ULONG STDMETHODCALLTYPE D3D11Device::Release() {
    return m_container->Release();
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::QueryInterface(REFIID riid, void** ppvObject) {
    return m_container->QueryInterface(riid, ppvObject);
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateBuffer(
    const D3D11_BUFFER_DESC*      pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
          ID3D11Buffer**          ppBuffer) {
    InitReturnPtr(ppBuffer);

    if (!pDesc)
      return E_INVALIDARG;

    if (!ppBuffer)
      return S_FALSE;

    const Com<D3D11Buffer> buffer = new D3D11Buffer(this, pDesc);
    *ppBuffer = buffer.ref();
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture1D(
    const D3D11_TEXTURE1D_DESC*   pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
          ID3D11Texture1D**       ppTexture1D) {
    InitReturnPtr(ppTexture1D);

    if (!pDesc)
      return E_INVALIDARG;

    D3D11_COMMON_TEXTURE_DESC desc;
    desc.Width          = pDesc->Width;
    desc.Height         = 1;
    desc.Depth          = 1;
    desc.MipLevels      = pDesc->MipLevels;
    desc.ArraySize      = pDesc->ArraySize;
    desc.Format         = pDesc->Format;
    desc.SampleDesc     = DXGI_SAMPLE_DESC { 1, 0 };
    desc.Usage          = pDesc->Usage;
    desc.BindFlags      = pDesc->BindFlags;
    desc.CPUAccessFlags = pDesc->CPUAccessFlags;
    desc.MiscFlags      = pDesc->MiscFlags;
    desc.TextureLayout  = D3D11_TEXTURE_LAYOUT_UNDEFINED;

    if (!ppTexture1D)
      return S_FALSE;

    const Com<D3D11Texture1D> texture = new D3D11Texture1D(this, &desc);
    *ppTexture1D = texture.ref();
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture2D(
    const D3D11_TEXTURE2D_DESC*   pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
          ID3D11Texture2D**       ppTexture2D) {
    InitReturnPtr(ppTexture2D);

    if (!pDesc)
      return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC1 desc;
    desc.Width          = pDesc->Width;
    desc.Height         = pDesc->Height;
    desc.MipLevels      = pDesc->MipLevels;
    desc.ArraySize      = pDesc->ArraySize;
    desc.Format         = pDesc->Format;
    desc.SampleDesc     = pDesc->SampleDesc;
    desc.Usage          = pDesc->Usage;
    desc.BindFlags      = pDesc->BindFlags;
    desc.CPUAccessFlags = pDesc->CPUAccessFlags;
    desc.MiscFlags      = pDesc->MiscFlags;
    desc.TextureLayout  = D3D11_TEXTURE_LAYOUT_UNDEFINED;

    ID3D11Texture2D1* texture2D = nullptr;
    HRESULT hr = CreateTexture2D1(&desc, pInitialData, ppTexture2D ? &texture2D : nullptr);

    if (hr != S_OK)
      return hr;

    *ppTexture2D = texture2D;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture2D1(
    const D3D11_TEXTURE2D_DESC1*  pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
          ID3D11Texture2D1**      ppTexture2D) {
    InitReturnPtr(ppTexture2D);

    if (!pDesc)
      return E_INVALIDARG;

    D3D11_COMMON_TEXTURE_DESC desc;
    desc.Width          = pDesc->Width;
    desc.Height         = pDesc->Height;
    desc.Depth          = 1;
    desc.MipLevels      = pDesc->MipLevels;
    desc.ArraySize      = pDesc->ArraySize;
    desc.Format         = pDesc->Format;
    desc.SampleDesc     = pDesc->SampleDesc;
    desc.Usage          = pDesc->Usage;
    desc.BindFlags      = pDesc->BindFlags;
    desc.CPUAccessFlags = pDesc->CPUAccessFlags;
    desc.MiscFlags      = pDesc->MiscFlags;
    desc.TextureLayout  = pDesc->TextureLayout;

    if (!ppTexture2D)
      return S_FALSE;

    Com<D3D11Texture2D> texture = new D3D11Texture2D(this, &desc);
    *ppTexture2D = texture.ref();
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture3D(
    const D3D11_TEXTURE3D_DESC*   pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
          ID3D11Texture3D**       ppTexture3D) {
    InitReturnPtr(ppTexture3D);

    if (!pDesc)
      return E_INVALIDARG;

    D3D11_TEXTURE3D_DESC1 desc;
    desc.Width          = pDesc->Width;
    desc.Height         = pDesc->Height;
    desc.Depth          = pDesc->Depth;
    desc.MipLevels      = pDesc->MipLevels;
    desc.Format         = pDesc->Format;
    desc.Usage          = pDesc->Usage;
    desc.BindFlags      = pDesc->BindFlags;
    desc.CPUAccessFlags = pDesc->CPUAccessFlags;
    desc.MiscFlags      = pDesc->MiscFlags;
    desc.TextureLayout  = D3D11_TEXTURE_LAYOUT_UNDEFINED;

    ID3D11Texture3D1* texture3D = nullptr;
    HRESULT hr = CreateTexture3D1(&desc, pInitialData, ppTexture3D ? &texture3D : nullptr);

    if (hr != S_OK)
      return hr;

    *ppTexture3D = texture3D;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateTexture3D1(
    const D3D11_TEXTURE3D_DESC1*  pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
          ID3D11Texture3D1**      ppTexture3D) {
    InitReturnPtr(ppTexture3D);

    if (!pDesc)
      return E_INVALIDARG;

    D3D11_COMMON_TEXTURE_DESC desc;
    desc.Width          = pDesc->Width;
    desc.Height         = pDesc->Height;
    desc.Depth          = pDesc->Depth;
    desc.MipLevels      = pDesc->MipLevels;
    desc.ArraySize      = 1;
    desc.Format         = pDesc->Format;
    desc.SampleDesc     = DXGI_SAMPLE_DESC { 1, 0 };
    desc.Usage          = pDesc->Usage;
    desc.BindFlags      = pDesc->BindFlags;
    desc.CPUAccessFlags = pDesc->CPUAccessFlags;
    desc.MiscFlags      = pDesc->MiscFlags;
    desc.TextureLayout  = pDesc->TextureLayout;

    if (!ppTexture3D)
      return S_FALSE;

    Com<D3D11Texture3D> texture = new D3D11Texture3D(this, &desc);
    *ppTexture3D = texture.ref();
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateShaderResourceView(
          ID3D11Resource*                   pResource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC*  pDesc,
          ID3D11ShaderResourceView**        ppSRView) {
    InitReturnPtr(ppSRView);

    D3D11_SHADER_RESOURCE_VIEW_DESC1 desc = pDesc
      ? D3D11ShaderResourceView::PromoteDesc(pDesc, 0)
      : D3D11_SHADER_RESOURCE_VIEW_DESC1();

    ID3D11ShaderResourceView1* view = nullptr;

    HRESULT hr = CreateShaderResourceView1(pResource,
      pDesc    ? &desc : nullptr,
      ppSRView ? &view : nullptr);

    if (hr != S_OK)
      return hr;

    *ppSRView = view;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateShaderResourceView1(
          ID3D11Resource*                   pResource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC1* pDesc,
          ID3D11ShaderResourceView1**       ppSRView) {
    InitReturnPtr(ppSRView);

    if (!pResource)
      return E_INVALIDARG;

    D3D11_COMMON_RESOURCE_DESC resourceDesc;
    GetCommonResourceDesc(pResource, &resourceDesc);

    // The description is optional. If omitted, we'll create
    // a view that covers all subresources of the image.
    D3D11_SHADER_RESOURCE_VIEW_DESC1 desc;

    if (!pDesc) {
      if (FAILED(D3D11ShaderResourceView::GetDescFromResource(pResource, &desc)))
        return E_INVALIDARG;
    } else {
      desc = *pDesc;
    }

    if (!ppSRView)
      return S_FALSE;

    *ppSRView = ref(new D3D11ShaderResourceView(this, pResource, &desc));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateUnorderedAccessView(
          ID3D11Resource*                   pResource,
    const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,
          ID3D11UnorderedAccessView**       ppUAView) {
    InitReturnPtr(ppUAView);

    D3D11_UNORDERED_ACCESS_VIEW_DESC1 desc = pDesc
      ? D3D11UnorderedAccessView::PromoteDesc(pDesc, 0)
      : D3D11_UNORDERED_ACCESS_VIEW_DESC1();

    ID3D11UnorderedAccessView1* view = nullptr;

    HRESULT hr = CreateUnorderedAccessView1(pResource,
      pDesc    ? &desc : nullptr,
      ppUAView ? &view : nullptr);

    if (hr != S_OK)
      return hr;

    *ppUAView = view;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateUnorderedAccessView1(
          ID3D11Resource*                   pResource,
    const D3D11_UNORDERED_ACCESS_VIEW_DESC1* pDesc,
          ID3D11UnorderedAccessView1**      ppUAView) {
    InitReturnPtr(ppUAView);

    if (!pResource)
      return E_INVALIDARG;

    D3D11_COMMON_RESOURCE_DESC resourceDesc;
    GetCommonResourceDesc(pResource, &resourceDesc);

    // The description is optional. If omitted, we'll create
    // a view that covers all subresources of the image.
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 desc;

    if (!pDesc) {
      if (FAILED(D3D11UnorderedAccessView::GetDescFromResource(pResource, &desc)))
        return E_INVALIDARG;
    } else {
      desc = *pDesc;
    }

    if (!ppUAView)
      return S_FALSE;

    auto uav = new D3D11UnorderedAccessView(this, pResource, &desc);
    *ppUAView = ref(uav);
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateRenderTargetView(
          ID3D11Resource*                   pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC*    pDesc,
          ID3D11RenderTargetView**          ppRTView) {
    InitReturnPtr(ppRTView);

    D3D11_RENDER_TARGET_VIEW_DESC1 desc = pDesc
      ? D3D11RenderTargetView::PromoteDesc(pDesc, 0)
      : D3D11_RENDER_TARGET_VIEW_DESC1();

    ID3D11RenderTargetView1* view = nullptr;

    HRESULT hr = CreateRenderTargetView1(pResource,
      pDesc    ? &desc : nullptr,
      ppRTView ? &view : nullptr);

    if (hr != S_OK)
      return hr;

    *ppRTView = view;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateRenderTargetView1(
          ID3D11Resource*                   pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC1*   pDesc,
          ID3D11RenderTargetView1**         ppRTView) {
    InitReturnPtr(ppRTView);

    if (!pResource)
      return E_INVALIDARG;

    D3D11_RENDER_TARGET_VIEW_DESC1 desc;

    if (!pDesc) {
      if (FAILED(D3D11RenderTargetView::GetDescFromResource(pResource, &desc)))
        return E_INVALIDARG;
    } else {
      desc = *pDesc;
    }

    if (!ppRTView)
      return S_FALSE;

    *ppRTView = ref(new D3D11RenderTargetView(this, pResource, &desc));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDepthStencilView(
          ID3D11Resource*                   pResource,
    const D3D11_DEPTH_STENCIL_VIEW_DESC*    pDesc,
          ID3D11DepthStencilView**          ppDepthStencilView) {
    InitReturnPtr(ppDepthStencilView);

    if (pResource == nullptr)
      return E_INVALIDARG;

    D3D11_COMMON_RESOURCE_DESC resourceDesc;
    GetCommonResourceDesc(pResource, &resourceDesc);

    // The view description is optional. If not defined, it
    // will use the resource's format and all array layers.
    D3D11_DEPTH_STENCIL_VIEW_DESC desc;

    if (pDesc == nullptr) {
      if (FAILED(D3D11DepthStencilView::GetDescFromResource(pResource, &desc)))
        return E_INVALIDARG;
    } else {
      desc = *pDesc;
    }

    if (ppDepthStencilView == nullptr)
      return S_FALSE;

    *ppDepthStencilView = ref(new D3D11DepthStencilView(this, pResource, &desc));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateInputLayout(
    const D3D11_INPUT_ELEMENT_DESC*   pInputElementDescs,
          UINT                        NumElements,
    const void*                       pShaderBytecodeWithInputSignature,
          SIZE_T                      BytecodeLength,
          ID3D11InputLayout**         ppInputLayout) {
    InitReturnPtr(ppInputLayout);

    if (pInputElementDescs == nullptr)
      return E_INVALIDARG;

    // Create the actual input layout object
    // if the application requests it.
    if (ppInputLayout != nullptr) {
      *ppInputLayout = ref(new D3D11InputLayout(this));
    }

    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateVertexShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11VertexShader**        ppVertexShader) {
    InitReturnPtr(ppVertexShader);

    if (!ppVertexShader)
      return S_FALSE;

    *ppVertexShader = ref(new D3D11VertexShader(this));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateGeometryShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11GeometryShader**      ppGeometryShader) {
    InitReturnPtr(ppGeometryShader);

    if (!ppGeometryShader)
      return S_FALSE;

    *ppGeometryShader = ref(new D3D11GeometryShader(this));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateGeometryShaderWithStreamOutput(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
    const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,
          UINT                        NumEntries,
    const UINT*                       pBufferStrides,
          UINT                        NumStrides,
          UINT                        RasterizedStream,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11GeometryShader**      ppGeometryShader) {
    InitReturnPtr(ppGeometryShader);

    if (!ppGeometryShader)
      return S_FALSE;

    *ppGeometryShader = ref(new D3D11GeometryShader(this));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreatePixelShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11PixelShader**         ppPixelShader) {
    InitReturnPtr(ppPixelShader);

    if (!ppPixelShader)
      return S_FALSE;

    *ppPixelShader = ref(new D3D11PixelShader(this));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateHullShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11HullShader**          ppHullShader) {
    InitReturnPtr(ppHullShader);

    if (!ppHullShader)
      return S_FALSE;

    *ppHullShader = ref(new D3D11HullShader(this));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDomainShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11DomainShader**        ppDomainShader) {
    InitReturnPtr(ppDomainShader);

    if (ppDomainShader == nullptr)
      return S_FALSE;

    *ppDomainShader = ref(new D3D11DomainShader(this));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateComputeShader(
    const void*                       pShaderBytecode,
          SIZE_T                      BytecodeLength,
          ID3D11ClassLinkage*         pClassLinkage,
          ID3D11ComputeShader**       ppComputeShader) {
    InitReturnPtr(ppComputeShader);

    if (!ppComputeShader)
      return S_FALSE;

    *ppComputeShader = ref(new D3D11ComputeShader(this));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateClassLinkage(ID3D11ClassLinkage** ppLinkage) {
    *ppLinkage = ref(new D3D11ClassLinkage(this));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateBlendState(
    const D3D11_BLEND_DESC*           pBlendStateDesc,
          ID3D11BlendState**          ppBlendState) {
    InitReturnPtr(ppBlendState);

    if (!pBlendStateDesc)
      return E_INVALIDARG;

    D3D11_BLEND_DESC1 desc = D3D11BlendState::PromoteDesc(pBlendStateDesc);

    if (ppBlendState != nullptr) {
      *ppBlendState = m_bsStateObjects.Create(this, desc);
      return S_OK;
    } return S_FALSE;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateBlendState1(
    const D3D11_BLEND_DESC1*          pBlendStateDesc,
          ID3D11BlendState1**         ppBlendState) {
    InitReturnPtr(ppBlendState);

    if (!pBlendStateDesc)
      return E_INVALIDARG;

    if (ppBlendState != nullptr) {
      *ppBlendState = m_bsStateObjects.Create(this, *pBlendStateDesc);
      return S_OK;
    } return S_FALSE;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDepthStencilState(
    const D3D11_DEPTH_STENCIL_DESC*   pDepthStencilDesc,
          ID3D11DepthStencilState**   ppDepthStencilState) {
    InitReturnPtr(ppDepthStencilState);

    if (!pDepthStencilDesc)
      return E_INVALIDARG;

    D3D11_DEPTH_STENCIL_DESC desc = *pDepthStencilDesc;

    if (ppDepthStencilState != nullptr) {
      *ppDepthStencilState = m_dsStateObjects.Create(this, desc);
      return S_OK;
    } return S_FALSE;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateRasterizerState(
    const D3D11_RASTERIZER_DESC*      pRasterizerDesc,
          ID3D11RasterizerState**     ppRasterizerState) {
    InitReturnPtr(ppRasterizerState);

    if (!pRasterizerDesc)
      return E_INVALIDARG;

    if (!ppRasterizerState)
      return S_FALSE;

    *ppRasterizerState = m_rsStateObjects.Create(this, D3D11RasterizerState::PromoteDesc(pRasterizerDesc));
    return S_OK;
  }


  HRESULT D3D11Device::CreateRasterizerState1(
    const D3D11_RASTERIZER_DESC1*     pRasterizerDesc,
          ID3D11RasterizerState1**    ppRasterizerState) {
    InitReturnPtr(ppRasterizerState);

    if (!pRasterizerDesc)
      return E_INVALIDARG;

    if (!ppRasterizerState)
      return S_FALSE;

    *ppRasterizerState = m_rsStateObjects.Create(this, D3D11RasterizerState::PromoteDesc(pRasterizerDesc));
    return S_OK;
  }


  HRESULT D3D11Device::CreateRasterizerState2(
    const D3D11_RASTERIZER_DESC2*     pRasterizerDesc,
          ID3D11RasterizerState2**    ppRasterizerState) {
    InitReturnPtr(ppRasterizerState);

    if (!pRasterizerDesc)
      return E_INVALIDARG;

    if (!ppRasterizerState)
      return S_FALSE;

    *ppRasterizerState = m_rsStateObjects.Create(this, *pRasterizerDesc);
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateSamplerState(
    const D3D11_SAMPLER_DESC*         pSamplerDesc,
          ID3D11SamplerState**        ppSamplerState) {
    InitReturnPtr(ppSamplerState);

    if (pSamplerDesc == nullptr)
      return E_INVALIDARG;

    D3D11_SAMPLER_DESC desc = *pSamplerDesc;

    if (ppSamplerState == nullptr)
      return S_FALSE;

    *ppSamplerState = m_samplerObjects.Create(this, desc);
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateQuery(
    const D3D11_QUERY_DESC*           pQueryDesc,
          ID3D11Query**               ppQuery) {
    InitReturnPtr(ppQuery);

    if (!pQueryDesc)
      return E_INVALIDARG;

    D3D11_QUERY_DESC1 desc;
    desc.Query       = pQueryDesc->Query;
    desc.MiscFlags   = pQueryDesc->MiscFlags;
    desc.ContextType = D3D11_CONTEXT_TYPE_ALL;

    ID3D11Query1* query = nullptr;
    HRESULT hr = CreateQuery1(&desc, ppQuery ? &query : nullptr);

    if (hr != S_OK)
      return hr;

    *ppQuery = query;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateQuery1(
    const D3D11_QUERY_DESC1*          pQueryDesc,
          ID3D11Query1**              ppQuery) {
    InitReturnPtr(ppQuery);

    if (!pQueryDesc)
      return E_INVALIDARG;

    if (!ppQuery)
      return S_FALSE;

    *ppQuery = ref(new D3D11Query(this, *pQueryDesc));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreatePredicate(
    const D3D11_QUERY_DESC*           pPredicateDesc,
          ID3D11Predicate**           ppPredicate) {
    InitReturnPtr(ppPredicate);

    if (!pPredicateDesc)
      return E_INVALIDARG;

    D3D11_QUERY_DESC1 desc;
    desc.Query       = pPredicateDesc->Query;
    desc.MiscFlags   = pPredicateDesc->MiscFlags;
    desc.ContextType = D3D11_CONTEXT_TYPE_ALL;

    if (desc.Query != D3D11_QUERY_OCCLUSION_PREDICATE) {
      DXVK_LOG("warn", "Unhandled predicate type: %d", pPredicateDesc->Query);
      return E_INVALIDARG;
    }

    if (!ppPredicate)
      return S_FALSE;

    *ppPredicate = D3D11Query::AsPredicate(ref(new D3D11Query(this, desc)));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateCounter(
    const D3D11_COUNTER_DESC*         pCounterDesc,
          ID3D11Counter**             ppCounter) {
    InitReturnPtr(ppCounter);

    DXVK_LOG("err", "Unsupported counter: %d", pCounterDesc->Counter);
    return E_INVALIDARG;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeferredContext(
          UINT                        ContextFlags,
          ID3D11DeviceContext**       ppDeferredContext) {
    *ppDeferredContext = ref(new D3D11DeferredContext(this, ContextFlags));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeferredContext1(
          UINT                        ContextFlags,
          ID3D11DeviceContext1**      ppDeferredContext) {
    *ppDeferredContext = ref(new D3D11DeferredContext(this, ContextFlags));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeferredContext2(
          UINT                        ContextFlags,
          ID3D11DeviceContext2**      ppDeferredContext) {
    *ppDeferredContext = ref(new D3D11DeferredContext(this, ContextFlags));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeferredContext3(
          UINT                        ContextFlags,
          ID3D11DeviceContext3**      ppDeferredContext) {
    *ppDeferredContext = ref(new D3D11DeferredContext(this, ContextFlags));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateDeviceContextState(
          UINT                        Flags,
    const D3D_FEATURE_LEVEL*          pFeatureLevels,
          UINT                        FeatureLevels,
          UINT                        SDKVersion,
          REFIID                      EmulatedInterface,
          D3D_FEATURE_LEVEL*          pChosenFeatureLevel,
          ID3DDeviceContextState**    ppContextState) {
    InitReturnPtr(ppContextState);

    if (!pFeatureLevels || FeatureLevels == 0)
      return E_INVALIDARG;

    if (EmulatedInterface != __uuidof(ID3D11Device)
     && EmulatedInterface != __uuidof(ID3D11Device1))
      return E_INVALIDARG;

    UINT flId;
    for (flId = 0; flId < FeatureLevels; flId++) {
      if (pFeatureLevels[flId] == D3D_FEATURE_LEVEL_11_1 || pFeatureLevels[flId] == D3D_FEATURE_LEVEL_11_0)
        break;
    }

    if (flId == FeatureLevels)
      return E_INVALIDARG;

    if (pFeatureLevels[flId] > m_featureLevel)
      m_featureLevel = pFeatureLevels[flId];

    if (pChosenFeatureLevel)
      *pChosenFeatureLevel = pFeatureLevels[flId];

    if (!ppContextState)
      return S_FALSE;

    *ppContextState = ref(new D3D11DeviceContextState(this));
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CreateFence(
          UINT64                      InitialValue,
          D3D11_FENCE_FLAG            Flags,
          REFIID                      ReturnedInterface,
          void**                      ppFence) {
    InitReturnPtr(ppFence);
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  void STDMETHODCALLTYPE D3D11Device::ReadFromSubresource(
          void*                       pDstData,
          UINT                        DstRowPitch,
          UINT                        DstDepthPitch,
          ID3D11Resource*             pSrcResource,
          UINT                        SrcSubresource,
    const D3D11_BOX*                  pSrcBox) {
  }


  void STDMETHODCALLTYPE D3D11Device::WriteToSubresource(
          ID3D11Resource*             pDstResource,
          UINT                        DstSubresource,
    const D3D11_BOX*                  pDstBox,
    const void*                       pSrcData,
          UINT                        SrcRowPitch,
          UINT                        SrcDepthPitch) {
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedResource(
          HANDLE      hResource,
          REFIID      ReturnedInterface,
          void**      ppResource) {
    InitReturnPtr(ppResource);
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedResource1(
          HANDLE      hResource,
          REFIID      ReturnedInterface,
          void**      ppResource) {
    InitReturnPtr(ppResource);
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedResourceByName(
          LPCWSTR     lpName,
          DWORD       dwDesiredAccess,
          REFIID      returnedInterface,
          void**      ppResource) {
    InitReturnPtr(ppResource);
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::OpenSharedFence(
          HANDLE      hFence,
          REFIID      ReturnedInterface,
          void**      ppFence) {
    InitReturnPtr(ppFence);
    DXVK_LOG_STUB();
    return E_NOTIMPL;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CheckFormatSupport(
          DXGI_FORMAT Format,
          UINT*       pFormatSupport) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CheckMultisampleQualityLevels(
          DXGI_FORMAT Format,
          UINT        SampleCount,
          UINT*       pNumQualityLevels) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CheckMultisampleQualityLevels1(
          DXGI_FORMAT Format,
          UINT        SampleCount,
          UINT        Flags,
          UINT*       pNumQualityLevels) {
    return S_OK;
  }


  void STDMETHODCALLTYPE D3D11Device::CheckCounterInfo(D3D11_COUNTER_INFO* pCounterInfo) {
    pCounterInfo->LastDeviceDependentCounter  = D3D11_COUNTER(0);
    pCounterInfo->NumSimultaneousCounters     = 0;
    pCounterInfo->NumDetectableParallelUnits  = 0;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CheckCounter(
    const D3D11_COUNTER_DESC* pDesc,
          D3D11_COUNTER_TYPE* pType,
          UINT*               pActiveCounters,
          LPSTR               szName,
          UINT*               pNameLength,
          LPSTR               szUnits,
          UINT*               pUnitsLength,
          LPSTR               szDescription,
          UINT*               pDescriptionLength) {
    return E_INVALIDARG;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::CheckFeatureSupport(
          D3D11_FEATURE Feature,
          void*         pFeatureSupportData,
          UINT          FeatureSupportDataSize) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::GetPrivateData(
          REFGUID guid, UINT* pDataSize, void* pData) {
    return m_container->GetPrivateData(guid, pDataSize, pData);
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::SetPrivateData(
          REFGUID guid, UINT DataSize, const void* pData) {
    return m_container->SetPrivateData(guid, DataSize, pData);
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::SetPrivateDataInterface(
          REFGUID guid, const IUnknown* pData) {
    return m_container->SetPrivateDataInterface(guid, pData);
  }


  D3D_FEATURE_LEVEL STDMETHODCALLTYPE D3D11Device::GetFeatureLevel() {
    return D3D_FEATURE_LEVEL_11_1;
  }


  UINT STDMETHODCALLTYPE D3D11Device::GetCreationFlags() {
    return m_featureFlags;
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::GetDeviceRemovedReason() {
    return S_OK;
  }


  void STDMETHODCALLTYPE D3D11Device::GetImmediateContext(ID3D11DeviceContext** ppImmediateContext) {
    *ppImmediateContext = m_context.ref();
  }


  void STDMETHODCALLTYPE D3D11Device::GetImmediateContext1(ID3D11DeviceContext1** ppImmediateContext) {
    *ppImmediateContext = m_context.ref();
  }


  void STDMETHODCALLTYPE D3D11Device::GetImmediateContext2(ID3D11DeviceContext2** ppImmediateContext) {
    *ppImmediateContext = m_context.ref();
  }


  void STDMETHODCALLTYPE D3D11Device::GetImmediateContext3(ID3D11DeviceContext3** ppImmediateContext) {
    *ppImmediateContext = m_context.ref();
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::SetExceptionMode(UINT RaiseFlags) {
    return S_OK;
  }


  UINT STDMETHODCALLTYPE D3D11Device::GetExceptionMode() {
    return 0;
  }


  void STDMETHODCALLTYPE D3D11Device::GetResourceTiling(
          ID3D11Resource*           pTiledResource,
          UINT*                     pNumTilesForEntireResource,
          D3D11_PACKED_MIP_DESC*    pPackedMipDesc,
          D3D11_TILE_SHAPE*         pStandardTileShapeForNonPackedMips,
          UINT*                     pNumSubresourceTilings,
          UINT                      FirstSubresourceTilingToGet,
          D3D11_SUBRESOURCE_TILING* pSubresourceTilingsForNonPackedMips) {
    if (pNumTilesForEntireResource)
      *pNumTilesForEntireResource = 0;

    if (pPackedMipDesc)
      *pPackedMipDesc = D3D11_PACKED_MIP_DESC();

    if (pStandardTileShapeForNonPackedMips)
      *pStandardTileShapeForNonPackedMips = D3D11_TILE_SHAPE();

    if (pNumSubresourceTilings) {
      if (pSubresourceTilingsForNonPackedMips) {
        for (uint32_t i = 0; i < *pNumSubresourceTilings; i++)
          pSubresourceTilingsForNonPackedMips[i] = D3D11_SUBRESOURCE_TILING();
      }

      *pNumSubresourceTilings = 0;
    }
  }


  HRESULT STDMETHODCALLTYPE D3D11Device::RegisterDeviceRemovedEvent(
          HANDLE                    hEvent,
          DWORD*                    pdwCookie) {
    return S_OK;
  }


  void STDMETHODCALLTYPE D3D11Device::UnregisterDeviceRemoved(
          DWORD                     dwCookie) {
  }


  D3D11DXGIDevice::D3D11DXGIDevice(
          IDXGIAdapter*       pAdapter,
          D3D_FEATURE_LEVEL   FeatureLevel,
          UINT                FeatureFlags)
  : m_dxgiAdapter   (pAdapter),
    m_d3d11Device   (this, FeatureLevel, FeatureFlags) {

  }


  D3D11DXGIDevice::~D3D11DXGIDevice() {

  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(IDXGIObject)
     || riid == __uuidof(IDXGIDevice)
     || riid == __uuidof(IDXGIDevice1)
     || riid == __uuidof(IDXGIDevice2)
     || riid == __uuidof(IDXGIDevice3)
     || riid == __uuidof(IDXGIDevice4)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(ID3D11Device)
     || riid == __uuidof(ID3D11Device1)
     || riid == __uuidof(ID3D11Device2)
     || riid == __uuidof(ID3D11Device3)
     || riid == __uuidof(ID3D11Device4)
     || riid == __uuidof(ID3D11Device5)) {
      *ppvObject = ref(&m_d3d11Device);
      return S_OK;
    }

    if (riid == __uuidof(ID3D11Debug))
      return E_NOINTERFACE;

    // Undocumented interfaces that are queried by some games
    if (riid == GUID{0xd56e2a4c,0x5127,0x8437,{0x65,0x8a,0x98,0xc5,0xbb,0x78,0x94,0x98}})
      return E_NOINTERFACE;

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::GetParent(
          REFIID                  riid,
          void**                  ppParent) {
    return m_dxgiAdapter->QueryInterface(riid, ppParent);
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::CreateSurface(
    const DXGI_SURFACE_DESC*    pDesc,
          UINT                  NumSurfaces,
          DXGI_USAGE            Usage,
    const DXGI_SHARED_RESOURCE* pSharedResource,
          IDXGISurface**        ppSurface) {
    if (!pDesc || (NumSurfaces && !ppSurface))
      return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC desc;
    desc.Width          = pDesc->Width;
    desc.Height         = pDesc->Height;
    desc.MipLevels      = 1;
    desc.ArraySize      = 1;
    desc.Format         = pDesc->Format;
    desc.SampleDesc     = pDesc->SampleDesc;
    desc.BindFlags      = 0;
    desc.MiscFlags      = 0;

    // Handle bind flags
    if (Usage & DXGI_USAGE_RENDER_TARGET_OUTPUT)
      desc.BindFlags |= D3D11_BIND_RENDER_TARGET;

    if (Usage & DXGI_USAGE_SHADER_INPUT)
      desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    if (Usage & DXGI_USAGE_UNORDERED_ACCESS)
      desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    // Handle CPU access flags
    switch (Usage & DXGI_CPU_ACCESS_FIELD) {
      case DXGI_CPU_ACCESS_NONE:
        desc.Usage          = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;
        break;

      case DXGI_CPU_ACCESS_DYNAMIC:
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        break;

      case DXGI_CPU_ACCESS_READ_WRITE:
      case DXGI_CPU_ACCESS_SCRATCH:
        desc.Usage          = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        break;

      default:
        return E_INVALIDARG;
    }

    // Restrictions and limitations of CreateSurface are not
    // well-documented, so we'll be a lenient on validation.
    HRESULT hr = m_d3d11Device.CreateTexture2D(&desc, nullptr, nullptr);

    if (FAILED(hr))
      return hr;

    // We don't support shared resources
    if (NumSurfaces && pSharedResource)
      DXVK_LOG("err", "CreateSurface: Shared surfaces not supported");

    // Try to create the given number of surfaces
    uint32_t surfacesCreated = 0;
    hr = S_OK;

    for (uint32_t i = 0; i < NumSurfaces; i++) {
      Com<ID3D11Texture2D> texture;

      hr = m_d3d11Device.CreateTexture2D(&desc, nullptr, &texture);

      if (SUCCEEDED(hr)) {
        hr = texture->QueryInterface(__uuidof(IDXGISurface),
          reinterpret_cast<void**>(&ppSurface[i]));
        surfacesCreated = i + 1;
      }

      if (FAILED(hr))
        break;
    }

    // Don't leak surfaces if we failed to create one
    if (FAILED(hr)) {
      for (uint32_t i = 0; i < surfacesCreated; i++)
        ppSurface[i]->Release();
    }

    return hr;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::GetAdapter(
          IDXGIAdapter**        pAdapter) {
    if (pAdapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    *pAdapter = m_dxgiAdapter.ref();
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::GetGPUThreadPriority(
          INT*                  pPriority) {
    *pPriority = 0;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::QueryResourceResidency(
          IUnknown* const*      ppResources,
          DXGI_RESIDENCY*       pResidencyStatus,
          UINT                  NumResources) {
    if (!ppResources || !pResidencyStatus)
      return E_INVALIDARG;

    for (uint32_t i = 0; i < NumResources; i++)
      pResidencyStatus[i] = DXGI_RESIDENCY_FULLY_RESIDENT;

    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::SetGPUThreadPriority(
          INT                   Priority) {
    if (Priority < -7 || Priority > 7)
      return E_INVALIDARG;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::GetMaximumFrameLatency(
          UINT*                 pMaxLatency) {
    if (!pMaxLatency)
      return DXGI_ERROR_INVALID_CALL;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::SetMaximumFrameLatency(
          UINT                  MaxLatency) {
    if (MaxLatency == 0)
      MaxLatency = 1;

    if (MaxLatency > DXGI_MAX_SWAP_CHAIN_BUFFERS)
      return DXGI_ERROR_INVALID_CALL;

    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::OfferResources(
          UINT                          NumResources,
          IDXGIResource* const*         ppResources,
          DXGI_OFFER_RESOURCE_PRIORITY  Priority) {
    return OfferResources1(NumResources, ppResources, Priority, 0);
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::OfferResources1(
          UINT                          NumResources,
          IDXGIResource* const*         ppResources,
          DXGI_OFFER_RESOURCE_PRIORITY  Priority,
          UINT                          Flags) {
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::ReclaimResources(
          UINT                          NumResources,
          IDXGIResource* const*         ppResources,
          BOOL*                         pDiscarded) {
    if (pDiscarded)
      *pDiscarded = false;
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::ReclaimResources1(
          UINT                          NumResources,
          IDXGIResource* const*         ppResources,
          DXGI_RECLAIM_RESOURCE_RESULTS* pResults) {
    if (pResults) {
      for (uint32_t i = 0; i < NumResources; i++)
        pResults[i] = DXGI_RECLAIM_RESOURCE_RESULT_OK;
    }
    return S_OK;
  }


  HRESULT STDMETHODCALLTYPE D3D11DXGIDevice::EnqueueSetEvent(HANDLE hEvent) {
    DXVK_LOG_STUB();
    return DXGI_ERROR_UNSUPPORTED;
  }

  void STDMETHODCALLTYPE D3D11DXGIDevice::Trim() {
  }

}

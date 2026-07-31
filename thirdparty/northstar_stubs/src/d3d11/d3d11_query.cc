#include "d3d11_device.h"
#include "d3d11_include.h"
#include "d3d11_query.h"

namespace dxvk {

  D3D11Query::D3D11Query(
          D3D11Device*       device,
    const D3D11_QUERY_DESC1& desc)
  : D3D11DeviceChild<ID3D11Query1>(device),
    m_desc(desc) {
  }


  D3D11Query::~D3D11Query() {
  }


  HRESULT STDMETHODCALLTYPE D3D11Query::QueryInterface(REFIID  riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11Asynchronous)
     || riid == __uuidof(ID3D11Query)
     || riid == __uuidof(ID3D11Query1)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (m_desc.Query == D3D11_QUERY_OCCLUSION_PREDICATE) {
      if (riid == __uuidof(ID3D11Predicate)) {
        *ppvObject = AsPredicate(ref(this));
        return S_OK;
      }
    }

    DXVK_LOG_UNK_IFACE(riid);
    return E_NOINTERFACE;
  }


  UINT STDMETHODCALLTYPE D3D11Query::GetDataSize() {
    switch (m_desc.Query) {
      case D3D11_QUERY_EVENT:
        return sizeof(BOOL);

      case D3D11_QUERY_OCCLUSION:
        return sizeof(UINT64);

      case D3D11_QUERY_TIMESTAMP:
        return sizeof(UINT64);

      case D3D11_QUERY_TIMESTAMP_DISJOINT:
        return sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT);

      case D3D11_QUERY_PIPELINE_STATISTICS:
        return sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS);

      case D3D11_QUERY_OCCLUSION_PREDICATE:
        return sizeof(BOOL);

      case D3D11_QUERY_SO_STATISTICS:
      case D3D11_QUERY_SO_STATISTICS_STREAM0:
      case D3D11_QUERY_SO_STATISTICS_STREAM1:
      case D3D11_QUERY_SO_STATISTICS_STREAM2:
      case D3D11_QUERY_SO_STATISTICS_STREAM3:
        return sizeof(D3D11_QUERY_DATA_SO_STATISTICS);

      case D3D11_QUERY_SO_OVERFLOW_PREDICATE:
      case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0:
      case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1:
      case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2:
      case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3:
        return sizeof(BOOL);
    }

    DXVK_LOG_FUNC("err", "Failed to query data size");
    return 0;
  }


  void STDMETHODCALLTYPE D3D11Query::GetDesc(D3D11_QUERY_DESC* pDesc) {
    pDesc->Query     = m_desc.Query;
    pDesc->MiscFlags = m_desc.MiscFlags;
  }


  void STDMETHODCALLTYPE D3D11Query::GetDesc1(D3D11_QUERY_DESC1* pDesc) {
    *pDesc = m_desc;
  }

  HRESULT STDMETHODCALLTYPE D3D11Query::GetData(
          void*                             pData,
          UINT                              GetDataFlags) {
    if (m_desc.Query == D3D11_QUERY_EVENT) {
      return S_OK;
    } else {
      if (pData == nullptr)
        return S_OK;

      switch (m_desc.Query) {
        case D3D11_QUERY_OCCLUSION:
          *static_cast<UINT64*>(pData) = 0;
          return S_OK;

        case D3D11_QUERY_OCCLUSION_PREDICATE:
          *static_cast<BOOL*>(pData) = FALSE;
          return S_OK;

        case D3D11_QUERY_TIMESTAMP:
          *static_cast<UINT64*>(pData) = 1;
          return S_OK;

        case D3D11_QUERY_TIMESTAMP_DISJOINT: {
          auto data = static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(pData);
          data->Frequency = 1;
          data->Disjoint  = false;
        } return S_OK;

        case D3D11_QUERY_PIPELINE_STATISTICS: {
          auto data = static_cast<D3D11_QUERY_DATA_PIPELINE_STATISTICS*>(pData);
          *data = D3D11_QUERY_DATA_PIPELINE_STATISTICS{};
        } return S_OK;

        case D3D11_QUERY_SO_STATISTICS:
        case D3D11_QUERY_SO_STATISTICS_STREAM0:
        case D3D11_QUERY_SO_STATISTICS_STREAM1:
        case D3D11_QUERY_SO_STATISTICS_STREAM2:
        case D3D11_QUERY_SO_STATISTICS_STREAM3: {
          auto data = static_cast<D3D11_QUERY_DATA_SO_STATISTICS*>(pData);
          *data = D3D11_QUERY_DATA_SO_STATISTICS{};
        } return S_OK;

        case D3D11_QUERY_SO_OVERFLOW_PREDICATE:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3: {
          auto data = static_cast<BOOL*>(pData);
          *data = FALSE;
        } return S_OK;

        default:
          DXVK_LOG_FUNC("err", "Unhandled query type: %d", m_desc.Query);
          return E_INVALIDARG;
      }
    }
  }

}

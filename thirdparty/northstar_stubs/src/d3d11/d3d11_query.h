#pragma once

#include "d3d11_device_child.h"
#include "d3d11_include.h"

namespace dxvk {

  class D3D11Device;

  class D3D11Query : public D3D11DeviceChild<ID3D11Query1> {
  public:

    D3D11Query(
            D3D11Device*        device,
      const D3D11_QUERY_DESC1&  desc);

    ~D3D11Query();

    HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID  riid,
            void**  ppvObject) final;

    UINT STDMETHODCALLTYPE GetDataSize();

    void STDMETHODCALLTYPE GetDesc(D3D11_QUERY_DESC* pDesc) final;

    void STDMETHODCALLTYPE GetDesc1(D3D11_QUERY_DESC1* pDesc) final;

    HRESULT STDMETHODCALLTYPE GetData(
            void*                             pData,
            UINT                              GetDataFlags);

    static ID3D11Predicate* AsPredicate(ID3D11Query* pQuery) {
      // ID3D11Predicate and ID3D11Query have the same vtable. This
      // saves us some headache in all query-related functions.
      return static_cast<ID3D11Predicate*>(pQuery);
    }

    static D3D11Query* FromPredicate(ID3D11Predicate* pPredicate) {
      return static_cast<D3D11Query*>(static_cast<ID3D11Query*>(pPredicate));
    }

  private:

    D3D11_QUERY_DESC1  m_desc;

  };

}

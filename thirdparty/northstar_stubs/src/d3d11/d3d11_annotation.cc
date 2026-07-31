#include "d3d11_annotation.h"
#include "d3d11_context.h"
#include "d3d11_device.h"

namespace dxvk {

  D3D11UserDefinedAnnotation::D3D11UserDefinedAnnotation(D3D11DeviceContext* ctx)
  : m_container(ctx) {
  }


  D3D11UserDefinedAnnotation::~D3D11UserDefinedAnnotation() {
  }


  ULONG STDMETHODCALLTYPE D3D11UserDefinedAnnotation::AddRef() {
    return m_container->AddRef();
  }


  ULONG STDMETHODCALLTYPE D3D11UserDefinedAnnotation::Release() {
    return m_container->Release();
  }


  HRESULT STDMETHODCALLTYPE D3D11UserDefinedAnnotation::QueryInterface(
          REFIID                  riid,
          void**                  ppvObject) {
    return m_container->QueryInterface(riid, ppvObject);
  }


  INT STDMETHODCALLTYPE D3D11UserDefinedAnnotation::BeginEvent(
          LPCWSTR                 Name) {
    if (!m_container->IsAnnotationEnabled())
      return -1;
    return m_eventDepth++;
  }


  INT STDMETHODCALLTYPE D3D11UserDefinedAnnotation::EndEvent() {
    if (!m_container->IsAnnotationEnabled())
      return -1;
    return m_eventDepth--;
  }


  void STDMETHODCALLTYPE D3D11UserDefinedAnnotation::SetMarker(
          LPCWSTR                 Name) {
    if (!m_container->IsAnnotationEnabled())
      return;
  }


  BOOL STDMETHODCALLTYPE D3D11UserDefinedAnnotation::GetStatus() {
    return m_container->IsAnnotationEnabled();
  }

}

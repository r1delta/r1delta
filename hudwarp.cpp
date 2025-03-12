#include "hudwarp.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
// Ported from TFORevive by Barnaby

bool CompileShader(const char* szShader, const char* szEntrypoint, const char* szTarget, ID3D10Blob** pBlob)
{
	ID3D10Blob* pErrorBlob = nullptr;

	auto hr = D3DCompile(szShader, strlen(szShader), 0, nullptr, nullptr, szEntrypoint, szTarget, D3DCOMPILE_ENABLE_STRICTNESS, 0, pBlob, &pErrorBlob);
	if (FAILED(hr))
	{
		if (pErrorBlob)
		{
			char szError[256]{ 0 };
			memcpy(szError, pErrorBlob->GetBufferPointer(), pErrorBlob->GetBufferSize());
			MessageBoxA(nullptr, szError, "Error", MB_OK);
		}
		return false;
	}
	return true;
}

ID3D11VertexShader* pVertexShader;
ID3D11PixelShader* pPixelShader;

#define MAINVP 0
D3D11_VIEWPORT viewport{ 0 };

HudwarpProcess::HudwarpProcess(ID3D11Device* pDevice, ID3D11DeviceContext** ppID3D11DeviceContext) : m_pDevice(pDevice), m_pContext(*ppID3D11DeviceContext)
{
	// Initialize shaders
	ID3D10Blob* pBlob = nullptr;

	// Create vertex shader
	if (!pVertexShader)
	{
		if (!CompileShader(hudwarpShader, "VS", "vs_5_0", &pBlob))
			return;

		HRESULT hr = pDevice->CreateVertexShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &pVertexShader);
		if (FAILED(hr))
			return;
	}

	// Define/create the input layout for the vertex shader
	D3D11_INPUT_ELEMENT_DESC layout[2] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	UINT numElements = 2;

	HRESULT hr = pDevice->CreateInputLayout(layout, numElements, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), &m_pVertexLayout);
	if (FAILED(hr))
		return;

	// Create pixel shader
	if (!pPixelShader)
	{
		if (!CompileShader(hudwarpShader, "PS", "ps_5_0", &pBlob))
			return;

		hr = pDevice->CreatePixelShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &pPixelShader);
		if (FAILED(hr))
			return;
	}

	static DWORD64 vguimatsurfacedllBaseAddress = (DWORD64)GetModuleHandle(L"materialsystem_dx11.dll");
	m_width = *reinterpret_cast<unsigned int*>(vguimatsurfacedllBaseAddress + 0x290DD8);
	m_height = *reinterpret_cast<unsigned int*>(vguimatsurfacedllBaseAddress + 0x290DD8 + 4);

	// We add a border to the texture so that the HUD can't get cut off by the texture boundaries
	unsigned int widthWithBorder = m_width * (1.0f + HUD_TEX_BORDER_SIZE * 2.0f);
	unsigned int heightWithBorder = m_height * (1.0f + HUD_TEX_BORDER_SIZE * 2.0f);

	// Setup the texture descriptor
	D3D11_TEXTURE2D_DESC textureDesc;
	ZeroMemory(&textureDesc, sizeof(textureDesc));
	textureDesc.Width = widthWithBorder;
	textureDesc.Height = heightWithBorder;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;

	// Create the render texture
	m_pDevice->CreateTexture2D(&textureDesc, NULL, &m_pRenderTexture);

	// Setup the render target view descriptor
	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
	renderTargetViewDesc.Format = textureDesc.Format;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	renderTargetViewDesc.Texture2D.MipSlice = 0;

	// Create the render target view.
	m_pDevice->CreateRenderTargetView(m_pRenderTexture, &renderTargetViewDesc, &m_pRenderTargetView);

	// Create our viewport
	viewport.Width = (float)widthWithBorder;
	viewport.Height = (float)heightWithBorder;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// Setup the shader resource view descriptor
	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
	shaderResourceViewDesc.Format = textureDesc.Format;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	// Create the shader resource view.
	m_pDevice->CreateShaderResourceView(m_pRenderTexture, &shaderResourceViewDesc, &m_pShaderResourceView);

	// Create the constant buffer
	D3D11_BUFFER_DESC bd{ 0 };
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.ByteWidth = sizeof(ConstantBuffer);
	bd.Usage = D3D11_USAGE_DEFAULT;

	// Setup orthographic projection
	mOrtho = XMMatrixOrthographicLH(1.0f, 1.0f, 0.0f, 1.0f);
	ConstantBuffer cb;
	cb.mProjection = mOrtho;
	cb.xWarp = 0.0f;
	cb.xScale = 1.0f;
	cb.yWarp = 0.0f;
	cb.yScale = 1.0f;
	cb.viewDist = 1.0f;

	D3D11_SUBRESOURCE_DATA sr{ 0 };
	sr.pSysMem = &cb;
	pDevice->CreateBuffer(&bd, &sr, &m_pConstantBuffer);

	// Create a triangle to render
	// Create a vertex buffer, start by setting up a description.
	ZeroMemory(&bd, sizeof(bd));
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = 4 * sizeof(Vertex);
	bd.StructureByteStride = sizeof(Vertex);

	// left and top edge of window
	float left = 1.0f / -2;
	float right = 1.0f / 2;
	float top = 1.0f / 2;
	float bottom = 1.0f / -2;

	Vertex pVerts[4] = {
		{ XMFLOAT3(left, top, 1.0f), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(right, top, 1.0f), XMFLOAT2(1.0f, 0.0f) },
		{ XMFLOAT3(right, bottom, 1.0f), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(left, bottom, 1.0f), XMFLOAT2(0.0f, 1.0f) },
	};

	// create the buffer.
	ZeroMemory(&sr, sizeof(sr));
	sr.pSysMem = &pVerts;
	pDevice->CreateBuffer(&bd, &sr, &m_pVertexBuffer);

	// Create an index buffer
	ZeroMemory(&bd, sizeof(bd));
	ZeroMemory(&sr, sizeof(sr));

	UINT pIndices[6] = { 0, 1, 2, 0, 2, 3 };
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(UINT) * 6;
	bd.StructureByteStride = sizeof(UINT);

	sr.pSysMem = &pIndices;
	pDevice->CreateBuffer(&bd, &sr, &m_pIndexBuffer);

	// Describe the Sample State
	D3D11_SAMPLER_DESC sampDesc;
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MaxAnisotropy = 4;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

	//Create the Sample State
	pDevice->CreateSamplerState(&sampDesc, &m_pSamplerState);

	// Create Enabled Blend State
	D3D11_RENDER_TARGET_BLEND_DESC rt_blend_desc;
	ZeroMemory(&rt_blend_desc, sizeof(rt_blend_desc));
	rt_blend_desc.BlendEnable = true;
	rt_blend_desc.SrcBlend = D3D11_BLEND_ONE;
	rt_blend_desc.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	rt_blend_desc.BlendOp = D3D11_BLEND_OP_ADD;
	rt_blend_desc.SrcBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA;
	rt_blend_desc.DestBlendAlpha = D3D11_BLEND_ONE;
	rt_blend_desc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	rt_blend_desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	D3D11_BLEND_DESC blend_desc;
	ZeroMemory(&blend_desc, sizeof(blend_desc));
	blend_desc.AlphaToCoverageEnable = false;
	blend_desc.IndependentBlendEnable = false;
	blend_desc.RenderTarget[0] = rt_blend_desc;

	pDevice->CreateBlendState(&blend_desc, &m_pBlendState);

	D3D11_RASTERIZER_DESC cmdesc;

	ZeroMemory(&cmdesc, sizeof(D3D11_RASTERIZER_DESC));
	cmdesc.FillMode = D3D11_FILL_SOLID;
	cmdesc.CullMode = D3D11_CULL_BACK;
	cmdesc.FrontCounterClockwise = false;
	cmdesc.MultisampleEnable = true;
	pDevice->CreateRasterizerState(&cmdesc, &m_pCWcullMode);
}

HudwarpProcess::~HudwarpProcess()
{
	m_pRenderTexture->Release();
	m_pRenderTargetView->Release();
	m_pShaderResourceView->Release();
	m_pSamplerState->Release();
	m_pBlendState->Release();
	m_pVertexBuffer->Release();
	m_pVertexBuffer->Release();
	m_pIndexBuffer->Release();
	m_pConstantBuffer->Release();
	m_pCWcullMode->Release();
}

void HudwarpProcess::Resize(unsigned int w, unsigned int h)
{
	// Release render target
	m_pRenderTexture->Release();
	m_pRenderTargetView->Release();
	m_pShaderResourceView->Release();

	// Create new render target
	static DWORD64 vguimatsurfacedllBaseAddress = (DWORD64)GetModuleHandleA("materialsystem_dx11.dll");
	m_width = *reinterpret_cast<unsigned int*>(vguimatsurfacedllBaseAddress + 0x290DD8);
	m_height = *reinterpret_cast<unsigned int*>(vguimatsurfacedllBaseAddress + 0x290DD8 + 4);

	// We add a border to the texture so that the HUD can't get cut off by the texture boundaries
	unsigned int widthWithBorder = m_width * (1.0f + HUD_TEX_BORDER_SIZE * 2.0f);
	unsigned int heightWithBorder = m_height * (1.0f + HUD_TEX_BORDER_SIZE * 2.0f);

	// Setup the texture descriptor
	D3D11_TEXTURE2D_DESC textureDesc;
	ZeroMemory(&textureDesc, sizeof(textureDesc));
	textureDesc.Width = widthWithBorder;
	textureDesc.Height = heightWithBorder;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;

	// Create the render texture
	m_pDevice->CreateTexture2D(&textureDesc, NULL, &m_pRenderTexture);

	// Setup the render target view descriptor
	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
	renderTargetViewDesc.Format = textureDesc.Format;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	renderTargetViewDesc.Texture2D.MipSlice = 0;

	// Create the render target view.
	m_pDevice->CreateRenderTargetView(m_pRenderTexture, &renderTargetViewDesc, &m_pRenderTargetView);

	// Setup the shader resource view descriptor
	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
	shaderResourceViewDesc.Format = textureDesc.Format;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	// Create the shader resource view.
	m_pDevice->CreateShaderResourceView(m_pRenderTexture, &shaderResourceViewDesc, &m_pShaderResourceView);
}

void HudwarpProcess::UpdateSettings(HudwarpSettings* hudwarpSettings)
{
	m_hudwarpSettings = *hudwarpSettings;
}

void HudwarpProcess::Begin()
{
	// Get the current render target
	m_pContext->OMGetRenderTargets(1, &m_pOriginalRenderTargetView, &m_pOriginalDepthStencilView);

	// Get the current viewports
	m_originalNumViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	m_pContext->RSGetViewports(&m_originalNumViewports, m_pOriginalViewports);

	// Set our viewport
	m_pContext->RSSetViewports(1, &viewport);

	// Set our render target
	m_pContext->OMSetRenderTargets(1, &m_pRenderTargetView, NULL);

	// Clear the render target
	float bgColor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
	m_pContext->ClearRenderTargetView(m_pRenderTargetView, bgColor);
}

void HudwarpProcess::Finish()
{
	// Set the current blend state
	ID3D11BlendState* pOriginalBlendState;
	float oldBlendFactor[4];
	unsigned int oldSampleMask;
	m_pContext->OMGetBlendState(&pOriginalBlendState, oldBlendFactor, &oldSampleMask);

	// Get current shader state
	ID3D11VertexShader* pOriginalVertexShader;
	ID3D11PixelShader* pOriginalPixelShader;
	m_pContext->VSGetShader(&pOriginalVertexShader, 0, 0);
	m_pContext->PSGetShader(&pOriginalPixelShader, 0, 0);

	ID3D11Buffer* pOriginalVertexBuffer;
	UINT originalVertexStride = 0;
	UINT originalVertexOffset = 0;
	ID3D11InputLayout* pOriginalVertexLayout;
	ID3D11Buffer* pOriginalIndexBuffer;
	DXGI_FORMAT originalIndexFormat;
	UINT originalIndexOffset = 0;
	D3D11_PRIMITIVE_TOPOLOGY originalPrimitiveTopology;
	m_pContext->IAGetVertexBuffers(0, 1, &pOriginalVertexBuffer, &originalVertexStride, &originalVertexOffset);
	m_pContext->IAGetInputLayout(&pOriginalVertexLayout);
	m_pContext->IAGetIndexBuffer(&pOriginalIndexBuffer, &originalIndexFormat, &originalIndexOffset);
	m_pContext->IAGetPrimitiveTopology(&originalPrimitiveTopology);

	// Set the blend state	
	m_pContext->OMSetBlendState(m_pBlendState, NULL, 0xffffffff);

	// Set the render target to swapchain buffer
	m_pContext->OMSetRenderTargets(1, &m_pOriginalRenderTargetView, NULL);

	// Set the shaders
	m_pContext->VSSetShader(pVertexShader, 0, 0);
	m_pContext->PSSetShader(pPixelShader, 0, 0);

	// Setup vertices and indices
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	m_pContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &stride, &offset);
	m_pContext->IASetInputLayout(m_pVertexLayout);
	m_pContext->IASetIndexBuffer(m_pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	m_pContext->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Update the constant buffer
	ConstantBuffer cb;
	cb.mProjection = mOrtho;
	cb.aspectRatio = (float)m_width / (float)m_height;
	cb.xWarp = m_hudwarpSettings.xWarp;
	cb.xScale = m_hudwarpSettings.xScale;
	cb.yWarp = m_hudwarpSettings.yWarp;
	cb.yScale = m_hudwarpSettings.yScale;
	cb.viewDist = m_hudwarpSettings.viewDist;

	m_pContext->UpdateSubresource(m_pConstantBuffer, 0, 0, &cb, 0, 0);

	// Set shader resources
	m_pContext->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);
	m_pContext->PSSetShaderResources(0, 1, &m_pShaderResourceView);
	m_pContext->PSSetSamplers(0, 1, &m_pSamplerState);
	m_pContext->PSSetConstantBuffers(0, 1, &m_pConstantBuffer);

	m_pContext->RSSetState(m_pCWcullMode);

	// Restore the original viewports
	m_pContext->RSSetViewports(m_originalNumViewports, m_pOriginalViewports);

	// Draw the texture to the screen
	m_pContext->DrawIndexed(6, 0, 0);

	// **************************************************
	// *** RESTORE STATE OTHERWISE WE GET VISUAL BUGS ***
	// **************************************************
	// Set the shaders
	m_pContext->VSSetShader(pOriginalVertexShader, 0, 0);
	m_pContext->PSSetShader(pOriginalPixelShader, 0, 0);
	m_pContext->IASetVertexBuffers(0, 1, &pOriginalVertexBuffer, &originalVertexStride, &originalVertexOffset);
	m_pContext->IASetInputLayout(pOriginalVertexLayout);
	m_pContext->IASetIndexBuffer(pOriginalIndexBuffer, originalIndexFormat, originalIndexOffset);
	m_pContext->IASetPrimitiveTopology(originalPrimitiveTopology);

	// Set the render target back
	m_pContext->OMSetRenderTargets(1, &m_pOriginalRenderTargetView, m_pOriginalDepthStencilView);

	// Restore the blend state
	m_pContext->OMSetBlendState(pOriginalBlendState, oldBlendFactor, oldSampleMask);
}

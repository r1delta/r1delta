#pragma once

// Ported from TFORevive by Barnaby

#include <d3d11.h>
#include <DirectXMath.h>
#include "cvar.h"

#pragma comment(lib, "d3d11.lib")

using namespace DirectX;

struct HudwarpSettings {
	float xWarp;
	float xScale;
	float yWarp;
	float yScale;
	float viewDist;
};

class HudwarpProcess
{
public:
	HudwarpProcess(ID3D11Device* pDevice, ID3D11DeviceContext** ppID3D11DeviceContext);
	~HudwarpProcess();

	void Begin();
	void Finish();
	void Resize(unsigned int w, unsigned int h);
	void UpdateSettings(HudwarpSettings* hudwarpSettings);

private:
	ID3D11Device* m_pDevice;
	ID3D11DeviceContext* m_pContext;
	ID3D11Texture2D* m_pRenderTexture = NULL;
	ID3D11RenderTargetView* m_pRenderTargetView = NULL;
	ID3D11ShaderResourceView* m_pShaderResourceView = NULL;
	ID3D11SamplerState* m_pSamplerState = NULL;
	ID3D11BlendState* m_pBlendState = NULL;

	ID3D11Buffer* m_pVertexBuffer = nullptr;
	ID3D11InputLayout* m_pVertexLayout = nullptr;
	ID3D11Buffer* m_pIndexBuffer = nullptr;
	ID3D11Buffer* m_pConstantBuffer = nullptr;

	ID3D11RasterizerState* m_pCWcullMode = NULL;

	ID3D11RenderTargetView* m_pOriginalRenderTargetView = NULL;
	ID3D11DepthStencilView* m_pOriginalDepthStencilView = NULL;

	UINT m_originalNumViewports = 0;
	D3D11_VIEWPORT m_pOriginalViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{ 0 };

	XMMATRIX mOrtho{};
	HudwarpSettings m_hudwarpSettings{};

	unsigned int m_width = 0;
	unsigned int m_height = 0;
};

struct ConstantBuffer
{
	XMMATRIX mProjection;
	float aspectRatio;
	// Hudwarp settings
	float xWarp;
	float xScale;
	float yWarp;
	float yScale;
	float viewDist;
};

struct Vertex
{
	XMFLOAT3 pos;
	XMFLOAT2 texCoord;
};

// Must match hudScale in shader
#define HUD_TEX_BORDER_SIZE 0.025f

constexpr const char* hudwarpShader = R"(
cbuffer ConstantBuffer : register(b0)
{
	matrix projection;
	float aspectRatio;
	// Hudwarp settings
	float xWarp;
	float xScale;
	float yWarp;
	float yScale;
	float viewDist; 
}
// PSI (PixelShaderInput)
struct PSI
{
	float4 pos : SV_POSITION;
	float2 texCoord : TEXCOORD;
};

// VertexShader
PSI VS( float4 pos : POSITION, float2 texCoord : TEXCOORD )
{
	PSI psi;
	psi.texCoord = texCoord;
	pos = mul( pos, projection );
	psi.pos = pos;
	return psi;
}
// PixelShader
Texture2D<float4> Texture : register(t0);
sampler Sampler : register(s0);

// Border correction from sub_1800084F0_Hook
float2 UndoHudTexBorder(float2 texCoord)
{
	// IMPORTANT: must match value of HUD_TEX_BORDER_SIZE
	float hudTexBorderSize = 0.025f;

	float hudScale = 1.0f + 2.0f * hudTexBorderSize;
	float hudOffset = 0.5f - (0.5f / hudScale);

	return texCoord / hudScale + hudOffset;
}

float2 NormalizeUV(float2 uv)
{
    if (aspectRatio > 1.0f)
    {
        uv.x *= aspectRatio;
        uv.x /= 16.0f / 9.0f;
    }
	else if (aspectRatio < 1.0f)
    {
        uv.y /= aspectRatio;
        uv.y *= 16.0f / 9.0f;
    }
    
    return uv;
}


float2 Distort(float2 uv) 
{
	// skip this processing if warp is disabled
	if (xWarp == 0.0f && yWarp == 0.0f)
		return uv;

	// i hate this, it's terrible
	// BUT... it gets similar results to respawn's fucked algorithm so we stay winning
	// :3

	float2 uvNorm = NormalizeUV(uv);

    float xWarpFactor = 0.159155f;
    float newX = uv.x * (viewDist + (uvNorm.y * atan(xWarp * uvNorm.y) * xWarpFactor + xWarpFactor * xScale * 1.5f * sin(xWarp)));
    
    float yWarpFactor = 0.5f;
    float newY = uv.y * (viewDist - (uvNorm.x * atan(yWarp * uvNorm.x) * yWarpFactor - yWarpFactor / 2.5f / yScale * sin(yWarp)));

    return float2(newX, newY);
}

float4 PS(PSI psi) : SV_TARGET
{
	// Skip rendering if scale is 0 in either axis
	if (xScale == 0.0f || yScale == 0.0f)
	{
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}

	float2 uv = UndoHudTexBorder(psi.texCoord);
	uv -= 0.5f;
	uv *= 2.0f;
	uv /= float2(xScale, yScale);

	uv = Distort(uv);

	// Skip rendering if our calculate UVs are outside of the hud texture
	if (uv.x < -1.0f || uv.x > 1.0f || uv.y < -1.0f || uv.y > 1.0f)
	{
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}

	//return float4(uv, 0.0f, 1.0f);

    float4 color = Texture.Sample(Sampler, uv / 2.0f + float2(0.5f, 0.5f));
    return color;
}
)";

//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
// Author: Casey Meekhof cmeekhof@microsoft.com

#include "pch.h"

#include "DrawCall.h"
#include "Common/FileUtilities.h"

#include <iostream>
#include <fstream>
#include <sstream>

#include <wincodec.h>

#include <cassert>

using namespace std;
using namespace DirectX;

//std::shared_ptr<DX::DeviceResources>                    m_deviceResources;

Microsoft::WRL::ComPtr<ID3D11Device> g_d3dDevice;
Microsoft::WRL::ComPtr<ID3D11DeviceContext> g_d3dContext;

Microsoft::WRL::ComPtr<ID2D1Factory1> g_d2dFactory;
Microsoft::WRL::ComPtr<ID2D1Device> g_d2dDevice;
Microsoft::WRL::ComPtr<ID2D1DeviceContext> g_d2dContext;
Microsoft::WRL::ComPtr<IDWriteFactory> g_dwriteFactory;

Microsoft::WRL::ComPtr<IDXGISwapChain1> g_d3dSwapChain;

//TODO: Move these into the class
std::map<float, Microsoft::WRL::ComPtr<IDWriteTextFormat>> g_textFormats;	// Text format cache, organized by font size
Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g_whiteBrush;
Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g_grayBrush;
Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g_blackBrush;

winrt::Windows::Graphics::Holographic::HolographicSpace g_holographic_space{ nullptr };


//
//DrawCall
//
const float DrawCall::DefaultFontSize = 64.0f;

XMMATRIX DrawCall::m_mtxLightViewProj;
XMMATRIX DrawCall::m_mtxCameraView;
DrawCall::Projection DrawCall::m_cameraProj;

XMVECTOR DrawCall::vAmbient;
DrawCall::Light DrawCall::vLights[kMaxLights];
unsigned DrawCall::uLightCount = 0;
unsigned DrawCall::uActiveLightIdx = 0;

bool DrawCall::m_singlePassStereoSupported = false;
bool DrawCall::m_singlePassStereoEnabled = false;

stack<DrawCall::View> DrawCall::m_viewStack;
DrawCall::View DrawCall::m_activeView;

stack<DrawCall::Projection> DrawCall::m_projectionStack;
DrawCall::Projection DrawCall::m_activeProjection;

shared_ptr<Texture2D> DrawCall::m_backBuffer;
std::map<intptr_t, std::shared_ptr<Texture2D>> DrawCall::m_cachedBackBuffers;
stack<vector<shared_ptr<Texture2D>>> DrawCall::m_renderTargetStack;

stack<unsigned> DrawCall::m_renderPassIndexStack;
unsigned DrawCall::m_activeRenderPassIndex = 0;

stack<DrawCall::BlendState> DrawCall::m_sAlphaBlendStates;
stack<bool> DrawCall::m_sDepthTestStates;
stack<bool> DrawCall::m_sBackfaceCullingStates;

stack<bool> DrawCall::m_sRightEyePassStates;
stack<bool> DrawCall::m_sFullscreenPassStates;

vector<DrawCall::RenderPassDesc> DrawCall::m_vGlobalRenderPasses;

map<string, shared_ptr<Shader>> DrawCall::m_shaderStore;

Microsoft::WRL::ComPtr<ID3D11BlendState> DrawCall::m_d3dAlphaBlendEnabledState;
Microsoft::WRL::ComPtr<ID3D11BlendState> DrawCall::m_d3dAdditiveBlendEnabledState;
Microsoft::WRL::ComPtr<ID3D11BlendState> DrawCall::m_d3dColorWriteDisabledState;
Microsoft::WRL::ComPtr<ID3D11DepthStencilState> DrawCall::m_d3dDepthTestDisabledState;
Microsoft::WRL::ComPtr<ID3D11RasterizerState> DrawCall::m_d3dBackfaceCullingDisabledState;

vector<D3D11_INPUT_ELEMENT_DESC> g_instancedElements =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},

	{"WORLDMATRIX_ROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
	{"WORLDMATRIX_ROW", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
	{"WORLDMATRIX_ROW", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
	{"WORLDMATRIX_ROW", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
	{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
};

vector<D3D11_INPUT_ELEMENT_DESC> g_instancedParticleElements =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },

	{ "TRANSLATIONSCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
};


#ifdef USE_WINRT_D3D
bool DrawCall::InitializeSwapChain(unsigned width, unsigned height, winrt::Windows::UI::Core::CoreWindow const& window)
#else
bool DrawCall::InitializeSwapChain(unsigned width, unsigned height, HWND hWnd, bool fullscreen)
#endif
{
	if (!g_d3dDevice)
		return false;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDesc.Stereo = false;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.Flags = 0;

	Microsoft::WRL::ComPtr<IDXGIDevice1> dxgiDevice;
	g_d3dDevice.As(&dxgiDevice);

	Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
	dxgiDevice->GetAdapter(&dxgiAdapter);

	Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
	dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), &dxgiFactory);

#ifdef USE_WINRT_D3D
	HRESULT success = dxgiFactory->CreateSwapChainForCoreWindow(g_d3dDevice.Get(), (IUnknown*) winrt::get_abi(window), &swapChainDesc, nullptr, &g_d3dSwapChain);
#else	
	if (fullscreen)
	{
		Microsoft::WRL::ComPtr<IDXGIOutput> dxgiOutput;
		dxgiAdapter->EnumOutputs(0, &dxgiOutput);

		DXGI_MODE_DESC desiredMode;
		desiredMode.Format = swapChainDesc.Format;
		desiredMode.Height = swapChainDesc.Height;
		desiredMode.Width = swapChainDesc.Width;
		desiredMode.RefreshRate.Numerator = 60000;
		desiredMode.RefreshRate.Denominator = 1001;
		desiredMode.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		desiredMode.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;

		DXGI_MODE_DESC closestMode;
		dxgiOutput->FindClosestMatchingMode(&desiredMode, &closestMode, nullptr);

		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc;
		swapChainDesc.Format = closestMode.Format;
		swapChainDesc.Width = closestMode.Width;
		swapChainDesc.Height = closestMode.Height;
		fullscreenDesc.RefreshRate = closestMode.RefreshRate;
		fullscreenDesc.ScanlineOrdering = closestMode.ScanlineOrdering;
		fullscreenDesc.Scaling = closestMode.Scaling;
		fullscreenDesc.Windowed = FALSE;
		dxgiFactory->CreateSwapChainForHwnd(g_d3dDevice.Get(), (HWND)hWnd, &swapChainDesc, &fullscreenDesc, nullptr, &g_d3dSwapChain);
	}
	else
	{
		dxgiFactory->CreateSwapChainForHwnd(g_d3dDevice.Get(), (HWND)hWnd, &swapChainDesc, nullptr, nullptr, &g_d3dSwapChain);
	}
#endif

	SetBackBuffer(g_d3dSwapChain.Get());

	return true;
}

bool DrawCall::ResizeSwapChain(unsigned newWidth, unsigned newHeight)
{
	if (!g_d3dSwapChain || !m_backBuffer || !g_d3dContext)
		return false;

	m_backBuffer->Reset();
	g_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
	g_d2dContext->SetTarget(nullptr);
	
	g_d3dSwapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);
	
	Microsoft::WRL::ComPtr<ID3D11Texture2D> d3dBackBuffer = nullptr;
	g_d3dSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&d3dBackBuffer);
	m_backBuffer->SetD3DTexture(d3dBackBuffer.Get());

	return true;
}

void DrawCall::Uninitialize()
{
	m_shaderStore.clear();

	m_backBuffer.reset();
	m_cachedBackBuffers.clear();

	m_d3dAlphaBlendEnabledState.Reset();
	m_d3dDepthTestDisabledState.Reset();
	m_d3dAdditiveBlendEnabledState.Reset();
	m_d3dColorWriteDisabledState.Reset();
	m_d3dBackfaceCullingDisabledState.Reset();

	g_d2dFactory.Reset();
	g_d2dDevice.Reset();
	g_d2dContext.Reset();
	g_dwriteFactory.Reset();

	g_textFormats.clear();
	g_whiteBrush.Reset();

	g_d3dContext.Reset();
	g_d3dDevice.Reset();
	g_d3dSwapChain.Reset();
}

ID3D11Device* DrawCall::GetD3DDevice()
{
	return g_d3dDevice.Get();
}

ID3D11DeviceContext* DrawCall::GetD3DDeviceContext()
{
	return g_d3dContext.Get();
}

IDXGISwapChain1* DrawCall::GetD3DSwapChain()
{
	return g_d3dSwapChain.Get();
}

bool DrawCall::IsSinglePassSteroSupported()
{
	return m_singlePassStereoSupported;
}

bool DrawCall::IsSinglePassSteroEnabled()
{
	return m_singlePassStereoEnabled;
}

void DrawCall::EnableSinglePassStereo(bool enabled)
{
	if (IsSinglePassSteroSupported())
		m_singlePassStereoEnabled = enabled;
}

void DrawCall::PushView(const XMMATRIX& mtxLeft, const XMMATRIX& mtxRight)
{
	m_activeView.mtx = mtxLeft;
	m_activeView.mtxRight = mtxRight;
	m_viewStack.push(m_activeView);
}

void DrawCall::PushView(const XMVECTOR& vEye, const XMVECTOR vAt, const XMVECTOR &vUp)
{
	m_activeView.mtx = m_activeView.mtxRight = XMMatrixLookAtRH(vEye, vAt, vUp);
	m_viewStack.push(m_activeView);
}

void DrawCall::PopView()
{
	// Don't pop the last view matrix
	if(m_viewStack.size() <=1)
		return;

	m_viewStack.pop();
	m_activeView = m_viewStack.top();
}

XMMATRIX DrawCall::GetView()
{
	return m_activeView.mtx;
}

void DrawCall::PushProj(const XMMATRIX& mtxLeft, const XMMATRIX& mtxRight)
{
	memset(&m_activeProjection, 0, sizeof(m_activeProjection));

	m_activeProjection.mtx = mtxLeft;
	m_activeProjection.mtxRight = mtxRight;

	m_projectionStack.push(m_activeProjection);
}

void DrawCall::PushProj(float fFOV, float fAspect, float fNear, float fFar)
{
	m_activeProjection.mtx = XMMatrixPerspectiveFovRH(fFOV, fAspect, fNear, fFar);
	m_activeProjection.fNearPlaneHeight = tan(fFOV * .5f) * 2.f * fNear;
	m_activeProjection.fNearPlaneWidth = m_activeProjection.fNearPlaneHeight * fAspect;
	m_activeProjection.fNear = fNear;
	m_activeProjection.fFar = fFar;
	m_activeProjection.fRange = fFar / (fFar-fNear);
	
	m_projectionStack.push(m_activeProjection);
}

void DrawCall::PushProjOrtho(float fWidth, float fHeight, float fNear, float fFar)
{
	m_activeProjection.mtx = XMMatrixOrthographicRH(fWidth, fHeight, fNear, fFar);
	m_activeProjection.fNearPlaneHeight = fHeight;
	m_activeProjection.fNearPlaneWidth = fWidth;
	m_activeProjection.fNear = fNear;
	m_activeProjection.fFar = fFar;
	m_activeProjection.fRange = fFar / (fFar-fNear);

	m_projectionStack.push(m_activeProjection);
}

void DrawCall::PopProj()
{
	// Don't pop the last proj matrix
	if (m_viewStack.size() <= 1)
		return;

	m_projectionStack.pop();
	m_activeProjection = m_projectionStack.top();
}

XMMATRIX DrawCall::GetProj()
{
	return m_activeProjection.mtx;
}

void DrawCall::StoreCurrentViewProjAsLightViewProj()
{
	m_mtxLightViewProj = XMMatrixMultiply(m_activeView.mtx, m_activeProjection.mtx);
}

void DrawCall::StoreCurrentViewAsCameraView()
{
	m_mtxCameraView = m_activeView.mtx;
	m_cameraProj = m_activeProjection;
}

shared_ptr<Texture2D> DrawCall::GetBackBuffer()
{
	return m_backBuffer;
}

void DrawCall::SetBackBuffer(shared_ptr<Texture2D> backBuffer)
{
	if (m_renderPassIndexStack.empty())
		m_activeRenderPassIndex = 0;

	m_backBuffer = backBuffer;
	if (m_renderTargetStack.empty())
		SetCurrentRenderTargetsOnD3DDevice();
}

void DrawCall::SetBackBuffer(ID3D11Texture2D* pD3DBackBuffer, const D3D11_VIEWPORT& viewport)
{
	if(!pD3DBackBuffer)
		return;

	std::shared_ptr<Texture2D> backBuffer;

	auto it = m_cachedBackBuffers.find((intptr_t)pD3DBackBuffer);
	if (it == m_cachedBackBuffers.end())
	{
		backBuffer = make_shared<Texture2D>(pD3DBackBuffer);
		m_cachedBackBuffers.insert({ (intptr_t)pD3DBackBuffer, backBuffer });
	}
	else
	{
		backBuffer = it->second;
	}

	backBuffer->SetViewport(viewport);
	SetBackBuffer(backBuffer);
}

void DrawCall::SetBackBuffer(ID3D11Texture2D* pD3DBackBuffer)
{
	if (!pD3DBackBuffer)
		return;

	D3D11_TEXTURE2D_DESC desc;
	pD3DBackBuffer->GetDesc(&desc);
	CD3D11_VIEWPORT viewport(0.0f, 0.0f, (float)desc.Width, (float)desc.Height);

	SetBackBuffer(pD3DBackBuffer, viewport);
}

void DrawCall::SetBackBuffer(IDXGISwapChain1* pSwapChain)
{
	if (!pSwapChain)
		return;

	// Create a texture for the back buffer of the swap chain
	Microsoft::WRL::ComPtr<ID3D11Texture2D> d3dBackBuffer = nullptr;
	pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&d3dBackBuffer);
	assert(d3dBackBuffer);

	SetBackBuffer(d3dBackBuffer.Get());

	return;
}

shared_ptr<Texture2D> DrawCall::GetCurrentRenderTarget()
{
	if (m_renderTargetStack.empty())
		return m_backBuffer;
	else
		return m_renderTargetStack.top().front();
}

void DrawCall::PushRenderPass(unsigned renderPassIndex, shared_ptr<Texture2D> renderTarget)
{
	if (!renderTarget)
		renderTarget = GetCurrentRenderTarget();

	vector<shared_ptr<Texture2D>> renderTargets	{ renderTarget };
	return PushRenderPass(renderPassIndex, renderTargets);
}

void DrawCall::PushRenderPass(unsigned renderPassIndex, vector<shared_ptr<Texture2D>> renderTargets)
{
	m_renderPassIndexStack.push(renderPassIndex);
	m_activeRenderPassIndex = renderPassIndex;

	if (renderTargets.empty())
		renderTargets.push_back(GetCurrentRenderTarget());

	m_renderTargetStack.push(renderTargets);
	SetCurrentRenderTargetsOnD3DDevice();
}

void DrawCall::PopRenderPass()
{
	m_renderPassIndexStack.pop();
	if (m_renderPassIndexStack.empty())
		m_activeRenderPassIndex = 0;
	else
		m_activeRenderPassIndex = m_renderPassIndexStack.top();

	m_renderTargetStack.pop();
	SetCurrentRenderTargetsOnD3DDevice();

	// Clear the shader resource views
	ID3D11ShaderResourceView* vNullShaderResourceViews[16];
	memset(vNullShaderResourceViews, 0, sizeof(vNullShaderResourceViews));
	g_d3dContext->VSSetShaderResources(0, 16, vNullShaderResourceViews);
	g_d3dContext->PSSetShaderResources(0, 16, vNullShaderResourceViews);
}

void DrawCall::SetCurrentRenderTargetsOnD3DDevice()
{
	vector<shared_ptr<Texture2D>> renderTargets;

	if (m_renderTargetStack.empty())
		renderTargets.push_back(m_backBuffer);
	else
		renderTargets = m_renderTargetStack.top();

	vector<ID3D11RenderTargetView*> renderTargetViews;
	for (auto &texture : renderTargets)
	{
		ID3D11RenderTargetView* pRenderTargetView = texture->GetRenderTargetView();
		if (texture->IsStereo() && IsRightEyePassActive())
			pRenderTargetView = texture->GetRenderTargetViewRight();

		renderTargetViews.push_back(pRenderTargetView);
	}

	ID3D11DepthStencilView* pDepthStencilView = renderTargets[0]->GetDepthStencilView();
	if (renderTargets[0]->IsStereo() && IsRightEyePassActive())
		pDepthStencilView = renderTargets[0]->GetDepthStencilViewRight();

	g_d3dContext->OMSetRenderTargets((UINT) renderTargetViews.size(), renderTargetViews.data(), pDepthStencilView);
	g_d3dContext->RSSetViewports(1, &renderTargets[0]->GetViewport());
	g_d2dContext->SetTarget(renderTargets[0]->GetD2DTargetBitmap());
}

bool DrawCall::IsRightEyePassActive()
{
	return !m_sRightEyePassStates.empty() && m_sRightEyePassStates.top();
}

void DrawCall::PushRightEyePass(unsigned renderPassIndex, shared_ptr<Texture2D> renderTarget)
{
	m_sRightEyePassStates.push(true);
	PushRenderPass(renderPassIndex, renderTarget);
}

void DrawCall::PopRightEyePass()
{
	m_sRightEyePassStates.pop();
	PopRenderPass();
}


void DrawCall::PushShadowPass(shared_ptr<Texture2D> depthMap, unsigned uRenderPassIdx, unsigned uLightIdx)
{
	assert(depthMap);

	DrawCall::PushRenderPass(uRenderPassIdx, depthMap);

	DrawCall::PushView(DrawCall::vLights[uLightIdx].vLightPosW, DrawCall::vLights[uLightIdx].vLightAtW, XMVectorSet(0.f, 1.f, 0.f, 0.f));
	DrawCall::PushProj(XM_PIDIV4, 1.f, 1.f, 250.f);
	DrawCall::StoreCurrentViewProjAsLightViewProj();
}

void DrawCall::PopShadowPass()
{
	DrawCall::PopView();
	DrawCall::PopProj();

	DrawCall::PopRenderPass();
}

void DrawCall::PushUIPass(shared_ptr<Texture2D> renderTarget)
{
	DrawCall::PushRenderPass(0, renderTarget);

	DrawCall::PushView(XMVectorSet(0.f, 0.f, 1.f, 1.f),
						XMVectorSet(0.f, 0.f, 0.f, 1.f), 
						XMVectorSet(0.f, 1.f, 0.f, 0.f));

	DrawCall::PushProjOrtho(GetCurrentRenderTarget()->GetWidth()/(float)GetCurrentRenderTarget()->GetHeight(), 1.f, 1.f, 1000.f);

	DrawCall::PushDepthTestState(false);
}

void DrawCall::PopUIPass()
{
	DrawCall::PopView();
	DrawCall::PopProj();

	DrawCall::PopDepthTestState();

	DrawCall::PopRenderPass();
}

void DrawCall::PushFullscreenPass(shared_ptr<Texture2D> renderTarget)
{
	PushUIPass(renderTarget);
	m_sFullscreenPassStates.push(true);
}

void DrawCall::PopFullscreenPass()
{
	PopUIPass();
	m_sFullscreenPassStates.pop();
}

void DrawCall::PushAlphaBlendState(BlendState blendState)
{
	m_sAlphaBlendStates.push(blendState);

	if (m_sAlphaBlendStates.top() == BLEND_ALPHA)
		g_d3dContext->OMSetBlendState(m_d3dAlphaBlendEnabledState.Get(), nullptr, 0xffffffff);
	else if (m_sAlphaBlendStates.top() == BLEND_ADDITIVE)
		g_d3dContext->OMSetBlendState(m_d3dAdditiveBlendEnabledState.Get(), nullptr, 0xffffffff);
	else if(m_sAlphaBlendStates.top() == BLEND_COLOR_DISABLED)
		g_d3dContext->OMSetBlendState(m_d3dColorWriteDisabledState.Get(), nullptr, 0xffffffff);
	else
		g_d3dContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
}

void DrawCall::PopAlphaBlendState()
{
	if (m_sAlphaBlendStates.size() <= 1)
		return;

	m_sAlphaBlendStates.pop();

	if (m_sAlphaBlendStates.top() == BLEND_ALPHA)
		g_d3dContext->OMSetBlendState(m_d3dAlphaBlendEnabledState.Get(), nullptr, 0xffffffff);
	else if (m_sAlphaBlendStates.top() == BLEND_ADDITIVE)
		g_d3dContext->OMSetBlendState(m_d3dAdditiveBlendEnabledState.Get(), nullptr, 0xffffffff);
	else if (m_sAlphaBlendStates.top() == BLEND_COLOR_DISABLED)
		g_d3dContext->OMSetBlendState(m_d3dColorWriteDisabledState.Get(), nullptr, 0xffffffff);
	else
		g_d3dContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
}

void DrawCall::PushDepthTestState(bool bEnabled)
{
	m_sDepthTestStates.push(bEnabled);

	if (!m_sDepthTestStates.top())
		g_d3dContext->OMSetDepthStencilState(m_d3dDepthTestDisabledState.Get(), 0);
	else
		g_d3dContext->OMSetDepthStencilState(nullptr, 0);
}

void DrawCall::PopDepthTestState()
{
	if (m_sDepthTestStates.size() <= 1)
		return;

	m_sDepthTestStates.pop();

	if (!m_sDepthTestStates.top())
		g_d3dContext->OMSetDepthStencilState(m_d3dDepthTestDisabledState.Get(), 0);
	else
		g_d3dContext->OMSetDepthStencilState(nullptr, 0);
}

void DrawCall::PushBackfaceCullingState(bool bEnabled)
{
	m_sBackfaceCullingStates.push(bEnabled);

	if (!m_sBackfaceCullingStates.top())
		g_d3dContext->RSSetState(m_d3dBackfaceCullingDisabledState.Get());
	else
		g_d3dContext->RSSetState(nullptr);
}

void DrawCall::PopBackfaceCullingState()
{
	if (m_sBackfaceCullingStates.size() <= 1)
		return;

	m_sBackfaceCullingStates.pop();

	if (!m_sBackfaceCullingStates.top())
		g_d3dContext->RSSetState(m_d3dBackfaceCullingDisabledState.Get());
	else
		g_d3dContext->RSSetState(nullptr);
}

void DrawCall::AddGlobalRenderPass(const string& vertexShaderFilename, const string& pixelShaderFilename, const unsigned renderPassIndex)
{
	RenderPassDesc renderPass;
	renderPass.vertexShaderFilename = vertexShaderFilename;
	renderPass.pixelShaderFilename = pixelShaderFilename;
	renderPass.renderPassIndex = renderPassIndex;
	m_vGlobalRenderPasses.push_back(renderPass);
}

shared_ptr<Shader> DrawCall::LoadShaderUsingShaderStore(Shader::ShaderType type, const std::string& filename)
{
	shared_ptr<Shader> shader;

	auto it = m_shaderStore.find(filename);
	if (it == m_shaderStore.end())
	{
		shader = make_shared<Shader>(type, filename);
		m_shaderStore[filename] = shader;
	}
	else
	{
		shader = it->second;
	}

	return shader;
}

void DrawCall::Present()
{
	if (g_d3dSwapChain)
		g_d3dSwapChain->Present(0, 0);
}

void DrawCall::DrawText(const std::wstring& text, const D2D_RECT_F& layoutRect, float fontSize, HorizontalAlignment horizontalAlignment, VerticalAlignment verticalAlignment, TextColor textColor)
{
	g_d2dContext->BeginDraw();

	const wstring& wideText = text;//StringToWideString(text);

	Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
	const auto& textFormatRecord = g_textFormats.find(fontSize);
	if (textFormatRecord != g_textFormats.end())
	{
		textFormat = textFormatRecord->second;
	}
	else
	{
		g_dwriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			fontSize,
			L"en-US",
			&textFormat);

		switch (horizontalAlignment)
		{
		case HorizontalAlignment::Left:
			textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
			break;
		case HorizontalAlignment::Center:
			textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			break;
		case HorizontalAlignment::Right:
			textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
			break;
		}

		switch (verticalAlignment)
		{
		case VerticalAlignment::Top:
			textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
			break;
		case VerticalAlignment::Middle:
			textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			break;
		case VerticalAlignment::Bottom:
			textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
			break;
		}
	}

	ID2D1SolidColorBrush* pBrush = g_whiteBrush.Get();
	if (textColor == TextColor::Gray)
		pBrush = g_grayBrush.Get();
	else if (textColor == TextColor::Black)
		pBrush = g_blackBrush.Get();

	g_d2dContext->DrawText(wideText.c_str(), (UINT32) text.size(), textFormat.Get(), layoutRect, pBrush);

	g_d2dContext->EndDraw();
}

void DrawCall::DrawText(const std::string& text, const D2D_RECT_F& layoutRect, float fontSize, HorizontalAlignment horizontalAlignment, VerticalAlignment verticalAlignment, TextColor textColor)
{
	wstring wideText = StringToWideString(text);
	DrawText(wideText, layoutRect, fontSize, horizontalAlignment, verticalAlignment, textColor);
}

void DrawCall::DrawText(const std::string &text, float x, float y, float fontSize, TextColor textColor)
{
	D2D1_RECT_F rect;
	rect.left = x;
	rect.top = y;
	rect.right = FLT_MAX;
	rect.bottom = FLT_MAX;

	DrawText(text, rect, fontSize, HorizontalAlignment::Left, VerticalAlignment::Top, textColor);
}

XMMATRIX DrawCall::CalculateWorldTransformForLine(const XMVECTOR& startPosition, const XMVECTOR& endPosition, const float radius, const XMVECTOR& rightDirection)
{
	if (XMVector3Equal(startPosition, endPosition))
		return XMMatrixIdentity();

	XMVECTOR vectorToTarget = endPosition - startPosition;
	XMMATRIX translation = XMMatrixTranslationFromVector(startPosition + (vectorToTarget / 2.0f));

	float length = XMVectorGetX(XMVector3Length(vectorToTarget));
	XMMATRIX scale = XMMatrixScaling(radius * 2.0f, length, radius * 2.0f);

	XMMATRIX rotationToZAxisAligned = XMMatrixRotationAxis(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XM_PIDIV2);

	XMMATRIX rotationToTargetAligned = XMMatrixLookToRH(
		XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
		XMVector3Normalize(vectorToTarget),
		rightDirection);
	rotationToTargetAligned = XMMatrixTranspose(rotationToTargetAligned);

	return scale * rotationToZAxisAligned * rotationToTargetAligned * translation;
}

XMMATRIX DrawCall::CalculateWorldTransformForLine(const XMVECTOR& startPosition, const XMVECTOR& endPosition, const float radius)
{
	return CalculateWorldTransformForLine(startPosition, endPosition, radius, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

#if 1
DrawCall::DrawCall()
	: m_instanceBufferNeedsUpdate(true)
	, m_particleInstancingEnabled(true)
	, allInstanceWorldTransform(XMMatrixIdentity())

{
}
#endif

void DrawCall::LoadMesh(const string& vertexShaderFilename, const string& pixelShaderFilename, shared_ptr<Mesh> mesh, const string& geometryShaderFilename)
{
	assert(g_d3dDevice);

	AddRenderPass(vertexShaderFilename, pixelShaderFilename, 0, geometryShaderFilename);

	SetInstanceCapacity(1, false);
	m_instanceBufferNeedsUpdate = true;

	allInstanceWorldTransform = XMMatrixIdentity();

	m_mesh = mesh;
	if (!m_mesh)
		m_mesh = make_shared<Mesh>();

	for (auto renderPass : m_vGlobalRenderPasses)
		AddRenderPass(renderPass.vertexShaderFilename, renderPass.pixelShaderFilename, renderPass.renderPassIndex, renderPass.geometryShaderFilename);
}

/// <summary>
/// メッシュ描画オブジェクトを生成する
/// </summary>
/// <param name="vertexShaderFilename">頂点シェーダーhlslファイル名</param>
/// <param name="pixelShaderFilename">ピクセルシェーダーhlslファイル名</param>
/// <param name="mesh">メッシュ</param>
/// <param name="geometryShaderFilename">ジェオメトリシェーダーのファイル名</param>
DrawCall::DrawCall(const string& vertexShaderFilename, const string& pixelShaderFilename, shared_ptr<Mesh> mesh, const string& geometryShaderFilename)
{
	this->LoadMesh(vertexShaderFilename, pixelShaderFilename, mesh, geometryShaderFilename);
};

DrawCall::DrawCall(const std::string& vertexShaderFilename, const std::string& pixelShaderFilename, Mesh::MeshType meshType, const std::string& geometryShaderFilename):
	DrawCall(vertexShaderFilename, pixelShaderFilename, make_shared<Mesh>(meshType), geometryShaderFilename)
{}

DrawCall::DrawCall(const std::string& vertexShaderFilename, const std::string& pixelShaderFilename, const std::string& modelFilename, const std::string& geometryShaderFilename):
	DrawCall(vertexShaderFilename, pixelShaderFilename, make_shared<Mesh>(modelFilename), geometryShaderFilename)
{}

void DrawCall::AddRenderPass(const string& vertexShaderFilename, const string& pixelShaderFilename, unsigned renderPassIndex, const string& geometryShaderFilename)
{
	ShaderSet shaderSet;

	shaderSet.vertexShader = LoadShaderUsingShaderStore(Shader::ST_VERTEX, vertexShaderFilename);
	shaderSet.pixelShader = LoadShaderUsingShaderStore(Shader::ST_PIXEL, pixelShaderFilename);

	if (!geometryShaderFilename.empty())
		shaderSet.geometryShader = LoadShaderUsingShaderStore(Shader::ST_GEOMETRY, geometryShaderFilename);

	if (DrawCall::IsSinglePassSteroSupported())
	{
		string extension = GetFilenameExtension(vertexShaderFilename);
		string vertexShaderSPSFilename = RemoveFilenameExtension(vertexShaderFilename);
		vertexShaderSPSFilename = vertexShaderSPSFilename + "_SPS." + extension;

		if (FileExists(vertexShaderSPSFilename))
			shaderSet.vertexShaderSPS = LoadShaderUsingShaderStore(Shader::ST_VERTEX_SPS, vertexShaderSPSFilename);
	}

	m_shaderSets[renderPassIndex] = shaderSet;
}

unsigned DrawCall::GetInstanceCapacity()
{
	return (unsigned) m_instances.size();
}

void DrawCall::SetInstanceCapacity(unsigned instanceCapacity, bool enableParticleInstancing)
{
	if (enableParticleInstancing != m_particleInstancingEnabled)
	{
		m_instancingLayout.Reset();
		m_instancingLayoutSPS.Reset();

		m_instances.clear();
		m_particleInstances.clear();
	}

	vector<D3D11_INPUT_ELEMENT_DESC> elements = g_instancedElements;
	unsigned instanceSize = sizeof(Instance);
	m_instances.resize(instanceCapacity);
	if (enableParticleInstancing)
	{
		elements = g_instancedParticleElements;
		instanceSize = sizeof(ParticleInstance);
		m_particleInstances.resize(instanceCapacity);
	}

	if (!m_instancingLayout && GetVertexShader())
	{
		g_d3dDevice->CreateInputLayout(elements.data(), (UINT) elements.size(), GetVertexShader()->GetBytecode(), GetVertexShader()->GetBytecodeSize(), &m_instancingLayout);
	}
	if (!m_instancingLayoutSPS && GetVertexShaderSPS())
	{
		for (auto &element : elements)
		{
			if (element.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA)
				element.InstanceDataStepRate = 2;
		}

		g_d3dDevice->CreateInputLayout(elements.data(), (UINT) elements.size(), GetVertexShaderSPS()->GetBytecode(), GetVertexShaderSPS()->GetBytecodeSize(), &m_instancingLayoutSPS);
	}

	D3D11_BUFFER_DESC d3dBufferDesc;
	memset(&d3dBufferDesc, 0, sizeof(D3D11_BUFFER_DESC));
	d3dBufferDesc.ByteWidth = instanceCapacity * instanceSize;

	d3dBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	d3dBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	d3dBufferDesc.StructureByteStride = instanceSize;

	g_d3dDevice->CreateBuffer(&d3dBufferDesc, nullptr, &m_instanceBuffer);
	assert(m_instanceBuffer);

	m_instanceBufferNeedsUpdate = true;
	m_particleInstancingEnabled = enableParticleInstancing;
}

DrawCall::Instance* DrawCall::GetInstanceBuffer()
{
	if (m_particleInstancingEnabled)
		return nullptr;

	m_instanceBufferNeedsUpdate = true;
	return m_instances.data();
}

DrawCall::ParticleInstance* DrawCall::GetParticleInstanceBuffer()
{
	if (!m_particleInstancingEnabled)
		return nullptr;

	m_instanceBufferNeedsUpdate = true;
	return m_particleInstances.data();
}

void DrawCall::SetWorldTransform(const XMMATRIX& worldTransform, unsigned instanceIndex)
{
	if (instanceIndex < m_instances.size())
		m_instances[instanceIndex].worldTransform = worldTransform;

	m_instanceBufferNeedsUpdate = true;
}

XMMATRIX DrawCall::GetWorldTransform(unsigned instanceIndex) const
{
	if (instanceIndex >= m_instances.size())
		return XMMatrixIdentity();

	return m_instances[instanceIndex].worldTransform;
}

void DrawCall::SetColor(const XMVECTOR& color, unsigned instanceIndex)
{
	if (m_particleInstancingEnabled == false)
	{
		if (instanceIndex < m_instances.size())
			m_instances[instanceIndex].color = color;
	}
	else
	{
		if (instanceIndex < m_particleInstances.size())
			m_particleInstances[instanceIndex].color = color;
	}
	m_instanceBufferNeedsUpdate = true;
}

bool DrawCall::TestPointInside(const XMVECTOR& pointInWorldSpace, const unsigned instanceIndex)
{
	if (instanceIndex < m_instances.size())
		return m_mesh->TestPointInside(pointInWorldSpace, m_instances[instanceIndex].worldTransform);
	else
		return false;
}

bool DrawCall::TestRayIntersection(const XMVECTOR& rayOriginInWorldSpace, const XMVECTOR& rayDirectionInWorldSpace, float &distance, XMVECTOR &normal, const unsigned instanceIndex)
{
	if (instanceIndex < m_instances.size())
		return m_mesh->TestRayIntersection(rayOriginInWorldSpace, rayDirectionInWorldSpace, m_instances[instanceIndex].worldTransform, distance, normal);
	else
		return false;
}

bool DrawCall::TestRayIntersection(const XMVECTOR& rayOriginInWorldSpace, const XMVECTOR& rayDirectionInWorldSpace, float& distance, const unsigned instanceIndex)
{
	XMVECTOR normal = XMVectorZero();
	return TestRayIntersection(rayOriginInWorldSpace, rayDirectionInWorldSpace, distance, normal, instanceIndex);
}


shared_ptr<Shader> DrawCall::GetVertexShader(unsigned renderPassIndex)
{
	return m_shaderSets[renderPassIndex].vertexShader;
}

shared_ptr<Shader> DrawCall::GetPixelShader(unsigned renderPassIndex)
{
	return m_shaderSets[renderPassIndex].pixelShader;
}

shared_ptr<Shader> DrawCall::GetGeometryShader(unsigned renderPassIndex)
{
	return m_shaderSets[renderPassIndex].geometryShader;
}

shared_ptr<Shader> DrawCall::GetVertexShaderSPS(unsigned renderPassIndex)
{
	return m_shaderSets[renderPassIndex].vertexShaderSPS;
}

/// <summary>
/// 描画オブジェクトを描画する
/// </summary>
/// <param name="instancesToDraw"></param>
void DrawCall::Draw(unsigned instancesToDraw)
{
	SetupDraw(instancesToDraw);

	if (GetCurrentRenderTarget()->IsStereo() && IsSinglePassSteroEnabled())
		instancesToDraw *= 2;

	g_d3dContext->DrawIndexedInstanced(m_mesh->GetIndexCount(), instancesToDraw, 0, 0, 0);
}

/// <summary>
/// Draw関数から呼ばれ描画に際して準備を行う
/// </summary>
/// <param name="instancesToDraw"></param>
void DrawCall::SetupDraw(unsigned instancesToDraw)
{
	if (GetCurrentRenderTarget()->IsStereo() && IsSinglePassSteroEnabled())
		g_d3dContext->IASetInputLayout(m_instancingLayoutSPS.Get());
	else
		g_d3dContext->IASetInputLayout(m_instancingLayout.Get());

	// Auto-scale quad to fullscreen if in fullscreen pass
	if(!m_sFullscreenPassStates.empty() && m_sFullscreenPassStates.top())
	{
		float fAspect = GetCurrentRenderTarget()->GetWidth() / (float) GetCurrentRenderTarget()->GetHeight();
		SetWorldTransform(XMMatrixScaling(fAspect, 1.f, 1.f));
	}

	ShaderSet& shaderSet = m_shaderSets[m_activeRenderPassIndex];
	assert(shaderSet.vertexShader && shaderSet.pixelShader);
	if (GetCurrentRenderTarget()->IsStereo() && IsSinglePassSteroEnabled())//シングルパスステレオがレンダーターゲットであるか？
	{
		shaderSet.vertexShaderSPS->Bind();
		UpdateShaderConstants(shaderSet.vertexShaderSPS);//SPSはステレオ用
	}
	else
	{
		shaderSet.vertexShader->Bind();//描画オブジェクトに結びつけられた頂点シェーダーのバインドをする
		UpdateShaderConstants(shaderSet.vertexShader);
	}

	shaderSet.pixelShader->Bind();//描画オブジェクトに結びつけられたピクセルシェーダーのバインド
	UpdateShaderConstants(shaderSet.pixelShader);

	if (shaderSet.geometryShader)
	{
		shaderSet.geometryShader->Bind();
		UpdateShaderConstants(shaderSet.geometryShader);
	}

	unsigned instanceSize = sizeof(Instance);
	unsigned instanceCapacity = (unsigned) m_instances.size();
	void* instanceData = m_instances.data();
	if (m_particleInstancingEnabled)
	{
		instanceSize = sizeof(ParticleInstance);
		instanceCapacity = (unsigned) m_particleInstances.size();
		instanceData = m_particleInstances.data();
	}

	if (m_instanceBufferNeedsUpdate)
	{
		if (instancesToDraw > instanceCapacity)
			instancesToDraw = instanceCapacity;

		g_d3dContext->UpdateSubresource(m_instanceBuffer.Get(), 0, nullptr, instanceData, instancesToDraw * instanceSize, 0);
		m_instanceBufferNeedsUpdate = false;
	}

	ID3D11Buffer* buffers[2];
	buffers[0] = m_mesh->GetVertexBuffer();//頂点バッファの設定
	buffers[1] = m_instanceBuffer.Get();

	UINT strides[2];
	strides[0] = sizeof(Mesh::Vertex);
	strides[1] = instanceSize;

	UINT offsets[2];
	offsets[0] = 0;
	offsets[1] = 0;

	g_d3dContext->IASetVertexBuffers(0, 2, buffers, strides, offsets);
	g_d3dContext->IASetIndexBuffer(m_mesh->GetIndexBuffer(), DXGI_FORMAT_R32_UINT, 0);

	if (m_mesh->GetDrawStyle() == Mesh::DS_LINELIST)
		g_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	else
		g_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

/// <summary>
/// SetupDraw関数から呼ばれる
/// </summary>
/// <param name="shader"></param>
void DrawCall::UpdateShaderConstants(shared_ptr<Shader> shader)
{
	unsigned N_constant_buffer_count = shader->GetContantBufferCount();
	for(unsigned i = 0 ; i < N_constant_buffer_count ; ++i)
	{
		ConstantBuffer& buffer = shader->GetConstantBuffer(i);

		// Grab the camera view and proj settings (which may be different from active view and proj if this is a fullscreen pass)
		XMMATRIX mtxCameraView = m_activeView.mtx;
		Projection cameraProj = m_activeProjection;
		if(!m_sFullscreenPassStates.empty() && m_sFullscreenPassStates.top() == true)	// For fullscreen pass, use the camera view matrix instead of the view matrix
		{
			mtxCameraView = m_mtxCameraView;
			cameraProj = m_cameraProj;
		}

		unsigned buffer_constants_size = buffer.constants.size();
		for(unsigned j = 0 ; j < buffer_constants_size ; ++j)
		{
			Constant& constant = buffer.constants[j];

			XMMATRIX mtx;
			XMVECTOR v;

			switch(constant.id)
			{
			case CONST_WORLD_MATRIX:
				mtx = XMMatrixTranspose(allInstanceWorldTransform);
				memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size);
				break;
			case CONST_WORLDVIEWPROJ_MATRIX:
				if (constant.elementCount == 0)
				{
					XMMATRIX view = m_activeView.mtx;
					XMMATRIX proj = m_activeProjection.mtx;
					if (IsRightEyePassActive())
					{
						view = m_activeView.mtxRight;
						proj = m_activeProjection.mtxRight;
					}

					mtx = XMMatrixTranspose(XMMatrixMultiply(allInstanceWorldTransform, XMMatrixMultiply(view, proj)));
					memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size);
				}
				else if (constant.elementCount == 2)
				{
					mtx = XMMatrixTranspose(XMMatrixMultiply(allInstanceWorldTransform, XMMatrixMultiply(m_activeView.mtx, m_activeProjection.mtx)));
					memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size / 2);

					mtx = XMMatrixTranspose(XMMatrixMultiply(allInstanceWorldTransform, XMMatrixMultiply(m_activeView.mtxRight, m_activeProjection.mtxRight)));
					memcpy(buffer.staging.get() + constant.startOffset + constant.size / 2, &mtx, constant.size / 2);
				}
				break;
			case CONST_VIEWPROJ_MATRIX:
				if (constant.elementCount == 0)
				{
					XMMATRIX view = m_activeView.mtx;
					XMMATRIX proj = m_activeProjection.mtx;
					if (IsRightEyePassActive())
					{
						view = m_activeView.mtxRight;
						proj = m_activeProjection.mtxRight;
					}

					mtx = XMMatrixTranspose(XMMatrixMultiply(view, proj));
					memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size);
				}
				else if (constant.elementCount == 2)
				{
					mtx = XMMatrixTranspose(XMMatrixMultiply(m_activeView.mtx, m_activeProjection.mtx));
					memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size/2);

					mtx = XMMatrixTranspose(XMMatrixMultiply(m_activeView.mtxRight, m_activeProjection.mtxRight));
					memcpy(buffer.staging.get() + constant.startOffset + constant.size / 2, &mtx, constant.size / 2);
				}
				break;
			case CONST_VIEW_MATRIX:
				if (constant.elementCount == 0)
				{
					XMMATRIX view = m_activeView.mtx;
					XMMATRIX proj = m_activeProjection.mtx;
					if (IsRightEyePassActive())
					{
						view = m_activeView.mtxRight;
						proj = m_activeProjection.mtxRight;
					}

					mtx = XMMatrixTranspose(view);
					memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size);
				}
				else if (constant.elementCount == 2)
				{
					mtx = XMMatrixTranspose(m_activeView.mtx);
					memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size/2);

					mtx = XMMatrixTranspose(m_activeView.mtxRight);
					memcpy(buffer.staging.get() + constant.startOffset + constant.size / 2, &mtx, constant.size / 2);
				}
				break;
			case CONST_INVVIEW_MATRIX:
				if (constant.elementCount == 0)
				{
					XMMATRIX view = XMMatrixInverse(nullptr, m_activeView.mtx);
					if (IsRightEyePassActive())
						view = XMMatrixInverse(nullptr, m_activeView.mtxRight);

					mtx = XMMatrixTranspose(view);
					memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size);
				}
				else if (constant.elementCount == 2)
				{
					mtx = XMMatrixTranspose(XMMatrixInverse(nullptr, m_activeView.mtx));
					memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size / 2);

					mtx = XMMatrixTranspose(XMMatrixInverse(nullptr, m_activeView.mtxRight));
					memcpy(buffer.staging.get() + constant.startOffset + constant.size / 2, &mtx, constant.size / 2);
				}
				break;
			case CONST_LIGHTVIEWPROJ_MATRIX:
				mtx = XMMatrixTranspose(m_mtxLightViewProj);
				memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size);
				break;
			case CONST_LIGHTPOSV:
				v = XMVector4Transform(vLights[uActiveLightIdx].vLightPosW, mtxCameraView);
				memcpy(buffer.staging.get() + constant.startOffset, &v, constant.size);
				break;
			case CONST_INVVIEWLIGHTVIEWPROJ_MATRIX:
				mtx = XMMatrixTranspose(XMMatrixMultiply(XMMatrixInverse(&v, mtxCameraView), m_mtxLightViewProj));
				memcpy(buffer.staging.get() + constant.startOffset, &mtx, constant.size);
				break;
			case CONST_LIGHT_AMBIENT:
				memcpy(buffer.staging.get() + constant.startOffset, &vAmbient, constant.size);
				break;
			case CONST_NEARPLANEHEIGHT:
				memcpy(buffer.staging.get() + constant.startOffset, &cameraProj.fNearPlaneHeight, constant.size);
				break;
			case CONST_NEARPLANEWIDTH:
				memcpy(buffer.staging.get() + constant.startOffset, &cameraProj.fNearPlaneWidth, constant.size);
				break;
			case CONST_NEARPLANEDIST:
				memcpy(buffer.staging.get() + constant.startOffset, &cameraProj.fNear, constant.size);
				break;
			case CONST_FARPLANEDIST:
				memcpy(buffer.staging.get() + constant.startOffset, &cameraProj.fFar, constant.size);
				break;
			case CONST_PROJECTIONRANGE:
				memcpy(buffer.staging.get() + constant.startOffset, &cameraProj.fRange, constant.size);
				break;
			}
		}

		D3D11_MAPPED_SUBRESOURCE mapped;
		g_d3dContext->Map(buffer.d3dBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		memcpy(mapped.pData, buffer.staging.get(), mapped.RowPitch);
		g_d3dContext->Unmap(buffer.d3dBuffer.Get(), 0);

		if(shader->GetType() == Shader::ST_VERTEX || shader->GetType() == Shader::ST_VERTEX_SPS)
			g_d3dContext->VSSetConstantBuffers(buffer.slot, 1, buffer.d3dBuffer.GetAddressOf());
		else if (shader->GetType() == Shader::ST_PIXEL)
			g_d3dContext->PSSetConstantBuffers(buffer.slot, 1, buffer.d3dBuffer.GetAddressOf());
		else if (shader->GetType() == Shader::ST_GEOMETRY)
			g_d3dContext->GSSetConstantBuffers(buffer.slot, 1, buffer.d3dBuffer.GetAddressOf());
	}
}

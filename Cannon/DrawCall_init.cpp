#include "pch.h"

#include "DrawCall.h"
#include "Common/FileUtilities.h"
#include "MixedReality.h"

#include <iostream>
#include <fstream>
#include <sstream>

#include <wincodec.h>

#include <cassert>

using namespace std;
using namespace DirectX;

//í«â¡
static bool g_initialized = false;
Microsoft::WRL::ComPtr<IDXGIAdapter3> g_dxgiAdapter;
Microsoft::WRL::ComPtr<ID3D11Device4>  g_d3dDevice4;

extern Microsoft::WRL::ComPtr<ID3D11Device> g_d3dDevice;
extern Microsoft::WRL::ComPtr<ID3D11DeviceContext> g_d3dContext;

extern Microsoft::WRL::ComPtr<ID2D1Factory1> g_d2dFactory;
extern Microsoft::WRL::ComPtr<ID2D1Device> g_d2dDevice;
extern Microsoft::WRL::ComPtr<ID2D1DeviceContext> g_d2dContext;
extern Microsoft::WRL::ComPtr<IDWriteFactory> g_dwriteFactory;

extern Microsoft::WRL::ComPtr<IDXGISwapChain1> g_d3dSwapChain;

//TODO: Move these into the class
extern std::map<float, Microsoft::WRL::ComPtr<IDWriteTextFormat>> g_textFormats;	// Text format cache, organized by font size
extern Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g_whiteBrush;
extern Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g_grayBrush;
extern Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g_blackBrush;

/// <summary>
/// DirectXëSëÃÇÃèâä˙âªÇ»Ç«
/// </summary>
/// <returns></returns>
bool DrawCall::Initialize()
{
	//
	// Create device
	//

	if(g_initialized) return true;
	g_initialized = true;

	unsigned createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;	// Required for D2D to work
#ifdef _DEBUG
	//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL selectedFeatureLevel;

//#if 0
	if (!MixedReality::IsAvailable())
	{
		D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
		D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, createDeviceFlags, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &g_d3dDevice, &selectedFeatureLevel, nullptr);
		g_d3dDevice->GetImmediateContext(&g_d3dContext);
	}
	else
	{
		g_holographic_space = winrt::Windows::Graphics::Holographic::HolographicSpace::CreateForCoreWindow(winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread());

		// The holographic space might need to determine which adapter supports
		// holograms, in which case it will specify a non-zero PrimaryAdapterId.
		LUID id =
		{
			g_holographic_space.PrimaryAdapterId().LowPart,
			g_holographic_space.PrimaryAdapterId().HighPart
		};

		// When a primary adapter ID is given to the app, the app should find
		// the corresponding DXGI adapter and use it to create Direct3D devices
		// and device contexts. Otherwise, there is no restriction on the DXGI
		// adapter the app can use.
		if ((id.HighPart != 0) || (id.LowPart != 0))
		{
			UINT createFlags = 0;
#ifdef DEBUG
			if (DX::SdkLayersAvailable())
			{
				createFlags |= DXGI_CREATE_FACTORY_DEBUG;
			}
#endif
			// Create the DXGI factory.
			Microsoft::WRL::ComPtr<IDXGIFactory1> dxgiFactory;
			winrt::check_hresult(
				CreateDXGIFactory2(
					createFlags,
					IID_PPV_ARGS(&dxgiFactory)
				));
			Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory4;
			winrt::check_hresult(dxgiFactory.As(&dxgiFactory4));

			// Retrieve the adapter specified by the holographic space.
			winrt::check_hresult(
				dxgiFactory4->EnumAdapterByLuid(
					id,
					IID_PPV_ARGS(&g_dxgiAdapter)
				));
		}
		else
		{
			g_dxgiAdapter.Reset();
		}

		// This flag adds support for surfaces with a different color channel ordering
		// than the API default. It is required for compatibility with Direct2D.
		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
		//if (DX::SdkLayersAvailable())
		//{
		//	// If the project is in a debug build, enable debugging via SDK Layers with this flag.
		//	creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
		//}
#endif

		// This array defines the set of DirectX hardware feature levels this app will support.
		// Note the ordering should be preserved.
		// Note that HoloLens supports feature level 11.1. The HoloLens emulator is also capable
		// of running on graphics cards starting with feature level 10.0.
		D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0
		};

		// Create the Direct3D 11 API device object and a corresponding context.
		//Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

		const D3D_DRIVER_TYPE driverType = g_dxgiAdapter == nullptr ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_UNKNOWN;
		D3D_FEATURE_LEVEL _d3dFeatureLevel = D3D_FEATURE_LEVEL_10_0;
		const HRESULT hr = D3D11CreateDevice(
			g_dxgiAdapter.Get(),        // Either nullptr, or the primary adapter determined by Windows Holographic.
			driverType,                 // Create a device using the hardware graphics driver.
			0,                          // Should be 0 unless the driver is D3D_DRIVER_TYPE_SOFTWARE.
			creationFlags,              // Set debug and Direct2D compatibility flags.
			featureLevels,              // List of feature levels this app can support.
			ARRAYSIZE(featureLevels),   // Size of the list above.
			D3D11_SDK_VERSION,          // Always set this to D3D11_SDK_VERSION for Windows Runtime apps.
			&g_d3dDevice,                    // Returns the Direct3D device created.
			&_d3dFeatureLevel,         // Returns feature level of device created.
			&context                    // Returns the device immediate context.
		);

		if (FAILED(hr))
		{
			// If the initialization fails, fall back to the WARP device.
			// For more information on WARP, see:
			// http://go.microsoft.com/fwlink/?LinkId=286690
			winrt::check_hresult(
				D3D11CreateDevice(
					nullptr,              // Use the default DXGI adapter for WARP.
					D3D_DRIVER_TYPE_WARP, // Create a WARP device instead of a hardware device.
					0,
					creationFlags,
					featureLevels,
					ARRAYSIZE(featureLevels),
					D3D11_SDK_VERSION,
					&g_d3dDevice,
					&_d3dFeatureLevel,
					&context
				));
		}
		// Store pointers to the Direct3D device and immediate context.
		winrt::check_hresult(g_d3dDevice.As(&g_d3dDevice4));
		winrt::check_hresult(context.As(&g_d3dContext));
	}
	// Check for device support for the optional feature that allows setting the render target array index from the vertex shader
	m_singlePassStereoSupported = false;
	m_singlePassStereoEnabled = false;
	D3D11_FEATURE_DATA_D3D11_OPTIONS3 d3dOptions;
	g_d3dDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3, &d3dOptions, sizeof(d3dOptions));
	if (d3dOptions.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer)
	{
		m_singlePassStereoSupported = true;
		m_singlePassStereoEnabled = true;
	}

	//
	// Create D2D and DWrite stuff
	//

	D2D1_FACTORY_OPTIONS options;
	ZeroMemory(&options, sizeof(D2D1_FACTORY_OPTIONS));

#if defined(_DEBUG)
	// If the project is in a debug build, enable Direct2D debugging via SDK Layers.
	//options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, &g_d2dFactory);
	
	DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &g_dwriteFactory);

	Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
	g_d3dDevice.As(&dxgiDevice);
	g_d2dFactory->CreateDevice(dxgiDevice.Get(), &g_d2dDevice);
	g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_d2dContext);
	
	g_d2dContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	
	g_d2dContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &g_whiteBrush);
	g_d2dContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray), &g_grayBrush);
	g_d2dContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &g_blackBrush);

	//
	// Setup static objects (and set their values to reasonable defaults)
	//

	m_activeRenderPassIndex = 0;
	SetBackBuffer(make_shared<Texture2D>(512, 512, DXGI_FORMAT_B8G8R8A8_UNORM));

	vAmbient = XMVectorSet(.1f, .1f, .1f, 1.f);

	vLights[0].vLightPosW = XMVectorSet(7.5f, 10.f, -2.5f, 1.f);
	vLights[0].vLightAtW = XMVectorSet(0.f, 0.f, 0.f, 1.f);
	uLightCount = 1;

	DrawCall::PushView(XMVectorSet(5.f, 5.f, 5.f, 1.f), XMVectorSet(0.f, 0.f, 0.f, 1.f), XMVectorSet(0.f, 1.f, 0.f, 0.f));
	DrawCall::PushProj(XM_PIDIV4, m_backBuffer->GetAspect(), 1.0f, 1000.0f);

	m_mtxLightViewProj = XMMatrixIdentity();
	m_mtxCameraView = XMMatrixIdentity();

	// Create the alpha blend state
	D3D11_BLEND_DESC blendDesc;
	memset(&blendDesc, 0, sizeof(blendDesc));
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	g_d3dDevice->CreateBlendState(&blendDesc, &m_d3dAlphaBlendEnabledState);
	DrawCall::PushAlphaBlendState(BLEND_NONE);

	// Create the additive blend state
	memset(&blendDesc, 0, sizeof(blendDesc));
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	g_d3dDevice->CreateBlendState(&blendDesc, &m_d3dAdditiveBlendEnabledState);

	// Create the color write disabled state
	memset(&blendDesc, 0, sizeof(blendDesc));
	g_d3dDevice->CreateBlendState(&blendDesc, &m_d3dColorWriteDisabledState);

	// Create the depth test disabled state
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	memset(&depthStencilDesc, 0, sizeof(depthStencilDesc));
	depthStencilDesc.DepthEnable = FALSE;
	g_d3dDevice->CreateDepthStencilState(&depthStencilDesc, &m_d3dDepthTestDisabledState);
	DrawCall::PushDepthTestState(true);

	// Create the backface culling disabled state
	D3D11_RASTERIZER_DESC rasterizerDesc;
	memset(&rasterizerDesc, 0, sizeof(rasterizerDesc));
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.FrontCounterClockwise = FALSE;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.SlopeScaledDepthBias = 0.f;
	rasterizerDesc.DepthBiasClamp = 0.f;
	rasterizerDesc.DepthClipEnable = TRUE;
	rasterizerDesc.ScissorEnable = FALSE;
	rasterizerDesc.MultisampleEnable = FALSE;
	rasterizerDesc.AntialiasedLineEnable = FALSE;
	g_d3dDevice->CreateRasterizerState(&rasterizerDesc, &m_d3dBackfaceCullingDisabledState);
	DrawCall::PushBackfaceCullingState(true);

	return true;
}

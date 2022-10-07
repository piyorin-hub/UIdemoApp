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

const unsigned GetBitDepthForD3DFormat(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R32G32_FLOAT:
		return 64;

	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_R32_FLOAT:
		return 32;

	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_UNORM:
		return 16;

	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_UNORM:
		return 8;

	default:
		return 0;
	}
}

struct DDS_PIXELFORMAT
{
	DWORD dwSize;
	DWORD dwFlags;
	DWORD dwFourCC;
	DWORD dwRGBBitCount;
	DWORD dwRBitMask;
	DWORD dwGBitMask;
	DWORD dwBBitMask;
	DWORD dwABitMask;
};

struct DDS_HEADER
{
	DWORD			dwSize;
	DWORD			dwFlags;
	DWORD			dwHeight;
	DWORD			dwWidth;
	DWORD			dwPitchOrLinearSize;
	DWORD			dwDepth;
	DWORD			dwMipMapCount;
	DWORD			dwReserved1[11];
	DDS_PIXELFORMAT ddspf;
	DWORD			dwCaps;
	DWORD			dwCaps2;
	DWORD			dwCaps3;
	DWORD			dwCaps4;
	DWORD			dwReserved2;
};

Image CreateImageUsingDDSLoader(string filename)
{
	Image image;

	FILE* pFile = OpenFile(filename, "rb");
	assert(pFile);

	unsigned magicNumber;
	fread(&magicNumber, sizeof(magicNumber), 1, pFile);
	assert(magicNumber == 0x20534444);

	DDS_HEADER header;
	fread(&header, sizeof(header), 1, pFile);

	unsigned dx10FourCC = ('D' << 24) + ('X' << 16) + ('1' << 8) + ('0' << 0);
	assert(header.ddspf.dwFourCC != dx10FourCC);

	image.width = header.dwWidth;
	image.height = header.dwHeight;
	image.rowPitch = header.dwPitchOrLinearSize;

	if (header.dwMipMapCount != 0)
		image.mipCount = header.dwMipMapCount;

	if (header.ddspf.dwFlags & 0x40)	// Contains uncompressed RGB data
	{
		if (header.ddspf.dwFlags & 0x1) // Contains alpha data
		{
			image.format = DXGI_FORMAT_B8G8R8A8_UNORM;
			image.bytesPerPixel = 4;
		}
		else
		{
			image.format = DXGI_FORMAT_B8G8R8A8_UNORM;
			image.bytesPerPixel = 4;
		}
	}

	for (unsigned i = 0; i < image.mipCount; ++i)
	{
		unsigned downsampleFactor = (unsigned) pow(2, i);

		unsigned mipDataSize = image.rowPitch / downsampleFactor * image.height / downsampleFactor;
		unique_ptr<unsigned char> mipData(new unsigned char[mipDataSize]);
		fread(mipData.get(), mipDataSize, 1, pFile);
		image.mips.push_back(move(mipData));
	}

	fclose(pFile);

	return image;
}

Image CreateImageUsingWICLoader(string filename)
{
	Image image;

	typedef HRESULT(WINAPI *CreateProxyFn)(UINT SDKVersion, IWICImagingFactory **ppIWICImagingFactory);

	IWICImagingFactory* pImageFactory = nullptr;
#if defined WINAPI_FAMILY && (WINAPI_FAMILY == WINAPI_FAMILY_TV_APP || WINAPI_FAMILY == WINAPI_FAMILY_APP)
	CoCreateInstance(CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&pImageFactory);
#else
	CreateProxyFn pWICCreateImagingFactory_Proxy;
	static HMODULE m = LoadLibraryW(L"WindowsCodecs.dll");
	pWICCreateImagingFactory_Proxy = (CreateProxyFn)GetProcAddress(m, "WICCreateImagingFactory_Proxy");
	(*pWICCreateImagingFactory_Proxy)(WINCODEC_SDK_VERSION2, (IWICImagingFactory**)&pImageFactory);
#endif 
	assert(pImageFactory);

	// First try filename as-is
	IWICBitmapDecoder* pDecoder = nullptr;
	wstring wideFilename = StringToWideString(filename);
	pImageFactory->CreateDecoderFromFilename(wideFilename.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder);
	if (!pDecoder)
	{
		// Then try the filename relative to the executable location
		wstring exePath = StringToWideString(GetExecutablePath());
		pImageFactory->CreateDecoderFromFilename((exePath + L"/" + wideFilename).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder);
	}
	assert(pDecoder);	// Unsupported format or file not found

	IWICBitmapFrameDecode* pSource = nullptr;
	pDecoder->GetFrame(0, &pSource);

	pSource->GetSize(&image.width, &image.height);

	WICPixelFormatGUID wicFormat;
	pSource->GetPixelFormat(&wicFormat);
	if (wicFormat == GUID_WICPixelFormat32bppRGBA)
	{
		image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		image.bytesPerPixel = GetBitDepthForD3DFormat(image.format) / 8;
		image.rowPitch = image.width * image.bytesPerPixel;
		unique_ptr<unsigned char> data(new unsigned char[image.rowPitch * image.height]);
		pSource->CopyPixels(nullptr, image.rowPitch, image.rowPitch * image.height, data.get());
		image.mips.push_back(move(data));
	}
	else if ((wicFormat == GUID_WICPixelFormat8bppIndexed || wicFormat == GUID_WICPixelFormat8bppGray))
	{
		image.format = DXGI_FORMAT_R32_FLOAT;
		image.bytesPerPixel = GetBitDepthForD3DFormat(image.format) / 8;
		image.rowPitch = image.width * image.bytesPerPixel;
		unique_ptr<unsigned char> data(new unsigned char[image.rowPitch * image.height]);

		IWICFormatConverter* pConverter = nullptr;
		pImageFactory->CreateFormatConverter(&pConverter);
		assert(pConverter);
		pConverter->Initialize(pSource, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut);

		// Copy image data into buffer (have to do an intermediate copy because converting to GUID_WICPixelFormat32bppGrayFloat doesn't seem to result in the right values
		unsigned uScratchDataSize = image.width * image.height;
		unsigned char* pScratchData = new unsigned char[uScratchDataSize];
		pConverter->CopyPixels(nullptr, image.width * 1, uScratchDataSize, pScratchData);
		for (unsigned i = 0; i<image.rowPitch * image.height / 4; ++i)
		{
			((float*)data.get())[i] = (pScratchData)[i] / 255.f;
		}
		pConverter->Release();

		delete[] pScratchData;

		image.mips.push_back(move(data));
	}
	else
	{
		image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		image.bytesPerPixel = GetBitDepthForD3DFormat(image.format) / 8;
		image.rowPitch = image.width * image.bytesPerPixel;
		unique_ptr<unsigned char> data(new unsigned char[image.rowPitch * image.height]);

		IWICFormatConverter* pConverter = nullptr;
		pImageFactory->CreateFormatConverter(&pConverter);
		assert(pConverter);
		pConverter->Initialize(pSource, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut);

		pConverter->CopyPixels(nullptr, image.rowPitch, image.rowPitch * image.height, data.get());
		pConverter->Release();

		image.mips.push_back(move(data));
	}

	pSource->Release();
	pDecoder->Release();
	pImageFactory->Release();

	return image;
}

Image CreateImageFromFile(string filename)
{
	if(!FileExists(filename))
		filename = string("Media/Textures/") + filename;

	if (GetFilenameExtension(filename) == "dds")
		return CreateImageUsingDDSLoader(filename);
	else
		return CreateImageUsingWICLoader(filename);
}

//
//Texture2D
//

Texture2D::Texture2D(unsigned width, unsigned height, DXGI_FORMAT format)
{
	m_width = width;
	m_height = height;
	m_format = format;

	m_isStereo = false;

	m_mappedCount = 0;
	memset(&m_mapped, 0, sizeof(m_mapped));

	CD3D11_TEXTURE2D_DESC textureDesc(
		m_format,
		m_width,
		m_height,
		1,
		1,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);

	ID3D11Texture2D* pTexture = nullptr;
	g_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &pTexture);
	SetD3DTexture(pTexture);
}

Texture2D::Texture2D(ID3D11Texture2D* pD3DTexture)
{
	m_width = 0;
	m_height = 0;
	m_format = DXGI_FORMAT_UNKNOWN;

	m_isStereo = false;

	m_mappedCount = 0;
	memset(&m_mapped, 0, sizeof(m_mapped));

	SetD3DTexture(pD3DTexture);
}

Texture2D::Texture2D(string filename)
{
	m_isStereo = false;

	m_mappedCount = 0;
	memset(&m_mapped, 0, sizeof(m_mapped));

	Image image = CreateImageFromFile(filename);
	assert(image.mips[0].get());

	vector<D3D11_SUBRESOURCE_DATA> initialData;
	for (unsigned i = 0; i < image.mipCount; ++i)
	{
		unsigned downsampleFactor = (unsigned) pow(2, i);

		D3D11_SUBRESOURCE_DATA mip;
		mip.pSysMem = image.mips[i].get();
		mip.SysMemPitch = image.rowPitch / downsampleFactor;
		mip.SysMemSlicePitch = mip.SysMemPitch * image.height / downsampleFactor;
		initialData.push_back(mip);
	}

	ID3D11Texture2D* pTexture = nullptr;
	CD3D11_TEXTURE2D_DESC textureDesc(image.format, image.width, image.height, 1, image.mipCount);
	g_d3dDevice->CreateTexture2D(&textureDesc, initialData.data(), &pTexture);
	assert(pTexture);

	SetD3DTexture(pTexture);
}

void Texture2D::SetD3DTexture(ID3D11Texture2D* pD3DTexture)
{
	Reset();

	assert(pD3DTexture);
	m_texture = pD3DTexture;

	D3D11_TEXTURE2D_DESC textureDesc;
	m_texture->GetDesc(&textureDesc);

	if (textureDesc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS)
		textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

	m_width = textureDesc.Width;
	m_height = textureDesc.Height;
	m_format = textureDesc.Format;

	m_viewport = CD3D11_VIEWPORT(0.0f, 0.0f, (float) m_width, (float) m_height);

	if (textureDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		CD3D11_SHADER_RESOURCE_VIEW_DESC viewDesc(
			(textureDesc.ArraySize > 1) ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D,
			textureDesc.Format);

		g_d3dDevice->CreateShaderResourceView(m_texture.Get(), &viewDesc, &m_shaderResourceView);
		assert(m_shaderResourceView);
	}

	if (textureDesc.BindFlags & D3D11_BIND_RENDER_TARGET)
	{
		m_isRenderTarget = true;

		const CD3D11_RENDER_TARGET_VIEW_DESC rtViewDesc(
			(textureDesc.ArraySize > 1) ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D,
			textureDesc.Format, 0, 0, textureDesc.ArraySize);
		g_d3dDevice->CreateRenderTargetView(m_texture.Get(), &rtViewDesc, &m_renderTargetView);
		assert(m_renderTargetView);

		CD3D11_TEXTURE2D_DESC depthStencilDesc(
			DXGI_FORMAT_R16_TYPELESS,
			textureDesc.Width,
			textureDesc.Height,
			textureDesc.ArraySize,
			1,
			D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);
		g_d3dDevice->CreateTexture2D(&depthStencilDesc, nullptr, &m_depthStencilTexture);
		assert(m_depthStencilTexture);

		CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(
			(depthStencilDesc.ArraySize > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D,
			DXGI_FORMAT_D16_UNORM);
		g_d3dDevice->CreateDepthStencilView(m_depthStencilTexture.Get(), &depthStencilViewDesc, &m_depthStencilView);
		assert(m_depthStencilView);

		if (textureDesc.ArraySize == 2)
		{
			m_isStereo = true;

			CD3D11_RENDER_TARGET_VIEW_DESC rightRenderTargetViewDesc(D3D11_RTV_DIMENSION_TEXTURE2DARRAY, textureDesc.Format, 0, 1);
			g_d3dDevice->CreateRenderTargetView(m_texture.Get(), &rightRenderTargetViewDesc, &m_renderTargetViewRight);
			assert(m_renderTargetViewRight);

			CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewRightDesc(D3D11_DSV_DIMENSION_TEXTURE2DARRAY, DXGI_FORMAT_D16_UNORM, 0, 1);
			g_d3dDevice->CreateDepthStencilView(m_depthStencilTexture.Get(), &depthStencilViewRightDesc, &m_depthStencilViewRight);
			assert(m_depthStencilView);

		}

		Microsoft::WRL::ComPtr<IDXGISurface2> dxgiSurface;
		m_texture.As(&dxgiSurface);
		if (dxgiSurface && (textureDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || textureDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || textureDesc.Format == DXGI_FORMAT_A8_UNORM))
		{
			D2D1_PIXEL_FORMAT pixelFormat;
			pixelFormat.format = textureDesc.Format;
			pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
			D2D1_BITMAP_PROPERTIES1 bitmapProperties;
			memset(&bitmapProperties, 0, sizeof(bitmapProperties));
			bitmapProperties.pixelFormat = pixelFormat;
			bitmapProperties.dpiX = 96.0f;
			bitmapProperties.dpiY = 96.0f;
			bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

			g_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bitmapProperties, &m_d2dTargetBitmap);
		}
	}

	CD3D11_DEFAULT d3dDefaults;
	CD3D11_SAMPLER_DESC samplerDesc(d3dDefaults);
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

	g_d3dDevice->CreateSamplerState(&samplerDesc, &m_samplerState);
	assert(m_samplerState);
}

Texture2D::~Texture2D()
{
	Reset();
}

void Texture2D::Reset()
{
	m_width = m_height = 0;
	m_format = DXGI_FORMAT_UNKNOWN;

	m_isStereo = false;
	m_isRenderTarget = false;

	if (m_mappedCount != 0)
		Unmap();
	m_mappedCount = 0;
	memset(&m_mapped, 0, sizeof(m_mapped));
	m_currentMapType = MapType::Read;

	m_samplerState.Reset();
	m_shaderResourceView.Reset();
	m_renderTargetView.Reset();
	m_renderTargetViewRight.Reset();
	m_texture.Reset();

	m_depthStencilView.Reset();
	m_depthStencilViewRight.Reset();
	m_depthStencilTexture.Reset();

	m_stagingTexture.Reset();

	m_d2dTargetBitmap.Reset();
}

bool Texture2D::UploadData(unsigned char* pData, unsigned size)
{
	unsigned uBytesPerPixel = GetBitDepthForD3DFormat(m_format) / 8;
	assert(uBytesPerPixel != 0);
	unsigned uRequiredSize = m_width * m_height * uBytesPerPixel;
	if (size < uRequiredSize)
		return false;

	if (!m_stagingTexture)
		InitStagingTexture();

	Map(MapType::Write);

	for (unsigned uRow = 0; uRow<m_height; ++uRow)
	{
		unsigned char* pSrc = &(pData[uRow*m_width*uBytesPerPixel]);
		unsigned char* pDst = &(((unsigned char*)m_mapped.pData)[uRow*m_mapped.RowPitch]);

		memcpy(pDst, pSrc, m_width*uBytesPerPixel);
	}

	Unmap();

	return true;
}

void Texture2D::SaveToFile(string filename)
{
	IWICImagingFactory* pImageFactory = nullptr;
	CoCreateInstance(CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&pImageFactory);
	assert(pImageFactory);

	IWICStream* pStream = nullptr;
	pImageFactory->CreateStream(&pStream);
	assert(pStream);

	wstring wideFilename = StringToWideString(filename);
	pStream->InitializeFromFilename(wideFilename.c_str(), GENERIC_WRITE);

	IWICBitmapEncoder* pEncoder = nullptr;
	pImageFactory->CreateEncoder(GUID_ContainerFormatPng, 0, &pEncoder);
	assert(pEncoder);

	pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);

	IWICBitmapFrameEncode* pFrame = nullptr;
	IPropertyBag2* pPropertyBag;
	pEncoder->CreateNewFrame(&pFrame, &pPropertyBag);
	assert(pFrame && pPropertyBag);

	pFrame->Initialize(pPropertyBag);
	pFrame->SetSize(m_width, m_height);
	pFrame->SetResolution(72, 72);

	WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
	pFrame->SetPixelFormat(&format);

	Map(MapType::Read);
	pFrame->WritePixels(m_height, GetPitch(), GetPitch() * m_height, reinterpret_cast<BYTE*>(GetDataPtr()));
	Unmap();

	pFrame->Commit();
	pEncoder->Commit();

	pFrame->Release();
	pPropertyBag->Release();
	pEncoder->Release();
	pStream->Release();
	pImageFactory->Release();
}

void Texture2D::BindAsVertexShaderResource(unsigned slot)
{
	g_d3dContext->VSSetShaderResources(slot, 1, m_shaderResourceView.GetAddressOf());
	g_d3dContext->VSSetSamplers(slot, 1, m_samplerState.GetAddressOf());
}

void Texture2D::BindAsPixelShaderResource(unsigned slot)
{
	g_d3dContext->PSSetShaderResources(slot, 1, m_shaderResourceView.GetAddressOf());
	g_d3dContext->PSSetSamplers(slot, 1, m_samplerState.GetAddressOf());
}

void Texture2D::Clear(float r, float g, float b, float a)
{
	float vColor[4] = { r, g, b, a };

	if(m_renderTargetView)
		g_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), vColor);

	if (m_depthStencilTexture)
		g_d3dContext->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void Texture2D::Clear(const XMVECTOR& color)
{
	Clear(XMVectorGetX(color), XMVectorGetY(color), XMVectorGetZ(color), XMVectorGetW(color));
}

void* Texture2D::Map(MapType mapType)
{
	++m_mappedCount;

	if(m_mappedCount > 1)
		return m_mapped.pData;

	if (!m_stagingTexture)
		InitStagingTexture();

	m_currentMapType = mapType;

	if (mapType == MapType::Read || mapType == MapType::ReadWrite)
	{
		g_d3dContext->CopySubresourceRegion(m_stagingTexture.Get(), 0, 0, 0, 0, m_texture.Get(), 0, nullptr);
		g_d3dContext->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &m_mapped);
	}
	else if (mapType == MapType::Write)
	{
		g_d3dContext->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ_WRITE, 0, &m_mapped);
	}

	if(m_mapped.pData)
	{
		m_mappedCount = 1;
		return m_mapped.pData;
	}
	else
	{
		assert(false);
		return nullptr;
	}
}

void Texture2D::Unmap()
{
	if (!m_texture || !m_stagingTexture)
		return;

	if(m_mappedCount == 0)
		return;

	--m_mappedCount;

	if(m_mappedCount == 0)
	{
		g_d3dContext->Unmap(m_stagingTexture.Get(), 0);

		if (m_currentMapType == MapType::Write || m_currentMapType == MapType::ReadWrite)
			g_d3dContext->CopySubresourceRegion(m_texture.Get(), 0, 0, 0, 0, m_stagingTexture.Get(), 0, nullptr);
	}
}

void* Texture2D::GetDataPtr()
{
	return m_mapped.pData;
}

unsigned Texture2D::GetPitch()
{
	return m_mapped.RowPitch;
}

const D3D11_VIEWPORT& Texture2D::GetViewport()
{
	return m_viewport;
}

void Texture2D::SetViewport(D3D11_VIEWPORT viewport)
{
	m_viewport = viewport;
}

void Texture2D::SetViewport(unsigned width, unsigned height, float minDepth, float maxDepth, float topLeftX, float topLeftY)
{
	m_viewport.Width = (float)width;
	m_viewport.Height = (float)height;
	m_viewport.MinDepth = minDepth;
	m_viewport.MaxDepth = maxDepth;
	m_viewport.TopLeftX = topLeftX;
	m_viewport.TopLeftY = topLeftY;
}

void Texture2D::EnableComparisonSampling(bool enabled)
{
	m_comparisonSamplingEnabled = enabled;

	CD3D11_DEFAULT d3dDefaults;
	CD3D11_SAMPLER_DESC samplerDesc(d3dDefaults);
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

	if (m_comparisonSamplingEnabled)
	{
		samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS;
	}

	g_d3dDevice->CreateSamplerState(&samplerDesc, &m_samplerState);
	assert(m_samplerState);
}

void Texture2D::InitStagingTexture()
{
	assert(g_d3dDevice);
	if(!m_stagingTexture)
	{
		D3D11_TEXTURE2D_DESC desc;
		memset(&desc, 0, sizeof(D3D11_TEXTURE2D_DESC));
		desc.ArraySize = 1;
		desc.CPUAccessFlags	 = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.Format = m_format;
		desc.Height = m_height;
		desc.Width = m_width;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;

		g_d3dDevice->CreateTexture2D(&desc, nullptr, &m_stagingTexture);
	}
	assert(m_stagingTexture);
}


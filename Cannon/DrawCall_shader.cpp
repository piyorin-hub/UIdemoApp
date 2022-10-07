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

//
// Shader
//

Shader::Shader(ShaderType type, string filename)
{
	m_type = type;

	FILE* pFile = OpenFile(filename, "rb");
	assert(pFile);

	fseek(pFile, 0, SEEK_END);	// Might not be portable
	m_shaderBlobSize = ftell(pFile);
	rewind(pFile);

	m_shaderBlob.reset(new unsigned char[m_shaderBlobSize]);
	fread(m_shaderBlob.get(), m_shaderBlobSize, 1, pFile);
	fclose(pFile);

	if(m_type == ST_VERTEX)
	{
		g_d3dDevice->CreateVertexShader(m_shaderBlob.get(), m_shaderBlobSize, nullptr, &m_vertexShader);
		assert(m_vertexShader);
	}
	else if(m_type == ST_PIXEL)
	{
		g_d3dDevice->CreatePixelShader(m_shaderBlob.get(), m_shaderBlobSize, nullptr, &m_pixelShader);
		assert(m_pixelShader);
	}
	else if (m_type == ST_GEOMETRY)
	{
		g_d3dDevice->CreateGeometryShader(m_shaderBlob.get(), m_shaderBlobSize, nullptr, &m_geometryShader);
		assert(m_geometryShader);
	}
	else if (m_type == ST_VERTEX_SPS)
	{
		g_d3dDevice->CreateVertexShader(m_shaderBlob.get(), m_shaderBlobSize, nullptr, &m_vertexShaderSPS);
		assert(m_vertexShaderSPS);
	}

	Microsoft::WRL::ComPtr<ID3D11ShaderReflection> shaderReflect;
	D3DReflect(m_shaderBlob.get(), m_shaderBlobSize, IID_ID3D11ShaderReflection, (void**) &shaderReflect);

	D3D11_SHADER_DESC shaderDesc;
	shaderReflect->GetDesc(&shaderDesc);
	for(unsigned i=0; i<shaderDesc.ConstantBuffers; ++i)
	{
		ID3D11ShaderReflectionConstantBuffer* pBufferReflect = shaderReflect->GetConstantBufferByIndex(i);

		D3D11_SHADER_BUFFER_DESC bufferDesc;
		pBufferReflect->GetDesc(&bufferDesc);

		ConstantBuffer buffer;
		buffer.slot = i;		// Assuming slot is equal to index. This is always true as long as registers aren't manually specified in the shader.
		buffer.size = bufferDesc.Size;
		buffer.staging.reset(new unsigned char[buffer.size]);

		D3D11_BUFFER_DESC desc = {buffer.size, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, buffer.size};
		g_d3dDevice->CreateBuffer(&desc, nullptr, &buffer.d3dBuffer);

		for(unsigned j=0; j<bufferDesc.Variables; ++j)
		{
			ID3D11ShaderReflectionVariable* pVariableReflect = pBufferReflect->GetVariableByIndex(j);
			D3D11_SHADER_VARIABLE_DESC variableDesc;
			pVariableReflect->GetDesc(&variableDesc);

			D3D11_SHADER_TYPE_DESC typeDesc;
			ID3D11ShaderReflectionType* pType = pVariableReflect->GetType();
			pType->GetDesc(&typeDesc);

			Constant constant;
			constant.id = CONST_COUNT;
			constant.startOffset = variableDesc.StartOffset;
			constant.size = variableDesc.Size;
			constant.elementCount = typeDesc.Elements;

			for(ConstantID id=CONST_WORLD_MATRIX; id<CONST_COUNT; id = (ConstantID) (id+1))
			{
				if(strcmp(variableDesc.Name, g_vContantIDStrings[id]) == 0)
				{
					constant.id = id;
					break;
				}
			}

			assert(constant.id != CONST_COUNT);
			buffer.constants.push_back(constant);
		}

		m_vConstantBuffers.push_back(move(buffer));
	}
}

void Shader::Bind()
{
	if(m_type == ST_VERTEX)
		g_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	else if(m_type == ST_PIXEL)
		g_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	else if (m_type == ST_GEOMETRY)
		g_d3dContext->GSSetShader(m_geometryShader.Get(), nullptr, 0);
	else if (m_type == ST_VERTEX_SPS)
		g_d3dContext->VSSetShader(m_vertexShaderSPS.Get(), nullptr, 0);
}

void* Shader::GetBytecode()
{
	if(!m_shaderBlob)
		return nullptr;

	return m_shaderBlob.get();
}

unsigned long Shader::GetBytecodeSize()
{
	return m_shaderBlobSize;
}

unsigned Shader::GetContantBufferCount()
{
	return (unsigned) m_vConstantBuffers.size();
}

ConstantBuffer& Shader::GetConstantBuffer(unsigned uIdx)
{
	return m_vConstantBuffers[uIdx];
}


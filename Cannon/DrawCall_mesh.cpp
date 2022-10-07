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
// Mesh
//

Mesh::Mesh(MeshType type)
	: m_drawStyle(DS_TRILIST), m_d3dBuffersNeedUpdate(true), m_boundingBoxNeedsUpdate(true)
{
	if(type == MT_PLANE || type == MT_UIPLANE || type == MT_ZERO_ONE_PLANE_XY_NEGATIVE_Z_NORMAL)
		LoadPlane(type, 1.5, 0.85);
	else if(type == MT_BOX)
		LoadBox(1.0f, 1.0f, 1.0f);
	else if (type == MT_CYLINDER)
		LoadCylinder(0.5f, 1.0f);
	else if (type == MT_ROUNDEDBOX)
		LoadRoundedBox(1.0f, 1.0f, 1.0f);
	else if (type == MT_SPHERE)
		LoadSphere();
}

Mesh::Mesh(Mesh::Vertex* pVertices, unsigned vertexCount)
	: m_drawStyle(DS_TRILIST), m_d3dBuffersNeedUpdate(true), m_boundingBoxNeedsUpdate(true)
{
	UpdateVertices(pVertices, vertexCount);
}

Mesh::Mesh(Mesh::Vertex* pVertices, unsigned vertexCount, unsigned* pIndices, unsigned indexCount)
	: m_drawStyle(DS_TRILIST), m_d3dBuffersNeedUpdate(true), m_boundingBoxNeedsUpdate(true)
{
	UpdateVertices(pVertices, vertexCount, pIndices, indexCount);
}

Mesh::Mesh(string filename)
	: m_drawStyle(DS_TRILIST), m_d3dBuffersNeedUpdate(true), m_boundingBoxNeedsUpdate(true)
{
	if (!FileExists(filename))
		filename = string("Media/Meshes/") + filename;

	FILE* pFile = OpenFile(filename, "r");
	assert(pFile);
	if (!pFile)
		return;

	auto &vertices = GetVertices();
	vertices.clear();
	auto &indices = GetIndices();
	indices.clear();

	unsigned verticesPerPolygon = 0;
	vector<XMVECTOR> positions;
	vector<XMFLOAT2> texcoords;
	vector<XMVECTOR> normals;

	map<vector<unsigned>, unsigned> knownVertices;

	char rawLine[1024];
	stringstream line;
	while(feof(pFile) == 0)
	{
		line.clear();
		fgets(rawLine, 1024, pFile);
		line.str(rawLine);

		if(line.peek() == '#')
			continue;

		string word;
		line >> word;

		if(word == "v")
		{
			XMFLOAT4 position;
			line >> position.x >> position.y >> position.z;
			position.w = 1.0f;

			positions.push_back(XMLoadFloat4(&position));
		}

		if(word == "vn")
		{
			XMFLOAT4 normal;
			line >> normal.x >> normal.y >> normal.z;
			normal.w = 0.0f;

			normals.push_back(XMLoadFloat4(&normal));
		}

		if(word == "vt")
		{
			XMFLOAT2 texcoord;
			line >> texcoord.x >> texcoord.y;
			texcoord.y = 1-texcoord.y;	// Invert v because DX likes texcoords top-down

			texcoords.push_back(texcoord);
		}

		if(word == "f")
		{
			verticesPerPolygon = 0;

			for(;;)
			{
				line >> word;
				if (line.fail())
					break;

				word += "/";
				++verticesPerPolygon;

				size_t startIndex = 0;
				vector<unsigned> ptnSet { 0, 0, 0 };
				for (size_t i = 0; i < 3; ++i)
				{
					size_t endIndex = word.find_first_of('/', startIndex);
					if (endIndex != startIndex)
					{
						string value = word.substr(startIndex, endIndex - startIndex);
						ptnSet[i] = atoi(value.c_str());
					}
					startIndex = endIndex + 1;
				}
				
				auto iterator = knownVertices.find(ptnSet);
				if (iterator != knownVertices.end())
				{
					indices.push_back(iterator->second);
				}
				else
				{
					Mesh::Vertex vertex;
					memset(&vertex, 0, sizeof(vertex));
					if (ptnSet[0] != 0)
						vertex.position = positions[ptnSet[0] - 1];
					if (ptnSet[1] != 0)
						vertex.texcoord = texcoords[ptnSet[1] - 1];
					if (ptnSet[2] != 0)
						vertex.normal = normals[ptnSet[2] - 1];

					vertices.push_back(vertex);
					indices.push_back((unsigned) vertices.size() - 1);
					knownVertices[ptnSet] = (unsigned) vertices.size() - 1;
				}
			}
		}
	}

	fclose(pFile);

	assert(verticesPerPolygon == 3);	// Loader only handles triangle meshes

	// Invert the winding order to account for DX default (clockwise)
	for(size_t i = 0; i < indices.size(); i +=3)
	{
		unsigned temp = indices[i + 0];
		indices[i + 0] = indices[i + 2];
		indices[i + 2] = temp;
	}
}

void Mesh::LoadPlane(MeshType type, float width, float height)
{
	if (type != MT_PLANE && type != MT_UIPLANE && type != MT_ZERO_ONE_PLANE_XY_NEGATIVE_Z_NORMAL)
		return;

	float fWidth = width;
	float fHeight = height;

	float fLeft = -fWidth / 2;
	float fRight = fWidth / 2;
	float fBottom = -fHeight / 2;
	float fTop = fHeight / 2;

	auto& vertices = GetVertices();
	vertices.resize(4);
	auto& indices = GetIndices();
	indices.resize(6);

	if (type == MT_PLANE)
	{
#if 0
		vertices[0].position = XMVectorSet(fLeft, 0.0f, fBottom, 1.0f);
		vertices[1].position = XMVectorSet(fRight, 0.0f, fBottom, 1.0f);
		vertices[2].position = XMVectorSet(fLeft, 0.0f, fTop, 1.0f);
		vertices[3].position = XMVectorSet(fRight, 0.0f, fTop, 1.0f);
#else
		vertices[2].position = XMVectorSet(fLeft, fBottom, 0.0f, 1.0f);
		vertices[3].position = XMVectorSet(fRight, fBottom, 0.0f, 1.0f);
		vertices[0].position = XMVectorSet(fLeft, fTop, 0.0f, 1.0f);
		vertices[1].position = XMVectorSet(fRight, fTop, 0.0f, 1.0f);
#endif
		for (unsigned i = 0; i < 4; ++i)
			vertices[i].normal = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	}
	else if (type == MT_UIPLANE)
	{
		vertices[0].position = XMVectorSet(fLeft, fTop, 0.0f, 1.0f);
		vertices[1].position = XMVectorSet(fRight, fTop, 0.0f, 1.0f);
		vertices[2].position = XMVectorSet(fLeft, fBottom, 0.0f, 1.0f);
		vertices[3].position = XMVectorSet(fRight, fBottom, 0.0f, 1.0f);

		for (unsigned i = 0; i < 4; ++i)
			vertices[i].normal = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	}
else if (type == MT_ZERO_ONE_PLANE_XY_NEGATIVE_Z_NORMAL)
	{
		vertices[0].position = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f); 
		vertices[1].position = XMVectorSet(1.0f, 0.0f, 0.0f, 1.0f);
		vertices[2].position = XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
		vertices[3].position = XMVectorSet(1.0f, 1.0f, 0.0f, 1.0f);

		for (unsigned i = 0; i < 4; ++i)
			vertices[i].normal = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
	}
	else
	{
		std::abort(); // Error^M
	}



	vertices[0].texcoord.x = 0.0f; vertices[0].texcoord.y = 0.0f;
	vertices[1].texcoord.x = 1.0f; vertices[1].texcoord.y = 0.0f;
	vertices[2].texcoord.x = 0.0f; vertices[2].texcoord.y = 1.0f;
	vertices[3].texcoord.x = 1.0f; vertices[3].texcoord.y = 1.0f;

	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 2;
	indices[3] = 3;
	indices[4] = 2;
	indices[5] = 1;
};

void Mesh::LoadBox(const float width, const float height, const float depth)
{
	float fLeft = -width / 2;
	float fRight = width / 2;

	float fBottom = -height / 2;
	float fTop = height / 2;

	float fNear = depth / 2;
	float fFar = -depth / 2;

	XMFLOAT4 ltn, rtn, rbn, lbn;
	XMFLOAT4 ltf, rtf, rbf, lbf;

	ltn.x = lbn.x = ltf.x = lbf.x = fLeft;
	rtn.x = rbn.x = rtf.x = rbf.x = fRight;

	lbn.y = lbf.y = rbn.y = rbf.y = fBottom;
	ltn.y = rtn.y = ltf.y = rtf.y = fTop;

	ltn.z = rtn.z = rbn.z = lbn.z = fNear;
	ltf.z = rtf.z = rbf.z = lbf.z = fFar;

	ltn.w = rtn.w = rbn.w = lbn.w = 1.0f;
	ltf.w = rtf.w = rbf.w = lbf.w = 1.0f;

	XMFLOAT4 a, b, c, d;
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	memset(&c, 0, sizeof(c));
	memset(&d, 0, sizeof(d));

	XMFLOAT4 normal;

	XMFLOAT2 at = { 0.f, 0.f };
	XMFLOAT2 bt = { 1.f, 0.f };
	XMFLOAT2 ct = { 1.f, 1.f };
	XMFLOAT2 dt = { 0.f, 1.f };

	m_vertices.resize(6 * 2 * 3);
	for (size_t i = 0; i < 6; ++i)
	{
		memset(&normal, 0, sizeof(normal));

		if (i == 0)	//near
		{
			a = ltn;
			b = rtn;
			c = rbn;
			d = lbn;

			normal.z = 1.f;
		}
		if (i == 1)	//right
		{
			a = rtn;
			b = rtf;
			c = rbf;
			d = rbn;

			normal.x = 1.f;
		}
		if (i == 2)	//far
		{
			a = rtf;
			b = ltf;
			c = lbf;
			d = rbf;

			normal.z = -1.f;
		}
		if (i == 3)	//left
		{
			a = ltf;
			b = ltn;
			c = lbn;
			d = lbf;

			normal.x = -1.f;
		}
		if (i == 4)	//top
		{
			a = ltf;
			b = rtf;
			c = rtn;
			d = ltn;

			normal.y = 1.f;
		}
		if (i == 5)	//bottom
		{
			a = lbn;
			b = rbn;
			c = rbf;
			d = lbf;

			normal.y = -1.f;
		}

		m_vertices[i * 6 + 0].position = XMLoadFloat4(&a);
		m_vertices[i * 6 + 1].position = XMLoadFloat4(&b);
		m_vertices[i * 6 + 2].position = XMLoadFloat4(&d);

		m_vertices[i * 6 + 3].position = XMLoadFloat4(&b);
		m_vertices[i * 6 + 4].position = XMLoadFloat4(&c);
		m_vertices[i * 6 + 5].position = XMLoadFloat4(&d);

		for (unsigned j = 0; j < 6; ++j)
			m_vertices[i * 6 + j].normal = XMLoadFloat4(&normal);

		m_vertices[i * 6 + 0].texcoord = at;
		m_vertices[i * 6 + 1].texcoord = bt;
		m_vertices[i * 6 + 2].texcoord = dt;
		m_vertices[i * 6 + 3].texcoord = bt;
		m_vertices[i * 6 + 4].texcoord = ct;
		m_vertices[i * 6 + 5].texcoord = dt;
	}

	m_indices.clear();
	for (unsigned i = 0; i < m_vertices.size(); ++i)
		m_indices.push_back(i);

	m_d3dBuffersNeedUpdate = true;
}

void Mesh::LoadCylinder(const float radius, const float height)
{
	const XMVECTOR center = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	const XMVECTOR upDir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const XMVECTOR rightDir = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	const unsigned segmentCount = 32;

	vector<Disc> discs;
	discs.push_back({ center - upDir * height / 2.0f, upDir, rightDir, 0.0f, 0.0f });
	discs.push_back({ center - upDir * height / 2.0f, upDir, rightDir, radius, radius });
	discs.push_back({ center + upDir * height / 2.0f, upDir, rightDir, radius, radius });
	discs.push_back({ center + upDir * height / 2.0f, upDir, rightDir, 0.0f, 0.0f });

	m_vertices.clear();
	m_indices.clear();
	AppendGeometryForDiscs(discs, segmentCount);
	GenerateSmoothNormals();
}

void Mesh::LoadRoundedBox(const float width, const float height, const float depth)
{
	const XMVECTOR center = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	const XMVECTOR upDir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const XMVECTOR rightDir = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	const unsigned segmentCount = 32;

	vector<Disc> discs;
	discs.push_back({ center - upDir * height / 2.0f, upDir, rightDir, 0.0f, 0.0f });
	discs.push_back({ center - upDir * height / 2.0f, upDir, rightDir, width / 2.0f, depth / 2.0f });
	discs.push_back({ center + upDir * height / 2.0f, upDir, rightDir, width / 2.0f, depth / 2.0f });
	discs.push_back({ center + upDir * height / 2.0f, upDir, rightDir, 0.0f, 0.0f });

	m_vertices.clear();
	m_indices.clear();
	AppendGeometryForDiscs(discs, segmentCount, DiscMode::RoundedSquare);
	GenerateSmoothNormals();
}

void Mesh::LoadSphere()
{
	const XMVECTOR center = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	const float radius = 0.5f;
	const unsigned segmentCount = 24;//32;

	XMVECTOR yAxis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR zAxis = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

	m_vertices.clear();
	for (unsigned discIndex = 0; discIndex <= segmentCount; ++discIndex)
	{
		float pitchAngle = XM_PI * ((float)discIndex / (float)segmentCount);
		XMVECTOR discStartPosition = XMVector3Transform(-yAxis * radius, XMMatrixRotationAxis(zAxis, pitchAngle));
		for (unsigned segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
		{
			float yawAngle = XM_PI * 2.0f * ((float)segmentIndex / (float)(segmentCount-1));

			Mesh::Vertex v;
			v.position = XMVector3Transform(discStartPosition, XMMatrixRotationAxis(yAxis, yawAngle));
			v.normal = XMVector3Normalize(v.position);
			v.texcoord.x = (float)segmentIndex / (float)(segmentCount-1);
			v.texcoord.y = 1.0f - ((float)discIndex / (float)segmentCount);
			m_vertices.push_back(v);

			if (discIndex == 0 || discIndex == segmentCount)
				break;
		}
	}

	m_indices.clear();
	for (unsigned discIndex = 0; discIndex <= segmentCount; ++discIndex)
	{
		unsigned geometryLoopIndex = (discIndex > 0) ? discIndex - 1 : discIndex;
		for (unsigned segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
		{
			unsigned bottomLeft = 1 + geometryLoopIndex * segmentCount + segmentIndex;
			unsigned bottomRight = 1 + geometryLoopIndex * segmentCount + (segmentIndex + 1) % segmentCount;
			unsigned topLeft = bottomLeft + segmentCount;
			unsigned topRight = bottomRight + segmentCount;

			if (discIndex == 0)
			{
				m_indices.push_back(0);
				m_indices.push_back(bottomLeft);
				m_indices.push_back(bottomRight);
			}
			else if (discIndex == segmentCount - 1)
			{
				m_indices.push_back(bottomRight);
				m_indices.push_back(bottomLeft);
				m_indices.push_back((unsigned)m_vertices.size() - 1);
			}
			else
			{
				m_indices.push_back(bottomLeft);
				m_indices.push_back(topLeft);
				m_indices.push_back(topRight);

				m_indices.push_back(topRight);
				m_indices.push_back(bottomRight);
				m_indices.push_back(bottomLeft);
			}
		}
	}

	m_d3dBuffersNeedUpdate = true;
}

void AppendVerticesForCircleDisc(std::vector<Mesh::Vertex>& vertices, const Mesh::Disc& disc, const unsigned segmentCount)
{
	Mesh::Vertex v;
	memset(&v, 0, sizeof(v));

	XMVECTOR discEdgeStartPosition = disc.rightDir * disc.radiusRight;
	for (unsigned discEdgeIndex = 0; discEdgeIndex < segmentCount; ++discEdgeIndex)
	{
		float yawAngle = XM_PI * 2.0f * ((float)discEdgeIndex / (float)segmentCount);

		XMVECTOR localPosition = XMVector3Transform(discEdgeStartPosition, XMMatrixRotationAxis(disc.upDir, yawAngle));
		v.position = XMVectorSetW(disc.center + localPosition, 1.0f);
		vertices.push_back(v);
	}
}

void AppendVerticesForSquareDisc(std::vector<Mesh::Vertex>& vertices, const Mesh::Disc& disc, const unsigned segmentCount)
{
	const float curveRadius = disc.radiusRight * 0.2f;

	Mesh::Vertex v;
	memset(&v, 0, sizeof(v));

	XMVECTOR backDir = XMVector3Cross(disc.rightDir, disc.upDir);

	// Start in bottom right corner
	XMVECTOR centerToCorner = backDir * disc.radiusBack + disc.rightDir * disc.radiusRight;
	XMMATRIX boxCornerStepRotation = XMMatrixRotationAxis(disc.upDir, XM_PIDIV2);

	for (unsigned cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
	{
		v.position = XMVectorSetW(disc.center + centerToCorner, 1.0f);
		v.texcoord.x = XMVectorGetX(disc.rightDir * XMVector3Dot(centerToCorner, disc.rightDir) / (disc.radiusRight * 2.0f) + 0.5f * disc.rightDir);
		v.texcoord.y = XMVectorGetZ(backDir * XMVector3Dot(centerToCorner, backDir) / (disc.radiusBack * 2.0f) + 0.5f * backDir);
		vertices.push_back(v);

		centerToCorner = XMVector3TransformNormal(centerToCorner, boxCornerStepRotation);
	}
}

void AppendVerticesForRoundedSquareDisc(std::vector<Mesh::Vertex>& vertices, const Mesh::Disc& disc, const unsigned segmentCount)
{
	float curveRadius = disc.radiusRight * 0.2f;

	// Curve radius can't be bigger than either disc radius 
	if (curveRadius > disc.radiusBack)
		curveRadius = disc.radiusBack;

	Mesh::Vertex v;
	memset(&v, 0, sizeof(v));

	XMVECTOR backDir = XMVector3Cross(disc.rightDir, disc.upDir);

	// Start in bottom right corner, and back it un such that edge of the rounding circle touches the edges of the box
	XMVECTOR centerToBottom = backDir * (disc.radiusBack - curveRadius);
	XMVECTOR centerToRight = disc.rightDir * (disc.radiusRight - curveRadius);
	XMVECTOR centerToCornerCircleCenters[4] = {
		centerToBottom + centerToRight,
		-centerToBottom + centerToRight,
		-centerToBottom + -centerToRight,
		centerToBottom + -centerToRight};

	XMVECTOR circleEdgeStartPosition = backDir * curveRadius;
	XMMATRIX boxCornerStepRotation = XMMatrixRotationAxis(disc.upDir, XM_PIDIV2);

	unsigned segmentsPerCorner = segmentCount / 4;
	for (unsigned cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
	{
		for (unsigned discEdgeIndex = 0; discEdgeIndex < segmentsPerCorner; ++discEdgeIndex)
		{
			float yawAngle = XM_PIDIV2 * ((float)discEdgeIndex / ((float)segmentsPerCorner));

			XMVECTOR localPosition = centerToCornerCircleCenters[cornerIndex] + XMVector3Transform(circleEdgeStartPosition, XMMatrixRotationAxis(disc.upDir, yawAngle));
			v.position = XMVectorSetW(disc.center + localPosition, 1.0f);
			v.texcoord.x = XMVectorGetX(disc.rightDir * XMVector3Dot(localPosition, disc.rightDir) / (disc.radiusRight * 2.0f) + 0.5f * disc.rightDir);
			v.texcoord.y = XMVectorGetZ(backDir * XMVector3Dot(localPosition, backDir) / (disc.radiusBack * 2.0f) + 0.5f * backDir);
			vertices.push_back(v);
		}

		circleEdgeStartPosition = XMVector3TransformNormal(circleEdgeStartPosition, boxCornerStepRotation);
	}
}

void AppendIndicesForCylinderBody(std::vector<unsigned>& indices, const unsigned startingVertexIndex, const unsigned geometryLoopCount, const unsigned segmentCount)
{
	for (unsigned loopIndex = 0; loopIndex < geometryLoopCount - 1; ++loopIndex)
	{
		for (unsigned segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
		{
			unsigned bottomLeft = startingVertexIndex + loopIndex * segmentCount + segmentIndex;
			unsigned bottomRight = startingVertexIndex + loopIndex * segmentCount + (segmentIndex + 1) % segmentCount;
			unsigned topLeft = bottomLeft + segmentCount;
			unsigned topRight = bottomRight + segmentCount;

			indices.push_back(bottomLeft);
			indices.push_back(topLeft);
			indices.push_back(topRight);

			indices.push_back(topRight);
			indices.push_back(bottomRight);
			indices.push_back(bottomLeft);
		}
	}
}

void AppendIndicesForCylinderBeginCap(std::vector<unsigned>& indices, const unsigned startingVertexIndex, const unsigned segmentCount)
{
	for (unsigned segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
	{
		unsigned topLeft = startingVertexIndex + 1 + segmentIndex;
		unsigned topRight = startingVertexIndex + 1 + (segmentIndex + 1) % segmentCount;

		indices.push_back(startingVertexIndex);
		indices.push_back(topLeft);
		indices.push_back(topRight);
	}
}

void AppendIndicesForCylinderEndCap(std::vector<unsigned>& indices, const unsigned startingVertexIndex, const unsigned segmentCount)
{
	unsigned endingVertexIndex = startingVertexIndex + segmentCount;
	for (unsigned segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
	{
		unsigned bottomLeft = startingVertexIndex + segmentIndex;
		unsigned bottomRight = startingVertexIndex + (segmentIndex + 1) % segmentCount;

		indices.push_back(bottomRight);
		indices.push_back(bottomLeft);
		indices.push_back(endingVertexIndex);
	}
}

enum class CapType
{
	None,		// No endcap
	Flat,		// Hard edge between endcap and body
	Smooth		// Smoothed edge between endcap and body
};

void Mesh::AppendGeometryForDiscs(const std::vector<Disc>& discs, unsigned segmentCount, const DiscMode discMode)
{
	if (discs.empty())
		return;

	auto AppendVerticesFunction = (discMode == DiscMode::Circle) ? AppendVerticesForCircleDisc : 
		(discMode == DiscMode::Square) ? AppendVerticesForSquareDisc : AppendVerticesForRoundedSquareDisc;

	Vertex v;
	memset(&v, 0, sizeof(v));
	unsigned startingVertexIndex = (unsigned)m_vertices.size();

	CapType beginCapType = CapType::None;
	if (discs.size() >= 2)
	{
		// Enable begin cap if the first point has 0 size
		if (discs[0].radiusRight == 0.0f && discs[0].radiusBack == 0.0f)
		{
			// If the first and second discs are at the same spot, make the begin cap flat
			if (XMVector3Equal(discs[0].center, discs[1].center))
				beginCapType = CapType::Flat;
			else
				beginCapType = CapType::Smooth;
		}
	}

	CapType endCapType = CapType::None;
	if ((beginCapType == CapType::None && discs.size() >= 2) || (beginCapType != CapType::None && discs.size() >= 4))
	{
		// Enable end cap if the last point has 0 size
		if (discs[discs.size() - 1].radiusRight == 0.0f && discs[discs.size() - 1].radiusBack == 0.0f)
		{
			// If the last two discs at the same spot, make the end cap flat
			if (XMVector3Equal(discs[discs.size() - 1].center, discs[discs.size() - 2].center))
				endCapType = CapType::Flat;
			else
				endCapType = CapType::Smooth;
		}
	}

	unsigned geometryLoopCount = 0;
	for (size_t discIndex = 0; discIndex < discs.size(); ++discIndex)
	{
		const Disc& disc = discs[discIndex];
		if (discIndex == 0 && beginCapType != CapType::None)
		{
			v.position = XMVectorSetW(disc.center, 1.0f);
			v.texcoord = { 0.5f, 0.5f };
			m_vertices.push_back(v);
		}
		else if (discIndex == discs.size() - 1 && endCapType != CapType::None)
		{
			v.position = XMVectorSetW(disc.center, 1.0f);
			v.texcoord = { 0.5f, 0.5f };
			m_vertices.push_back(v);
		}
		else
		{
			if ((beginCapType == CapType::Flat && discIndex == 1) ||
				(endCapType == CapType::Flat && discIndex == discs.size() - 2))
			{
				AppendVerticesFunction(m_vertices, discs[discIndex], segmentCount);
			}

			AppendVerticesFunction(m_vertices, discs[discIndex], segmentCount);
			++geometryLoopCount;
		}
	}

	unsigned bodyStartIndex = startingVertexIndex;
	if (beginCapType == CapType::Flat)
	{
		AppendIndicesForCylinderBeginCap(m_indices, startingVertexIndex, segmentCount);
		bodyStartIndex += (1 + segmentCount);
	}
	else if (beginCapType == CapType::Smooth)
	{
		AppendIndicesForCylinderBeginCap(m_indices, startingVertexIndex, segmentCount);
		bodyStartIndex += 1;
	}

	AppendIndicesForCylinderBody(m_indices, bodyStartIndex, geometryLoopCount, segmentCount);

	if (endCapType == CapType::Flat)
	{
		AppendIndicesForCylinderEndCap(m_indices, bodyStartIndex + geometryLoopCount * segmentCount, segmentCount);
	}
	else if (endCapType == CapType::Smooth)
	{
		AppendIndicesForCylinderEndCap(m_indices, bodyStartIndex + (geometryLoopCount - 1) * segmentCount, segmentCount);
	}

	m_d3dBuffersNeedUpdate = true;
}

/// <summary>
/// OBJファイルにメッシュを保存する
/// </summary>
/// <param name="filename">保存先OBJファイル名</param>
/// <returns></returns>
bool Mesh::SaveToFile(const std::string& filename)
{
	ofstream out(filename);
	if (!out.is_open())
		return false;

	for (auto& vertex : m_vertices)
		out << "v " << XMVectorGetX(vertex.position) << " " << XMVectorGetY(vertex.position) << " " << XMVectorGetZ(vertex.position) << "\n";
	for (auto& vertex : m_vertices)
		out << "vn " << XMVectorGetX(vertex.normal) << " " << XMVectorGetY(vertex.normal) << " " << XMVectorGetZ(vertex.normal) << "\n";
	
	// Invert winding order because Cannon is clockwise winding and obj is counter-clockwise
	for (size_t vertexIndexIndex = 0; vertexIndexIndex < m_indices.size(); vertexIndexIndex+=3)
		out << "f " << m_indices[vertexIndexIndex + 2] + 1 << "//" << m_indices[vertexIndexIndex + 2] + 1 << " " << m_indices[vertexIndexIndex + 1] + 1 << "//" << m_indices[vertexIndexIndex + 1] + 1 << " " << m_indices[vertexIndexIndex + 0] + 1 << "//" << m_indices[vertexIndexIndex + 0] + 1 << "\n";

	out.close();
	return true;
}

void Mesh::Clear()
{
	UpdateVertices(nullptr, 0, nullptr, 0);
}

void Mesh::BakeTransform(const DirectX::XMMATRIX& transform)
{
	m_boundingBoxNeedsUpdate = true;
	m_d3dBuffersNeedUpdate = true;

	for (auto& vertex : m_vertices)
	{
		vertex.position = XMVector3TransformCoord(vertex.position, transform);
		vertex.normal = XMVector3Normalize(XMVector3TransformNormal(vertex.normal, transform));
	}
}

void Mesh::GenerateSmoothNormals()
{
	m_d3dBuffersNeedUpdate = true;

	for (size_t index = 0; index < m_indices.size(); index += 3)
	{
		auto& a = m_vertices[m_indices[index + 0]];
		auto& b = m_vertices[m_indices[index + 1]];
		auto& c = m_vertices[m_indices[index + 2]];

		XMVECTOR normal = XMVector3Cross(a.position - b.position, c.position - b.position);
		a.normal += normal;
		b.normal += normal;
		c.normal += normal;
	}

	for (size_t vertexIndex = 0; vertexIndex < m_vertices.size(); ++vertexIndex)
	{
		Mesh::Vertex& vertex = m_vertices[vertexIndex];
		vertex.normal = XMVector3Normalize(m_vertices[vertexIndex].normal);
	}
}

/// <summary>
/// 与えられた頂点データで上書き更新する
/// </summary>
/// <param name="pVertices">頂点データ</param>
/// <param name="vertexCount">頂点データ数</param>
void Mesh::UpdateVertices(Vertex* pVertices, unsigned vertexCount)
{
	m_boundingBoxNeedsUpdate = true;
	m_d3dBuffersNeedUpdate = true;

	if (!pVertices)
	{
		m_vertices.clear();
		m_indices.clear();
		return;
	}

	m_vertices.resize(vertexCount);
	memcpy(m_vertices.data(), pVertices, vertexCount * sizeof(Vertex));

	m_indices.resize(vertexCount);
	for (unsigned i = 0; i < vertexCount; ++i)
		m_indices[i] = i;
}

void Mesh::UpdateVertices(Vertex* pVertices, unsigned vertexCount, unsigned* pIndices, unsigned indexCount)
{
	m_boundingBoxNeedsUpdate = true;
	m_d3dBuffersNeedUpdate = true;

	if (!pVertices || !pIndices)
	{
		m_vertices.clear();
		m_indices.clear();
		return;
	}

	m_vertices.resize(vertexCount);
	memcpy(m_vertices.data(), pVertices, vertexCount * sizeof(Vertex));

	m_indices.resize(indexCount);
	memcpy(m_indices.data(), pIndices, indexCount* sizeof(unsigned));
}

std::vector<Mesh::Vertex>& Mesh::GetVertices()
{
	m_d3dBuffersNeedUpdate = true;
	m_boundingBoxNeedsUpdate = true;

	return m_vertices;
}

std::vector<unsigned>& Mesh::GetIndices()
{
	m_d3dBuffersNeedUpdate = true;
	m_boundingBoxNeedsUpdate = true;

	return m_indices;
}

ID3D11Buffer* Mesh::GetVertexBuffer()
{
	if (m_d3dBuffersNeedUpdate)
		UpdateD3DBuffers();

	return m_d3dVertexBuffer.Get();
}

ID3D11Buffer* Mesh::GetIndexBuffer()
{
	if (m_d3dBuffersNeedUpdate)
		UpdateD3DBuffers();

	return m_d3dIndexBuffer.Get();
}

bool Mesh::BoundingBoxNode::TestRayIntersection(const vector<Vertex>& vertices, const XMVECTOR& rayOriginInWorldSpace, const XMVECTOR& rayDirectionInWorldSpace,
	const XMMATRIX& worldTransform, float &distance, XMVECTOR& normalInLocalSpace,
	float maxDistance, bool returnFurthest)
{
	//	XMVector3Transform()
	BoundingBox boundingBoxInWorldSpace;
	boundingBox.Transform(boundingBoxInWorldSpace, worldTransform);
	if (!boundingBoxInWorldSpace.Intersects(rayOriginInWorldSpace, XMVector3Normalize(rayDirectionInWorldSpace), distance))
		return false;

	bool hit = false;
	float closestDistance = FLT_MAX;
	float furthestDistance = -1.0f;
	XMVECTOR returnedNormal = XMVectorZero();

	if (!children.empty())
	{
		float currentDistance = 0.0f;
		XMVECTOR currentNormal = XMVectorZero();

		for (auto& node : children)
		{
			if (node.TestRayIntersection(vertices, rayOriginInWorldSpace, rayDirectionInWorldSpace, worldTransform, currentDistance, currentNormal, maxDistance, returnFurthest))
			{
				if (!returnFurthest
					&& currentDistance < closestDistance)
				{
					closestDistance = currentDistance;
					returnedNormal = currentNormal;
					hit = true;
				}

				if (returnFurthest
					&& currentDistance > furthestDistance
					&& currentDistance <= maxDistance)
				{
					furthestDistance = currentDistance;
					returnedNormal = currentNormal;
					hit = true;
				}
			}
		}
	}
	else
	{
		float currentDistance = 0.0f;
		XMVECTOR currentNormal = XMVectorZero();

		for (size_t i = 0; i < indicesContained.size(); i += 3)
		{
			XMVECTOR v1 = XMVector3Transform(vertices[indicesContained[i + 0]].position, worldTransform);
			XMVECTOR v2 = XMVector3Transform(vertices[indicesContained[i + 1]].position, worldTransform);
			XMVECTOR v3 = XMVector3Transform(vertices[indicesContained[i + 2]].position, worldTransform);

			if (TriangleTests::Intersects(rayOriginInWorldSpace, rayDirectionInWorldSpace, v1, v2, v3, currentDistance))
			{
				// Calculate normal and reject backfacing triangles (clockwise winding order)
				XMVECTOR ab = v2 - v1;
				XMVECTOR ac = v3 - v1;
				currentNormal = XMVector3Normalize(XMVector3Cross(ac, ab));

				if (XMVectorGetX(XMVector3Dot(XMVectorNegate(rayDirectionInWorldSpace), currentNormal)) < 0)
					continue;

				if (!returnFurthest
					&& currentDistance < closestDistance)
				{
					closestDistance = currentDistance;
					returnedNormal = currentNormal;
					hit = true;
				}

				if (returnFurthest
					&& currentDistance > furthestDistance
					&& currentDistance <= maxDistance)
				{
					furthestDistance = currentDistance;
					returnedNormal = currentNormal;
					hit = true;
				}
			}
		}
	}

	if (hit)
	{
		distance = returnFurthest ? furthestDistance : closestDistance;
		normalInLocalSpace = returnedNormal;
	}

	return hit;
}

bool Mesh::TestRayIntersection(const XMVECTOR& rayOriginInWorldSpace, const XMVECTOR& rayDirectionInWorldSpace, const XMMATRIX& worldTransform, float &distance, XMVECTOR &normal, float maxDistance, bool returnFurthest)
{
	if (IsEmpty())
		return false;

	if (m_boundingBoxNeedsUpdate)
		UpdateBoundingBox();

	if (m_drawStyle != Mesh::DS_TRILIST)
		return false;

	return m_boundingBoxNode.TestRayIntersection(m_vertices, rayOriginInWorldSpace, rayDirectionInWorldSpace, worldTransform, distance, normal, maxDistance, returnFurthest);
}

bool Mesh::TestRayIntersection(const XMVECTOR& rayOriginInWorldSpace, const XMVECTOR& rayDirectionInWorldSpace, const XMMATRIX& worldTransform, float& distance)
{
	XMVECTOR normal = XMVectorZero();
	return TestRayIntersection(rayOriginInWorldSpace, rayDirectionInWorldSpace, worldTransform, distance, normal);
}

bool Mesh::TestPointInside(const XMVECTOR& pointInWorldSpace, const XMMATRIX& worldTransform)
{
	if (IsEmpty())
		return false;

	if (m_boundingBoxNeedsUpdate)
		UpdateBoundingBox();

	BoundingOrientedBox orientedBoundingBox;
	BoundingOrientedBox::CreateFromBoundingBox(orientedBoundingBox, m_boundingBoxNode.boundingBox);	
	orientedBoundingBox.Transform(orientedBoundingBox, worldTransform);

	return orientedBoundingBox.Contains(pointInWorldSpace) == CONTAINS;
}

void Mesh::BoundingBoxNode::GenerateChildNodes(const vector<Mesh::Vertex>& vertices, const vector<unsigned>& indices, unsigned currentDepth)
{
	indicesContained.clear();
	if (currentDepth == 1)
	{
		indicesContained = indices;
	}
	else
	{
		for (size_t i = 0; i < indices.size(); i += 3)
		{
			unsigned indexA = indices[i + 0];
			unsigned indexB = indices[i + 1];
			unsigned indexC = indices[i + 2];

			if (boundingBox.Contains(vertices[indexA].position) == ContainmentType::CONTAINS ||
				boundingBox.Contains(vertices[indexB].position) == ContainmentType::CONTAINS ||
				boundingBox.Contains(vertices[indexC].position) == ContainmentType::CONTAINS)
			{
				indicesContained.push_back(indexA);
				indicesContained.push_back(indexB);
				indicesContained.push_back(indexC);
			}
		}
	}

	if(indices.size() / 3 > targetTriangleCount)
	{
		XMFLOAT3 childExtents;
		childExtents.x = boundingBox.Extents.x / 2.0f;
		childExtents.y = boundingBox.Extents.y / 2.0f;
		childExtents.z = boundingBox.Extents.z / 2.0f;

		float xSigns[8] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };
		float ySigns[8] = { 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
		float zSigns[8] = { 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f };

		children.resize(8);
		for (unsigned i = 0; i < 8; ++i)
		{
			auto& child = children[i];
			child.boundingBox.Center.x = boundingBox.Center.x + xSigns[i] * childExtents.x;
			child.boundingBox.Center.y = boundingBox.Center.y + ySigns[i] * childExtents.y;
			child.boundingBox.Center.z = boundingBox.Center.z + zSigns[i] * childExtents.z;
			child.boundingBox.Extents = childExtents;
			child.GenerateChildNodes(vertices, indicesContained, currentDepth + 1);
		}
	}
	else
	{
		children.clear();
	}
}

const BoundingBox& Mesh::GetBoundingBox()
{
	if (m_boundingBoxNeedsUpdate)
		UpdateBoundingBox();

	return m_boundingBoxNode.boundingBox;
}

void Mesh::UpdateBoundingBox()
{
	m_boundingBoxNeedsUpdate = false;

	if (IsEmpty())
	{
		m_boundingBoxNode.boundingBox = BoundingBox(XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 0.0f));
		m_boundingBoxNode.children.clear();
		return;
	}

	float minX, minY, minZ;
	minX = minY = minZ = FLT_MAX;
	float maxX, maxY, maxZ;
	maxX = maxY = maxZ = -FLT_MAX;

	for (unsigned i = 0; i < m_vertices.size(); ++i)
	{
		Vertex& vertex = m_vertices[i];
		float x = XMVectorGetX(vertex.position);
		float y = XMVectorGetY(vertex.position);
		float z = XMVectorGetZ(vertex.position);

		if (x < minX)
			minX = x;
		if (x > maxX)
			maxX = x;
		
		if (y < minY)
			minY = y;
		if (y > maxY)
			maxY = y;
		
		if (z < minZ)
			minZ = z;
		if (z > maxZ)
			maxZ = z;
	}

	m_boundingBoxNode.boundingBox.Extents.x = (maxX - minX) / 2.0f;
	m_boundingBoxNode.boundingBox.Extents.y = (maxY - minY) / 2.0f;
	m_boundingBoxNode.boundingBox.Extents.z = (maxZ - minZ) / 2.0f;

	m_boundingBoxNode.boundingBox.Center.x = minX + m_boundingBoxNode.boundingBox.Extents.x;
	m_boundingBoxNode.boundingBox.Center.y = minY + m_boundingBoxNode.boundingBox.Extents.y;
	m_boundingBoxNode.boundingBox.Center.z = minZ + m_boundingBoxNode.boundingBox.Extents.z;

	m_boundingBoxNode.GenerateChildNodes(m_vertices, m_indices, 1);
}

// Updates the vertex/index buffers if they already exists and is large enough, otherwise recreates them
void Mesh::UpdateD3DBuffers()
{
	m_d3dBuffersNeedUpdate = false;

	if (IsEmpty())
	{
		m_d3dVertexBuffer.Reset();
		m_d3dIndexBuffer.Reset();
		return;
	}

	if (m_d3dVertexBuffer && m_d3dVertexBufferDesc.ByteWidth / m_d3dVertexBufferDesc.StructureByteStride >= m_vertices.size())
	{
		g_d3dContext->UpdateSubresource(m_d3dVertexBuffer.Get(), 0, nullptr, m_vertices.data(), (UINT) m_vertices.size() * sizeof(Vertex), 1);
	}
	else
	{
		memset(&m_d3dVertexBufferDesc, 0, sizeof(D3D11_BUFFER_DESC));
		m_d3dVertexBufferDesc.ByteWidth = (UINT) m_vertices.size() * sizeof(Vertex);
		m_d3dVertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
		m_d3dVertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		m_d3dVertexBufferDesc.StructureByteStride = sizeof(Vertex);

		D3D11_SUBRESOURCE_DATA data;
		memset(&data, 0, sizeof(data));
		data.pSysMem = m_vertices.data();

		g_d3dDevice->CreateBuffer(&m_d3dVertexBufferDesc, &data, &m_d3dVertexBuffer);
		assert(m_d3dVertexBuffer);
	}

	if (m_d3dIndexBuffer && m_d3dIndexBufferDesc.ByteWidth / m_d3dIndexBufferDesc.StructureByteStride >= m_indices.size())
	{
		g_d3dContext->UpdateSubresource(m_d3dIndexBuffer.Get(), 0, nullptr, m_indices.data(), (UINT) m_indices.size() * sizeof(unsigned), 1);
	}
	else
	{
		memset(&m_d3dIndexBufferDesc, 0, sizeof(D3D11_BUFFER_DESC));
		m_d3dIndexBufferDesc.ByteWidth = (UINT) m_indices.size() * sizeof(unsigned);
		m_d3dIndexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
		m_d3dIndexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		m_d3dIndexBufferDesc.StructureByteStride = sizeof(unsigned);

		D3D11_SUBRESOURCE_DATA data;
		memset(&data, 0, sizeof(data));
		data.pSysMem = m_indices.data();

		g_d3dDevice->CreateBuffer(&m_d3dIndexBufferDesc, &data, &m_d3dIndexBuffer);
		assert(m_d3dIndexBuffer);
	}
}


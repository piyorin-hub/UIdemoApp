#include "pch.h"
#include "numbers"
#include "AppMain.h"
#include "sstream"

#include <iostream>
#define _USE_MATH_DEFINES
#include <math.h>

using namespace DirectX;
using namespace std;
using namespace winrt::Windows::Storage;

AppMain::AppMain() : m_textureTest("poly.jpg"), m_transformTest(XMMatrixIdentity())
{
	DrawCall::vAmbient = XMVectorSet(1.f, 1.f, 1.f, 1.f);
	DrawCall::vLights[0].vLightPosW = XMVectorSet(0.0f, 1.0f, 0.0f, 0.f);
	DrawCall::PushBackfaceCullingState(false);

	m_modelTest.LoadMesh("Lit_VS.cso", "LitTexture_PS.cso", std::make_shared<Mesh>("poly.obj"));

}

bool AppMain::Update()
{
	return false;
}

void AppMain::DrawObjects()
{
}

void AppMain::Render()
{
}

void AppMain::init()
{
	m_mixedReality.EnableMixedReality();
	m_mixedReality.EnableSurfaceMapping();
	m_mixedReality.EnableQRCodeTracking();
}
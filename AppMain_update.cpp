#include "pch.h"
#include "AppMain.h"

using namespace DirectX;
using namespace winrt::Windows::Storage;

bool AppMain::Update()
{
	m_mixedReality.Update();
	F_hands.UpdateFromMixedReality(m_mixedReality);

	float frameDelta = F_frameDeltaTimer.GetTime();
	if (frameDelta >= 3.6)
	{
		F_frameDeltaTimer.Reset();
	}
	float scale = 1.0f;

	auto showPos = XMMatrixTranslationFromVector(XMVectorSet(0.5f, 0.0f, 0.0f, 0.0f));
	m_modelTest.SetWorldTransform(XMMatrixMultiply(showPos, XMMatrixScaling(scale, scale, scale)));
	return false;
}

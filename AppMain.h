#pragma once

#include "pch.h"
#include "Cannon/MixedReality.h"
#include "Cannon/FloatingText.h"
#include "Cannon/Common/Timer.h"
#include "Cannon/DrawCall.h"
#include "Cannon/FloatingSlate.h"
#include "Cannon/FloatingText.h"
#include "Cannon/TrackedHands.h"

#define _ONLY_ONE

class AppMain //: public IFloatingSlateButtonCallback
{
public:

	AppMain();

	bool Update();

	void DrawObjects();
	void Render();

	void init();

private:
	MixedReality m_mixedReality;
#ifdef _MANY_MODEL
	DrawCall m_model[4];
	DrawCall ChosenOne;
	//Texture2D m_texture[4];
#endif // 0

#ifdef _ONLY_ONE
	Texture2D m_textureTest;
	DrawCall m_modelTest;
#endif

	DirectX::XMMATRIX m_transformTest;

	Timer F_frameDeltaTimer;
	TrackedHands F_hands;
	// UI
		//virtual void OnButtonPressed(FloatingSlateButton* pButton);
		//virtual void OnButtonUnPressed(FloatingSlateButton* pButton);

	enum ButtonID
	{
		MODEL_0,
		MODEL_1,
		MODEL_2,
		SHOW_MODEL
	};
	FloatingSlate F_menu;
	FloatingText F_textTest;
};
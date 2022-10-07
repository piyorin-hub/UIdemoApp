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

#pragma once

#include "DrawCall.h"

#include <DirectXMath.h>

#include <string>

class FloatingText
{
public:

	FloatingText();

	void SetText(const std::string& text);
	void SetPosition(const DirectX::XMVECTOR& position);
	void SetForwardDirection(const DirectX::XMVECTOR& forwardDirection);
	void SetUpDirection(const DirectX::XMVECTOR& upDirection);
	void SetSize(const DirectX::XMVECTOR& size);		// Physical size of the layout rect for this text in meters
	void SetFontSize(const float fontSize);
	void SetDPI(const float dpi);

	void SetHorizontalAlignment(const HorizontalAlignment horizontalAlignment);
	void SetVerticalAlignment(const VerticalAlignment verticalAlignment);

	void Render();

private:

	std::string m_text = "text";

	DirectX::XMVECTOR m_position = DirectX::XMVectorZero();
	DirectX::XMVECTOR m_forwardDirection = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	DirectX::XMVECTOR m_upDirection = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	DirectX::XMVECTOR m_size = DirectX::XMVectorSet(0.25f, 0.25f, 1.0f, 0.0f);
	float m_fontSize = DrawCall::DefaultFontSize;
	float m_dpi = 96.0f;

	HorizontalAlignment m_horizontalAlignment = HorizontalAlignment::Left;
	VerticalAlignment m_verticalAlignment = VerticalAlignment::Top;

	std::shared_ptr<Texture2D> m_texture;
	DrawCall m_drawCall;
	bool m_textureNeedsUpdate = true;

	void UpdateTexture();
};

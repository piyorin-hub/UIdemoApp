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


#include "Shared.hlsl"

cbuffer g_cbMatrices
{
	float4x4 mtxWorld;
	float4x4 mtxViewProj;
};

cbuffer g_cbLight
{
	float4 vLightPosV;
};

/*
struct InstancedVertex
{
	float4 position			: POSITION;
	float3 normal			: NORMAL;
	float2 texcoord			: TEXCOORD0;

	float4 worldMatrixRow0	: WORLDMATRIX_ROW0;
	float4 worldMatrixRow1	: WORLDMATRIX_ROW1;
	float4 worldMatrixRow2	: WORLDMATRIX_ROW2;
	float4 worldMatrixRow3	: WORLDMATRIX_ROW3;
	float4 color			: COLOR;

	uint   instId		: SV_InstanceID;
};
*/
UnlitRasterData main(InstancedVertex vertex)
{
	/*
	struct UnlitRasterData
	{
		float4 projectedPosition	: SV_POSITION;
		float4 color 				: TEXCOORD6;
		float2 texcoord				: TEXCOORD7;
	};	
	*/
	UnlitRasterData output = (UnlitRasterData)0;

	float4 worldPosition;
	float3 worldNormal;
	CalculateWorldPositionAndNormal(
		vertex,
		mtxWorld,
		worldPosition,
		worldNormal);
	output.projectedPosition = mul(worldPosition, mtxViewProj);

	output.color = vertex.color;
	output.texcoord = vertex.texcoord;

	return output;
}

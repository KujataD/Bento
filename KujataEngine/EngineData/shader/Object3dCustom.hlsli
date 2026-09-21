#ifndef KUJATA_OBJECT3D_CUSTOM_HLSLI
#define KUJATA_OBJECT3D_CUSTOM_HLSLI

// マテリアルで選ぶ自作シェーダー(Data 配下の .hlsl)が最初に include するファイル。
//
// 自作シェーダーは 1 ファイルに次の 2 つを書く:
//   PixelShaderOutput PSMain(VertexShaderOutput input)   … 必須
//   VertexShaderOutput VSMain(VertexShaderInput input)    … 省略可(省略するとエンジン標準の頂点処理)
// 同じファイルを頂点用・ピクセル用に 2 回コンパイルするので、片方でしか使えないものは
// KUJATA_VERTEX_SHADER / KUJATA_PIXEL_SHADER の #ifdef で分ける(下の include がその例)。
//
// 使えるもの:
//   頂点・ピクセル共通: gShaderParams.params[0..3](マテリアルの Shader Params)、gShaderParams.time(秒)
//   頂点: gTranformationMatrix(WVP / World / WorldInverseTranspose)、DefaultVertex(input)
//   ピクセル: gMaterial(色・発光・トゥーンの段など)、gTexture / gEmissiveTexture、SampleTexture(tex, uv)、
//            gDirectionalLight(影の色 shadowColor を含む)、gCamera、pointLights / spotLights、ToonStep、FlatNormal
#include "Object3d.hlsli"

#ifdef KUJATA_VERTEX_SHADER
#include "Object3dVertex.hlsli"
#endif

#ifdef KUJATA_PIXEL_SHADER
#include "Object3dPixel.hlsli"
#endif

#endif // KUJATA_OBJECT3D_CUSTOM_HLSLI

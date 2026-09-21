#ifndef KUJATA_OBJECT3D_VERTEX_HLSLI
#define KUJATA_OBJECT3D_VERTEX_HLSLI

// Object3d の頂点シェーダーが使う定数バッファと、標準の頂点処理。
// エンジン標準(Object3d.VS.hlsl)と、マテリアルで選ぶ自作シェーダー(Object3dCustom.hlsli 経由)の両方が読む。
#include "Object3d.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4x4 WorldInverseTranspose;
};
ConstantBuffer<TransformationMatrix> gTranformationMatrix : register(b0);

// 標準の頂点処理(ワールド・スクリーンへの変換)。自作シェーダーは頂点を動かしてからこれを呼べばよい。
VertexShaderOutput DefaultVertex(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTranformationMatrix.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3)gTranformationMatrix.WorldInverseTranspose));
    output.worldPosition = mul(input.position, gTranformationMatrix.World).xyz;
    return output;
}

#endif // KUJATA_OBJECT3D_VERTEX_HLSLI

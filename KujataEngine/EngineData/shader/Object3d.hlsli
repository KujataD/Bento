#ifndef KUJATA_OBJECT3D_HLSLI
#define KUJATA_OBJECT3D_HLSLI

struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t3 worldPosition : POSITION0;
};

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float32_t intensity;
    // 影の色(世界共通)。C++側 3d/DirectionalLight.h の DirectionalLightData と並びを一致させること。
    float32_t3 shadowColor;
};

struct Camera
{
    float32_t3 worldPosition;
};

// マテリアルごとの自由なパラメータ(自作シェーダー用)と時間。頂点・ピクセルの両方から読める。
// C++側 3d/Model.h の ShaderParamsData と並びを一致させること。
struct ShaderParams
{
    float32_t4 params[4]; // マテリアルの Shader Params(意味は各シェーダーが決める)
    float32_t time;       // 起動からの秒数(見た目用。一時停止や時間スケールでは止まらない)
    float32_t curveLength; // SplineRendererComponent の曲線の全長(それ以外は0)。u をこれで割ると根元0〜先端1
};
ConstantBuffer<ShaderParams> gShaderParams : register(b5);

#endif // KUJATA_OBJECT3D_HLSLI

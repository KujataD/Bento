#include "PostEffect.hlsli"

FullscreenVSOutput main(uint32_t vertexId : SV_VertexID)
{
    FullscreenVSOutput output;
    output.texcoord = float32_t2(float32_t((vertexId << 1) & 2), float32_t(vertexId & 2));
    output.position = float32_t4(output.texcoord * float32_t2(2.0f, -2.0f) + float32_t2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

#pragma once

#include "../base/DirectXCommon.h"
#include "../runtime/KujataApi.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include <array>

namespace KujataEngine {

static const uint32_t kMaxSpotLight = 16;

struct SpotLightData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // !< ライトの色
	Vector3 position = {0.0f, 0.0f, 0.0f};    // !< ライトの位置
	float intensity = 0.0f;                   // !< 輝度
	Vector3 direction = {0.0f, 0.0f, 1.0f};   // !< スポットライトの方向
	float distance = 0.0f;                    // !< ライトの届く最大距離
	float decay = 1.0f;                       // !< 減衰率
	float cosAngle = 0.0f;                    // スポットライトの余弦
	float cosFalloffStart = 1.0f;
	float padding = 0.0f;
};

// cbuffer内の配列は要素が16バイト境界に並ぶため、1要素をちょうど64バイト(=16×4)にしておく。
// ここがズレると2要素目以降が丸ごと化けるので、HLSL側のSpotLight構造体と必ず揃えること
// (shader/Object3d.PS.hlsl と shader/Fog.PS.hlsl)。
static_assert(sizeof(SpotLightData) == 64, "SpotLightData must be 64 bytes to match the HLSL cbuffer array stride.");

struct SpotLightForGPU {
	std::array<SpotLightData, kMaxSpotLight> lights;
	int32_t count = 0;
};

class KUJATA_API SpotLight {
public:
	static SpotLight* GetInstance();

	void Initialize();
	void Reset();

	void AddLight(const SpotLightData& light);
	void SetLight(uint32_t index, const SpotLightData& light);

	ID3D12Resource* GetResource() const { return resource_.Get(); }
	const SpotLightForGPU& GetData() const { return *map_; }

private:
	SpotLight() = default;
	~SpotLight() = default;
	SpotLight(const SpotLight&) = delete;
	SpotLight& operator=(const SpotLight&) = delete;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
	SpotLightForGPU* map_ = nullptr;
};

} // namespace KujataEngine

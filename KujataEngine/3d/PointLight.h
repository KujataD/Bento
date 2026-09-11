#pragma once

#include "../base/DirectXCommon.h"
#include "../runtime/KujataApi.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include <array>

namespace KujataEngine {

static const uint32_t kMaxPointLight = 16;

struct PointLightData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // !< ライトの色
	Vector3 position = {0.0f, 0.0f, 0.0f};    // !< ライトの位置
	float intensity = 1.0f;                   // !< 輝度
	float radius = 10.0f;                      // !< ライトの届く最大距離
	float decay = 1.0f;                       // !< 減衰率
	float padding[2];
};

struct PointLightForGPU {
	std::array<PointLightData, kMaxPointLight> lights;
	int32_t count = 0;
};

class KUJATA_API PointLight {
public:
	static PointLight* GetInstance();

	void Initialize();
	void Reset();

	void AddLight(const PointLightData& light);
	void SetLight(uint32_t index, const PointLightData& light);

	ID3D12Resource* GetResource() const { return resource_.Get(); }
	const PointLightForGPU& GetData() const { return *map_; }

private:
	PointLight() = default;
	~PointLight() = default;
	PointLight(const PointLight&) = delete;
	PointLight& operator=(const PointLight&) = delete;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
	PointLightForGPU* map_ = nullptr;
};

} // namespace KujataEngine

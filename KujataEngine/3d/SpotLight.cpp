#include "SpotLight.h"
#include <algorithm>

namespace KujataEngine {

SpotLight* SpotLight::GetInstance() {
	static SpotLight instance;
	return &instance;
}

void SpotLight::Initialize() {
	constexpr size_t kSpotLightBufferSize = (sizeof(SpotLightForGPU) + 0xff) & ~size_t(0xff);
	resource_ = DirectXCommon::GetInstance()->CreateBufferResource(kSpotLightBufferSize);
	resource_->Map(0, nullptr, reinterpret_cast<void**>(&map_));
	Reset();
}

void SpotLight::Reset() {
	if (!map_) {
		return;
	}

	// Hierarchyに存在しないSpotLightがGPU側へ残らないよう、毎フレーム空にしてから積み直す。
	map_->count = 0;
	for (SpotLightData& light : map_->lights) {
		light = SpotLightData{};
	}
}

void SpotLight::AddLight(const SpotLightData& light) {
	if (!map_) {
		return;
	}
	if (map_->count >= static_cast<int32_t>(kMaxSpotLight)) {
		return;
	}

	map_->lights[map_->count] = light;
	map_->count++;
}

void SpotLight::SetLight(uint32_t index, const SpotLightData& light) {
	if (!map_) {
		return;
	}
	if (index >= kMaxSpotLight) {
		return;
	}

	map_->lights[index] = light;

	if (map_->count <= static_cast<int32_t>(index)) {
		map_->count = static_cast<int32_t>(index + 1);
	}
}

} // namespace KujataEngine

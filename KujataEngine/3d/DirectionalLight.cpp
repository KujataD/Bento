#include "DirectionalLight.h"
#include "../base/DirectXCommon.h"
#include <cassert>

namespace KujataEngine {

DirectionalLight* DirectionalLight::GetInstance() {
	static DirectionalLight instance;
	return &instance;
}

void DirectionalLight::Initialize() {
	lightResource_ = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(DirectionalLightData));

	lightResource_->Map(0, nullptr, reinterpret_cast<void**>(&lightMap_));
	Reset();
}

void DirectionalLight::Reset() {
	if (!lightMap_) {
		return;
	}

	// DirectionalLightDataの既定値はintensityが1なので、消灯用の値を明示してGPU側へ書き込む。
	DirectionalLightData data{};
	data.intensity = 0.0f;
	*lightMap_ = data;
}

void DirectionalLight::Update() {
	// Mapしたままなので特に何もしなくて良い
	// ImGuiで GetData() を経由して値を変えれば即反映される
}

} // namespace KujataEngine

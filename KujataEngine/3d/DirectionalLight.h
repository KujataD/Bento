#pragma once
#include "../runtime/KujataApi.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include <d3d12.h>
#include <wrl.h>

namespace KujataEngine {

struct DirectionalLightData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
	Vector3 direction = {0.0f, -1.0f, 0.0f};
	float intensity = 1.0f;
};

class KUJATA_API DirectionalLight {
public:
	static DirectionalLight* GetInstance();

	void Initialize();
	void Reset();
	void Update(); // ImGuiでの編集後にGPUへ反映

	// Drawから呼ぶ用
	ID3D12Resource* GetResource() const { return lightResource_.Get(); }

	// ImGuiや外部から値を変える用
	DirectionalLightData& GetData() { return *lightMap_; }

private:
	DirectionalLight() = default;
	~DirectionalLight() = default;
	DirectionalLight(const DirectionalLight&) = delete;
	DirectionalLight& operator=(const DirectionalLight&) = delete;

	Microsoft::WRL::ComPtr<ID3D12Resource> lightResource_;
	DirectionalLightData* lightMap_ = nullptr;
};

} // namespace KujataEngine

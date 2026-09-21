#pragma once
#include "../runtime/KujataApi.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include <d3d12.h>
#include <wrl.h>

namespace KujataEngine {

// GPUの定数バッファにそのまま書く。HLSL側 Object3d.hlsli の DirectionalLight と並びを一致させること。
struct DirectionalLightData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
	Vector3 direction = {0.0f, -1.0f, 0.0f};
	float intensity = 1.0f;
	// 影の色(世界共通)。トゥーン(ShaderModel::kToon)のいちばん暗い段を、元の色×この色で塗る。
	// 紺などにすると影がやわらかく見える。ライトの当たり方とは無関係に、影の側の色だけを決める。
	Vector3 shadowColor = {0.22f, 0.24f, 0.42f};
	float padding = 0.0f;
};
static_assert(sizeof(DirectionalLightData) == 48, "HLSL の DirectionalLight と大きさがずれている");

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

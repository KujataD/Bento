#pragma once

#include "../postprocess/VolumeProfile.h"
#include "../scene/Component.h"

namespace KujataEngine {

class GameObject;
class Scene;

/// <summary>
/// ポストエフェクト設定をGameObjectとして配置するComponent(UnityのVolume相当)。
///
/// Global: シーン全体へ効く。Hierarchyに1つ置けばそのシーンのポストエフェクトになる。
/// Local : 同じGameObjectのColliderの内側だけで効く。blendDistanceで外側へフェードする。
///
/// 複数のVolumeはpriority昇順に重ねられ、エフェクト単位のoverrideフラグが立っている項目だけが
/// 上書きされる。解決はVolumeStackが毎フレーム行い、結果をPostProcessへ渡す。
/// </summary>
class KUJATA_API VolumeComponent : public Component {
public:
	const char* GetTypeName() const override { return "Volume"; }

	bool AllowMultiple() const override { return false; }

	void DrawInspector() override;

	void WriteJson(nlohmann::json& json) const override;

	void ReadJson(const nlohmann::json& json) override;

	bool IsGlobal() const { return isGlobal_; }
	void SetGlobal(bool isGlobal) { isGlobal_ = isGlobal; }

	/// 大きいほど後に重ねられ、強く反映される。
	float GetPriority() const { return priority_; }
	void SetPriority(float priority) { priority_ = priority; }

	/// このVolume全体の効き具合(0〜1)。Localではさらに距離減衰が掛かる。
	float GetWeight() const { return weight_; }
	void SetWeight(float weight) { weight_ = weight; }

	/// Local時、Colliderの外側でweightが0になるまでの距離。0で境界での即時切り替え。
	float GetBlendDistance() const { return blendDistance_; }
	void SetBlendDistance(float blendDistance) { blendDistance_ = blendDistance; }

	const VolumeProfileData& GetProfile() const { return profile_; }
	VolumeProfileData& GetProfile() { return profile_; }

private:
	bool isGlobal_ = true;
	float priority_ = 0.0f;
	float weight_ = 1.0f;
	float blendDistance_ = 0.0f;
	VolumeProfileData profile_;
};

/// <summary>
/// Global Volumeとしてすぐ使えるGameObjectを生成する。
/// Hierarchyの Create メニューとRenderingウィンドウのボタンで共用し、生成物が食い違わないようにする。
/// 生成直後から効果が見えるようTonemapだけoverrideを立てた状態で返す。
/// </summary>
KUJATA_API GameObject* CreateGlobalVolumeObject(Scene& scene);

} // namespace KujataEngine

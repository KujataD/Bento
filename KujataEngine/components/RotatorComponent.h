#pragma once

#include "../runtime/KujataApi.h"
#include "../scene/Component.h"

namespace KujataEngine {

/// <summary>
/// 所有GameObjectをY軸回転させ、任意でY軸上下(sin波)移動も行うComponent
/// </summary>
class KUJATA_API RotatorComponent : public Component {
public:
	const char* GetTypeName() const override { return "RotatorComponent"; }

	void Update() override;

	void SetSpeed(float speed) { speed_ = speed; }

	float GetSpeed() const { return speed_; }

	/// <summary>上下移動の距離(振れ幅)。0で上下移動なし。</summary>
	void SetBobDistance(float distance) { bobDistance_ = distance; }

	float GetBobDistance() const { return bobDistance_; }

	void SetBobSpeed(float speed) { bobSpeed_ = speed; }

	float GetBobSpeed() const { return bobSpeed_; }

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT(speed_, 0.001f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT(bobDistance_, 0.01f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT(bobSpeed_, 0.001f, 0.0f, 0.0f);
	}

	KUJATA_FIELD_FLOAT(speed_, 0.02f);
	// Y軸上下(sin波)の距離。0のままなら従来通り回転のみ(既存シーンの挙動を変えない)。
	KUJATA_FIELD_FLOAT(bobDistance_, 0.0f);
	KUJATA_FIELD_FLOAT(bobSpeed_, 0.02f);

	// 実行時状態(非シリアライズ): sin位相と前フレームまでに適用したオフセット。
	float bobPhase_ = 0.0f;
	float appliedBobOffset_ = 0.0f;
};

} // namespace KujataEngine

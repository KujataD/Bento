#pragma once

#include "../math/Vector3.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include "../scene/SerializedFieldRegistry.h"

namespace KujataEngine {

/// <summary>
/// 水弾1つぶんの状態。WaterSprayComponent が実行中に作る GameObject に付く(自分では動かない)。
/// 水弾を GameObject にしてあるのは、Hierarchy で選んで Inspector・CUI(object.get)で中身を見られるようにするため。
/// 位置は Transform、当たり判定の形は同じオブジェクトの SphereColliderComponent(選ぶとギズモで見える)。
/// </summary>
class KUJATA_API WaterDropComponent : public Component {
public:
	const char* GetTypeName() const override { return "WaterDropComponent"; }
	bool AllowMultiple() const override { return false; }

	const Vector3& GetVelocity() const { return velocity_; }
	void SetVelocity(const Vector3& velocity) { velocity_ = velocity; }
	float GetAge() const { return age_; }
	void SetAge(float age) { age_ = age; }

	/// <summary>このフレームで何かに当たったか(当たった水弾は WaterSprayComponent がしまう)。</summary>
	bool IsHit() const { return hit_; }
	void SetHit(bool hit) { hit_ = hit; }

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_VECTOR3_NAMED_TIP(velocity_, "Velocity", 0.1f, -1000.0f, 1000.0f, "今の速度[m/s]。重力で毎フレーム下向きに変わる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(age_, "Age", 0.01f, 0.0f, 100.0f, "撃たれてからの秒数。Lifetime を超えると消える。");
	}

	KUJATA_FIELD_VECTOR3(velocity_, (Vector3{0.0f, 0.0f, 0.0f}));
	KUJATA_FIELD_FLOAT(age_, 0.0f);
	bool hit_ = false;
};

} // namespace KujataEngine

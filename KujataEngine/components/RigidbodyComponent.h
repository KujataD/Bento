#pragma once

#include "../math/Vector3.h"
#include "../scene/Component.h"

namespace KujataEngine {

/// <summary>
/// 剛体反発(押し出し+速度反射)のためのRigidbody。形状を持つColliderとは分離する。
/// 速度はゲームコードがSetVelocityで与えるか、重力・反射で変化する。摩擦・力(AddForce)は未対応。
///
/// gravityScale: 重力の影響度(Unity Rigidbody2D準拠)。適用される重力 = グローバル重力(0,-9.81,0)×gravityScale。
///   1=通常、0=無重力、負値=反重力。キネマティック(Is Static)には適用されない。
/// Freeze Position X/Y/Z: そのワールド軸の移動を固定(速度成分を0にし、押し出しでも動かさない)。Unity Constraints準拠。
/// Freeze Rotation X/Y/Z: そのワールド軸の回転を固定(現状は角速度物理が無いため保持のみ・将来用)。
///
/// Is Static:
///   OFF(動的) = 通常の剛体。重力・衝突で押し出され、速度が反射される。
///   ON(キネマティック) = 無限質量。押し出されず速度も変化せず重力も受けないが、velocityで移動し
///     接触した動的ボディへ運動量を与える。
/// Rigidbodyを持たないCollider = 動かない純粋な静的壁(velocity 0、運動量なし)。
/// </summary>
/// <remarks>
/// ゲームDLL(GameModule)から GetComponent&lt;RigidbodyComponent&gt;() で参照するためKUJATA_APIでエクスポートする。
/// dynamic_castがDLL境界を跨ぐので、型情報をエンジン側に1つに揃える必要がある。
/// </remarks>
class KUJATA_API RigidbodyComponent : public Component {
public:
	const char* GetTypeName() const override { return "RigidbodyComponent"; }

	// 同一GameObjectに複数のRigidbodyは持てない。
	bool AllowMultiple() const override { return false; }

	const Vector3& GetVelocity() const { return velocity_; }
	void SetVelocity(const Vector3& velocity) { velocity_ = velocity; }

	// 反発係数[0,1]。0=完全非弾性、1=完全弾性。
	float GetBounciness() const { return bounciness_; }
	void SetBounciness(float bounciness) {
		if (bounciness < 0.0f) {
			bounciness = 0.0f;
		}
		if (bounciness > 1.0f) {
			bounciness = 1.0f;
		}
		bounciness_ = bounciness;
	}

	// 重力の影響度(Unity Rigidbody2D準拠)。
	float GetGravityScale() const { return gravityScale_; }
	void SetGravityScale(float gravityScale) { gravityScale_ = gravityScale; }

	// true=キネマティック(無限質量・押し出されない・重力を受けない)、false=動的。
	bool IsStatic() const { return isStatic_; }
	void SetStatic(bool isStatic) { isStatic_ = isStatic; }

	// Freeze Position(そのワールド軸の移動を固定)。
	bool FreezePositionX() const { return freezePositionX_; }
	bool FreezePositionY() const { return freezePositionY_; }
	bool FreezePositionZ() const { return freezePositionZ_; }

	// Freeze Rotation(そのワールド軸の回転を固定。現状は保持のみ)。
	bool FreezeRotationX() const { return freezeRotationX_; }
	bool FreezeRotationY() const { return freezeRotationY_; }
	bool FreezeRotationZ() const { return freezeRotationZ_; }

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_BOOL(isStatic_);
		KUJATA_REGISTER_FLOAT(gravityScale_, 0.01f, 0.0f, 0.0f);
		KUJATA_REGISTER_VECTOR3(velocity_, 0.01f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT(bounciness_, 0.01f, 0.0f, 1.0f);
		KUJATA_REGISTER_BOOL_AXES("Freeze Position", freezePositionX_, freezePositionY_, freezePositionZ_);
		KUJATA_REGISTER_BOOL_AXES("Freeze Rotation", freezeRotationX_, freezeRotationY_, freezeRotationZ_);
	}

	KUJATA_FIELD_BOOL(isStatic_, false);
	KUJATA_FIELD_FLOAT(gravityScale_, 1.0f);
	KUJATA_FIELD_VECTOR3(velocity_, {});
	KUJATA_FIELD_FLOAT(bounciness_, 0.5f);
	KUJATA_FIELD_BOOL(freezePositionX_, false);
	KUJATA_FIELD_BOOL(freezePositionY_, false);
	KUJATA_FIELD_BOOL(freezePositionZ_, false);
	KUJATA_FIELD_BOOL(freezeRotationX_, false);
	KUJATA_FIELD_BOOL(freezeRotationY_, false);
	KUJATA_FIELD_BOOL(freezeRotationZ_, false);
};

} // namespace KujataEngine

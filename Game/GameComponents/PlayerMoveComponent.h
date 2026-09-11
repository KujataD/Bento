#pragma once
#include <KujataEngine.h>
#include <math/Quaternion.h>

/// <summary>
/// エルデンリング風のプレイヤー移動。左スティック(+WASD)の入力をカメラのyaw基準の方向へ変換して移動し、
/// プレイヤーの向きは移動方向へQuaternionのRotateTowardsでスムーズに旋回する。
/// カメラ操作はOrbitCameraComponent(右スティック)側が担当する。
/// </summary>
class PlayerMoveComponent : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "PlayerMoveComponent"; }
	bool AllowMultiple() const override { return false; }

	void Update() override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED(moveSpeed_, "Move Speed", 0.05f, 0.0f, 100.0f);
		KUJATA_REGISTER_FLOAT_NAMED(turnSpeed_, "Turn Speed", 0.1f, 0.0f, 50.0f);
		KUJATA_REGISTER_STRING_NAMED(cameraName_, "Camera Name");
	}

	// 移動速度[unit/s]
	KUJATA_FIELD_FLOAT(moveSpeed_, 5.0f);
	// 旋回速度[rad/s]
	KUJATA_FIELD_FLOAT(turnSpeed_, 12.0f);
	// 移動基準にするカメラのGameObject名
	KUJATA_FIELD_STRING(cameraName_, "Main Camera");
};

#include "PlayerMoveComponent.h"

#include <scene/MovementUtil.h>

using namespace KujataEngine;

void PlayerMoveComponent::Update() {
	GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}

	// --- 入力(左スティック + WASD併用)。ローカルではz+が前 ---
	Vector2 stick = Input::GetLeftStick();
	Vector3 input = {stick.x, 0.0f, stick.y};
	if (Input::GetKey(DIK_W)) {
		input.z += 1.0f;
	}
	if (Input::GetKey(DIK_S)) {
		input.z -= 1.0f;
	}
	if (Input::GetKey(DIK_D)) {
		input.x += 1.0f;
	}
	if (Input::GetKey(DIK_A)) {
		input.x -= 1.0f;
	}

	// カメラ基準の移動 + 移動方向へのスムーズ旋回(エンジン標準関数)。
	MovementUtil::MoveAndFace(*owner, input, moveSpeed_, turnSpeed_, Time::GetDeltaTime(), cameraName_);
}

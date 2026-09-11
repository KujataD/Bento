#include "FollowCamera.h"
#include "KujataEngine.h"

namespace KujataEngine {

void FollowCamera::Initialize() {
	viewProjection_.Initialize();

	viewProjection_.rotation_ = { 0.12f, 0.0f, 0.0f };
	viewProjection_.translation_ = { 0.0f, 15.0f, -15.0f };

	viewProjection_.UpdateMatrix();
}

void FollowCamera::Update() {

	if (Input::IsControllerInput()) {
		float rotationSpeed = 0.02f;

		viewProjection_.rotation_.y += (float)Input::GetRightStick().x * rotationSpeed;
		viewProjection_.rotation_.x += (float)Input::GetRightStick().y * -rotationSpeed;
	}

	if (target_) {
		//追従対象からカメラまでのオフセット
		Vector3 offset = { 0.0f, 5.0f, -15.0f };

		// カメラの角度から回転行列を計算する。
		Matrix4x4 matRotate = MakeRotateMatrix(viewProjection_.rotation_);

		// オフセットをカメラの回転に合わせて回転させる
		offset = TransformNormal(offset, matRotate);

		//座標をコピーしてオフセット分ずらす
		viewProjection_.translation_ = target_->translation_ + offset;
	}

	// ビュー行列の更新と転送
	viewProjection_.UpdateMatrix();
}

} // namespace KujataEngine
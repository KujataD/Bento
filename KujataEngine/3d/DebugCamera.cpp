#include "DebugCamera.h"
#include "../input/Input.h"

namespace KujataEngine{


void DebugCamera::Initialize(Vector3 rotation, Vector3 translation) {
	rotation_ = rotation;
	translation_ = translation;
}

void DebugCamera::Update() {
	prevMousePos_ = mousePos_;
	mousePos_ = Input::GetMouseClientPos();

	if (Input::GetClick(1)) {
		Vector2 dPos = mousePos_ - prevMousePos_;

		rotation_.y += dPos.x * kRotateSpeed;
		rotation_.x += dPos.y * kRotateSpeed;
	}

	// 正面のベクトル
	Vector3 forward{sinf(rotation_.y), 0, cosf(rotation_.y)};

	// 正面から見た右のベクトル
	Vector3 right{forward.z, 0, -forward.x};

	if (Input::GetKey(DIK_W)) {
		translation_ += forward * kMoveSpeed;
	}
	if (Input::GetKey(DIK_S)) {
		translation_ -= forward * kMoveSpeed;
	}
	if (Input::GetKey(DIK_A)) {
		translation_ -= right * kMoveSpeed;
	}
	if (Input::GetKey(DIK_D)) {
		translation_ += right * kMoveSpeed;
	}
	if (Input::GetKey(DIK_Q)) {
		translation_.y += kMoveSpeed;
	}
	if (Input::GetKey(DIK_E)) {
		translation_.y -= kMoveSpeed;
	}


	// ビュー行列の更新
	UpdateViewMatrix();
}

void DebugCamera::UpdateViewMatrix() {
	// 回転行列と平行移動行列からワールド行列を計算する
	matRot_ = MakeRotateXMatrix(rotation_.x) * MakeRotateYMatrix(rotation_.y);
	Matrix4x4 cameraMatrix = matRot_ * MakeTranslateMatrix(translation_);

	// ワールド行列の逆行列
	matView_ = Inverse(cameraMatrix);
}
}

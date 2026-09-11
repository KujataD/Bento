#include "RailCameraController.h"
#include "KujataEngine.h"

namespace KujataEngine {

void RailCameraController::Initialize(Vector3 rotation, Vector3 position) {
	camera_.Initialize();

	// 引数項目を代入
	worldTransform_.Initialize();
	worldTransform_.translation_ = position;
	worldTransform_.rotation_ = rotation;
}

void RailCameraController::Update() {
	
	// 
#ifdef USE_IMGUI
	ImGui::Begin("Camera");
	ImGui::SliderFloat3("translation", &worldTransform_.translation_.x, -150.0f, 50.0f);
	ImGui::DragFloat3("rotation", &worldTransform_.rotation_.x, 0.001f);
	ImGui::End();
#endif // USE_IMGUI
	worldTransform_.UpdateMatrix(camera_);

	// カメラオブジェクトのワールド行列からビュー行列を計算する
	camera_.matView = Inverse(worldTransform_.matWorld_);
}

} // namespace KujataEngine
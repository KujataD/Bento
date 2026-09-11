#pragma once
#include "../runtime/KujataApi.h"
#include "Camera.h"
#include "WorldTransform.h"

namespace KujataEngine {

class KUJATA_API RailCameraController {
public:
	void Initialize(Vector3 rotation, Vector3 position);

	void Update();

	const Matrix4x4& GetViewMatrix() const { return camera_.matView; }
	const WorldTransform* GetWorldTransform() const { return &worldTransform_; }
	void SetPosition(const Vector3& position) { worldTransform_.translation_ = position; }
	void SetRotation(const Vector3& rotation) {
		if (abs(worldTransform_.rotation_.y - rotation.y) < std::numbers::pi_v<float>) {
			worldTransform_.rotation_ = rotation;
		}
	}

private:
	Camera camera_;

	WorldTransform worldTransform_;
};

} // namespace KujataEngine
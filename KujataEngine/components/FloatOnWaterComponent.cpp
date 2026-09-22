#include "FloatOnWaterComponent.h"

#include "../base/Time.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "OceanComponent.h"

#include <algorithm>
#include <cmath>

namespace KujataEngine {

void FloatOnWaterComponent::OnPlayStart() {
	onWater_ = false;
	if (GameObject* owner = GetOwner()) {
		baseRotation_ = owner->GetTransform().rotation_;
	}
}

void FloatOnWaterComponent::Update() {
	GameObject* owner = GetOwner();
	Scene* scene = owner ? owner->GetScene() : nullptr;
	onWater_ = false;
	if (!scene) {
		return;
	}
	owner->UpdateWorldTransformSelfAndAncestors();
	WorldTransform& transform = owner->GetTransform();
	const Vector3 position = transform.GetWorldPosition();

	float surface = 0.0f;
	if (!OceanComponent::TryGetSurfaceHeight(*scene, position.x, position.z, surface)) {
		return;
	}
	onWater_ = true;

	// 高さ: 海面 + オフセットへ(Follow Speed が 0 ならぴったり、それ以外は近づく割合を時間で決める)。
	const float target = surface + heightOffset_;
	float step = target - position.y;
	if (followSpeed_ > 0.0f) {
		step *= 1.0f - std::exp(-followSpeed_ * Time::GetDeltaTime());
	}
	transform.translation_.y += step;

	// 傾き: 前後・左右の海面の高さの差から、面の傾きを出す(X 軸まわり=前後、Z 軸まわり=左右)。
	if (tilt_ > 0.0f) {
		const float d = tiltSampleDistance_;
		float xPlus = surface, xMinus = surface, zPlus = surface, zMinus = surface;
		OceanComponent::TryGetSurfaceHeight(*scene, position.x + d, position.z, xPlus);
		OceanComponent::TryGetSurfaceHeight(*scene, position.x - d, position.z, xMinus);
		OceanComponent::TryGetSurfaceHeight(*scene, position.x, position.z + d, zPlus);
		OceanComponent::TryGetSurfaceHeight(*scene, position.x, position.z - d, zMinus);
		const float slopeX = std::atan2(xPlus - xMinus, 2.0f * d);
		const float slopeZ = std::atan2(zPlus - zMinus, 2.0f * d);
		// +X が上がっていれば Z 軸まわりに正(左回り)、+Z が上がっていれば X 軸まわりに負。
		transform.rotation_.x = baseRotation_.x - slopeZ * tilt_;
		transform.rotation_.z = baseRotation_.z + slopeX * tilt_;
	}
}

} // namespace KujataEngine

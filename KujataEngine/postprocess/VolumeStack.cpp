#include "VolumeStack.h"

#include <algorithm>
#include <cmath>

#include "../components/ColliderComponent.h"
#include "../components/VolumeComponent.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"

namespace KujataEngine {

namespace {

// Windows.hのmin/maxマクロと衝突するため、std::max/std::minは使わず自前で比較する。
float DistanceToSphereSurface(const Sphere& sphere, const Vector3& point) {
	Vector3 diff = point - sphere.center;
	float distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
	float outside = distance - sphere.radius;
	return outside > 0.0f ? outside : 0.0f;
}

// 1軸ぶんの範囲外へのはみ出し量(範囲内なら0)。
float AxisOverhang(float minValue, float maxValue, float value) {
	if (value < minValue) {
		return minValue - value;
	}
	if (value > maxValue) {
		return value - maxValue;
	}
	return 0.0f;
}

float DistanceToAABBSurface(const AABB& aabb, const Vector3& point) {
	// 各軸のはみ出し量を成分とするベクトルの長さが、最近接点までの距離になる(内側なら0)。
	float dx = AxisOverhang(aabb.min.x, aabb.max.x, point.x);
	float dy = AxisOverhang(aabb.min.y, aabb.max.y, point.y);
	float dz = AxisOverhang(aabb.min.z, aabb.max.z, point.z);
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

float VolumeStack::ComputeWeight(VolumeComponent& volume, const Vector3& cameraPosition) {
	float baseWeight = std::clamp(volume.GetWeight(), 0.0f, 1.0f);
	if (baseWeight <= 0.0f) {
		return 0.0f;
	}

	if (volume.IsGlobal()) {
		return baseWeight;
	}

	GameObject* owner = volume.GetOwner();
	if (!owner) {
		return 0.0f;
	}

	// Local Volumeの範囲は同じGameObjectのColliderで表す(Unityと同じ流儀)。
	ColliderComponent* collider = owner->GetComponent<ColliderComponent>();
	if (!collider) {
		return 0.0f;
	}

	// Sphereだけは球として正確に測り、それ以外(Box/Capsule)はワールドAABBで近似する。
	float distance = 0.0f;
	if (collider->GetShapeType() == ColliderShapeType::Sphere) {
		distance = DistanceToSphereSurface(collider->GetWorldSphere(), cameraPosition);
	} else {
		distance = DistanceToAABBSurface(collider->GetWorldAABB(), cameraPosition);
	}

	float blendDistance = volume.GetBlendDistance();
	if (blendDistance <= 0.0f) {
		// フェード無し。境界を跨いだ瞬間に切り替わる。
		return distance <= 0.0f ? baseWeight : 0.0f;
	}

	float fade = 1.0f - (distance / blendDistance);
	return baseWeight * std::clamp(fade, 0.0f, 1.0f);
}

VolumeResolveResult VolumeStack::Resolve(Scene& scene, const Vector3& cameraPosition) {
	VolumeResolveResult result{};

	// 実効weightが0より大きいVolumeだけを集める。
	struct Entry {
		VolumeComponent* volume;
		float weight;
	};
	std::vector<Entry> entries;

	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			if (!component || !component->IsEnabled()) {
				continue;
			}
			VolumeComponent* volume = dynamic_cast<VolumeComponent*>(component.get());
			if (!volume) {
				continue;
			}
			float weight = ComputeWeight(*volume, cameraPosition);
			if (weight > 0.0f) {
				entries.push_back({volume, weight});
			}
		}
	}

	// priorityが小さいものから順に重ねる = priorityが大きいVolumeほど後勝ちで強く出る。
	// stable_sortなので同priorityはシーン内の並び順が保たれる。
	std::stable_sort(entries.begin(), entries.end(), [](const Entry& lhs, const Entry& rhs) { return lhs.volume->GetPriority() < rhs.volume->GetPriority(); });

	for (const Entry& entry : entries) {
		BlendVolumeProfile(entry.volume->GetProfile(), entry.weight, result.profile);
		result.contributors.push_back(entry.volume);
	}

	return result;
}

} // namespace KujataEngine

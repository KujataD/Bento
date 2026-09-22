#include "PhysicsQuery.h"

#include "../components/ColliderComponent.h"
#include "../math/MathUtil.h"
#include "../shapes/ShapeUtil.h"
#include "GameObject.h"
#include "Scene.h"

#include <memory>

namespace KujataEngine {

namespace {

// 球を動かして当てる判定は、「相手の形を球の半径ぶん太らせて、中心の線分が当たるか」と同じ。
// 既存の RaycastSegment(線分と形の判定)をそのまま使うため、相手の形の方を太らせる。
bool CastAgainst(const ColliderComponent& collider, const Segment& segment, float radius, float& outT) {
	switch (collider.GetShapeType()) {
	case ColliderShapeType::Sphere: {
		Sphere sphere = collider.GetWorldSphere();
		sphere.radius += radius;
		return ShapeUtil::RaycastSegment(segment, sphere, outT);
	}
	case ColliderShapeType::Box: {
		// 箱は各面を半径ぶん外へ出す(本当は角が丸くなるが、角の近くで少し当たりやすくなるだけなので箱のまま)。
		OBB obb = static_cast<const BoxColliderComponent&>(collider).GetWorldOBB();
		obb.size = obb.size + Vector3{radius, radius, radius};
		return ShapeUtil::RaycastSegment(segment, obb, outT);
	}
	case ColliderShapeType::Capsule: {
		Capsule capsule = static_cast<const CapsuleColliderComponent&>(collider).GetWorldCapsule();
		capsule.radius += radius;
		return ShapeUtil::RaycastSegment(segment, capsule, outT);
	}
	}
	return false;
}

} // namespace

bool SphereCast(Scene& scene, const Vector3& from, const Vector3& to, float radius, SphereCastHit& outHit, const GameObject* ignore, bool includeTriggers) {
	const Segment segment{.origin = from, .diff = to - from};
	bool found = false;
	float nearest = 2.0f;

	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		if (ignore && (gameObject.get() == ignore || gameObject->IsDescendantOf(ignore))) {
			continue;
		}
		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			ColliderComponent* collider = component ? component->AsColliderComponent() : nullptr;
			if (!collider || !collider->IsEnabled() || (!includeTriggers && collider->IsTrigger())) {
				continue;
			}
			float t = 0.0f;
			if (CastAgainst(*collider, segment, radius, t) && t < nearest) {
				nearest = t;
				found = true;
				outHit.collider = collider;
				outHit.gameObject = gameObject.get();
				outHit.fraction = t;
				outHit.point = from + segment.diff * t;
			}
		}
	}
	return found;
}

} // namespace KujataEngine

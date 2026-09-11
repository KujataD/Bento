#include "RayCast.h"
#include "../3d/Camera.h"
#include "../3d/Model.h"
#include "../3d/WorldTransform.h"
#include "../math/MathUtil.h"
#include "Component.h"
#include "GameObject.h"
#include "IEditorBillboard.h"
#include "IRaycastTarget.h"
#include "Scene.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace KujataEngine {
namespace {

constexpr float kRayEpsilon = 0.000001f;

bool GetModelLocalBounds(const Model& model, Vector3& outMin, Vector3& outMax) {
	const std::vector<VertexData>& vertices = model.GetVertices();
	if (vertices.empty()) {
		return false;
	}

	outMin = {vertices[0].position.x, vertices[0].position.y, vertices[0].position.z};
	outMax = outMin;

	for (const VertexData& vertex : vertices) {
		outMin.x = (std::min)(outMin.x, vertex.position.x);
		outMin.y = (std::min)(outMin.y, vertex.position.y);
		outMin.z = (std::min)(outMin.z, vertex.position.z);
		outMax.x = (std::max)(outMax.x, vertex.position.x);
		outMax.y = (std::max)(outMax.y, vertex.position.y);
		outMax.z = (std::max)(outMax.z, vertex.position.z);
	}

	// 平面など厚みがないModelにもRayが当たりやすいよう、薄い軸だけ少し広げる。
	if (std::fabs(outMax.x - outMin.x) < 0.001f) {
		outMin.x -= 0.05f;
		outMax.x += 0.05f;
	}
	if (std::fabs(outMax.y - outMin.y) < 0.001f) {
		outMin.y -= 0.05f;
		outMax.y += 0.05f;
	}
	if (std::fabs(outMax.z - outMin.z) < 0.001f) {
		outMin.z -= 0.05f;
		outMax.z += 0.05f;
	}

	return true;
}

bool UpdateRayAabbSlab(float origin, float direction, float minValue, float maxValue, float& inOutNear, float& inOutFar) {
	if (std::fabs(direction) < kRayEpsilon) {
		if (origin < minValue) {
			return false;
		}
		if (origin > maxValue) {
			return false;
		}
		return true;
	}

	float nearValue = (minValue - origin) / direction;
	float farValue = (maxValue - origin) / direction;
	if (nearValue > farValue) {
		std::swap(nearValue, farValue);
	}

	inOutNear = (std::max)(inOutNear, nearValue);
	inOutFar = (std::min)(inOutFar, farValue);
	if (inOutNear > inOutFar) {
		return false;
	}

	return true;
}

bool FindPickableRenderer(GameObject& gameObject, Component*& outRenderer) {
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (!component || !component->IsEnabled()) {
			continue;
		}

		const IRaycastTarget* raycastTarget = dynamic_cast<const IRaycastTarget*>(component.get());
		if (!raycastTarget || !raycastTarget->GetRayCastModel()) {
			continue;
		}

		outRenderer = component.get();
		return true;
	}

	return false;
}

bool IntersectRaySphere(const Ray& ray, const Vector3& center, float radius, float& outDistance) {
	Vector3 offset = ray.origin - center;
	float b = Dot(offset, ray.diff);
	float c = Dot(offset, offset) - radius * radius;

	if (c > 0.0f && b > 0.0f) {
		return false;
	}

	float discriminant = b * b - c;
	if (discriminant < 0.0f) {
		return false;
	}

	outDistance = -b - std::sqrt(discriminant);
	if (outDistance < 0.0f) {
		outDistance = 0.0f;
	}

	return true;
}

void UpdateNearestHit(
    GameObject* gameObject,
    Component* component,
    const Vector3& point,
    float distance,
    GameObject*& nearestObject,
    Component*& nearestComponent,
    Vector3& nearestPoint,
    float& nearestDistance) {
	if (distance >= nearestDistance) {
		return;
	}

	nearestDistance = distance;
	nearestPoint = point;
	nearestObject = gameObject;
	nearestComponent = component;
}

bool TryCastModel(GameObject& gameObject, Component& renderer, const Ray& worldRay, Vector3& outPoint, float& outDistance) {
	const IRaycastTarget* raycastTarget = dynamic_cast<const IRaycastTarget*>(&renderer);
	const Model* model = raycastTarget ? raycastTarget->GetRayCastModel() : nullptr;
	if (!model) {
		return false;
	}

	Vector3 localMin{};
	Vector3 localMax{};
	if (!GetModelLocalBounds(*model, localMin, localMax)) {
		return false;
	}

	const WorldTransform& transform = gameObject.GetTransform();
	gameObject.UpdateWorldTransformSelfAndAncestors();
	Matrix4x4 worldMatrix = model->GetRootLocalMatrix() * transform.matWorld_;
	Matrix4x4 inverseWorld = Inverse(worldMatrix);

	Ray localRay{};
	localRay.origin = Transform(worldRay.origin, inverseWorld);
	localRay.diff = TransformNormal(worldRay.diff, inverseWorld);
	if (Length(localRay.diff) == 0.0f) {
		return false;
	}

	float hitDistance = 0.0f;
	if (!RayCast::IntersectRayAabb(localRay, localMin, localMax, hitDistance)) {
		return false;
	}

	Vector3 localHitPoint = localRay.origin + localRay.diff * hitDistance;
	outPoint = Transform(localHitPoint, worldMatrix);
	outDistance = Length(outPoint - worldRay.origin);
	return true;
}

bool CastInternal(const Scene& scene, const Ray& ray, bool includeEditorBillboards, RayCastHit& outHit) {
	Ray worldRay = ray;
	if (Length(worldRay.diff) == 0.0f) {
		outHit = {};
		return false;
	}
	worldRay.diff = Normalize(worldRay.diff);

	GameObject* nearestObject = nullptr;
	Component* nearestComponent = nullptr;
	Vector3 nearestPoint{};
	float nearestDistance = (std::numeric_limits<float>::max)();

	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}

		Component* renderer = nullptr;
		if (FindPickableRenderer(*gameObject, renderer)) {
			Vector3 hitPoint{};
			float distance = 0.0f;
			if (TryCastModel(*gameObject, *renderer, worldRay, hitPoint, distance)) {
				UpdateNearestHit(gameObject.get(), renderer, hitPoint, distance, nearestObject, nearestComponent, nearestPoint, nearestDistance);
			}
		}

		if (!includeEditorBillboards) {
			continue;
		}

		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			if (!component || !component->IsEnabled()) {
				continue;
			}
			const IEditorBillboard* billboard = dynamic_cast<const IEditorBillboard*>(component.get());
			if (!billboard) {
				continue;
			}

			float radius = billboard->GetEditorBillboardPickRadius();
			if (radius <= 0.0f) {
				continue;
			}

			gameObject->UpdateWorldTransformSelfAndAncestors();
			float distance = 0.0f;
			if (!IntersectRaySphere(worldRay, gameObject->GetTransform().GetWorldPosition(), radius, distance)) {
				continue;
			}
			if (distance <= kRayEpsilon) {
				// Rayの始点を包むEditorアイコンは、視点自身のCameraアイコンなどが常に最前面ヒットになるため除外する。
				continue;
			}

			Vector3 hitPoint = worldRay.origin + worldRay.diff * distance;
			UpdateNearestHit(gameObject.get(), component.get(), hitPoint, distance, nearestObject, nearestComponent, nearestPoint, nearestDistance);
		}
	}

	if (!nearestObject) {
		outHit = {};
		return false;
	}

	outHit.gameObject = nearestObject;
	outHit.renderer = nearestComponent;
	outHit.distance = nearestDistance;
	outHit.point = nearestPoint;
	return true;
}

} // namespace

bool RayCast::CreateRayFromViewportPoint(
    const Vector2& point,
    const Vector2& viewportPosition,
    const Vector2& viewportSize,
    const Camera& camera,
    Ray& outRay) {
	if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
		return false;
	}
	if (point.x < viewportPosition.x) {
		return false;
	}
	if (point.y < viewportPosition.y) {
		return false;
	}
	if (point.x > viewportPosition.x + viewportSize.x) {
		return false;
	}
	if (point.y > viewportPosition.y + viewportSize.y) {
		return false;
	}

	float localX = point.x - viewportPosition.x;
	float localY = point.y - viewportPosition.y;
	float ndcX = localX / viewportSize.x * 2.0f - 1.0f;
	float ndcY = 1.0f - localY / viewportSize.y * 2.0f;

	Matrix4x4 inverseViewProjection = Inverse(camera.matView * camera.matProjection);
	Vector3 nearPoint = Transform({ndcX, ndcY, 0.0f}, inverseViewProjection);
	Vector3 farPoint = Transform({ndcX, ndcY, 1.0f}, inverseViewProjection);

	outRay.origin = nearPoint;
	outRay.diff = Normalize(farPoint - nearPoint);
	if (Length(outRay.diff) == 0.0f) {
		return false;
	}

	return true;
}

bool RayCast::Cast(const Scene& scene, const Ray& ray, RayCastHit& outHit) {
	return CastInternal(scene, ray, false, outHit);
}

bool RayCast::CastForEditor(const Scene& scene, const Ray& ray, RayCastHit& outHit) {
	return CastInternal(scene, ray, true, outHit);
}

bool RayCast::IntersectRayAabb(const Ray& ray, const Vector3& minValue, const Vector3& maxValue, float& outDistance) {
	float nearDistance = 0.0f;
	float farDistance = (std::numeric_limits<float>::max)();

	if (!UpdateRayAabbSlab(ray.origin.x, ray.diff.x, minValue.x, maxValue.x, nearDistance, farDistance)) {
		return false;
	}
	if (!UpdateRayAabbSlab(ray.origin.y, ray.diff.y, minValue.y, maxValue.y, nearDistance, farDistance)) {
		return false;
	}
	if (!UpdateRayAabbSlab(ray.origin.z, ray.diff.z, minValue.z, maxValue.z, nearDistance, farDistance)) {
		return false;
	}

	outDistance = nearDistance;
	if (outDistance < 0.0f) {
		outDistance = farDistance;
	}
	if (outDistance < 0.0f) {
		return false;
	}

	return true;
}

} // namespace KujataEngine

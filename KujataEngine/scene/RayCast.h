#pragma once

#include "../runtime/KujataApi.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../shapes/ShapeUtil.h"

namespace KujataEngine {

class Camera;
class Component;
class GameObject;
class Scene;

struct RayCastHit {
	GameObject* gameObject = nullptr;
	Component* renderer = nullptr;
	Vector3 point = {};
	float distance = 0.0f;
};

/// <summary>
/// Scene内の描画ObjectへRayを飛ばす機能
/// </summary>
class KUJATA_API RayCast {
public:
	static bool CreateRayFromViewportPoint(
	    const Vector2& point,
	    const Vector2& viewportPosition,
	    const Vector2& viewportSize,
	    const Camera& camera,
	    Ray& outRay);

	/// <summary>
	/// Scene内のRayCast対象ComponentにRayを当てる
	/// </summary>
	static bool Cast(const Scene& scene, const Ray& ray, RayCastHit& outHit);

	/// <summary>
	/// Editor表示用のアイコン当たり判定も含めてRayを当てる。
	/// </summary>
	static bool CastForEditor(const Scene& scene, const Ray& ray, RayCastHit& outHit);

	static bool IntersectRayAabb(const Ray& ray, const Vector3& minValue, const Vector3& maxValue, float& outDistance);
};

} // namespace KujataEngine

#include "Scene.h"
#include "IEditorBillboard.h"
#include "ISceneCamera.h"
#include "../3d/Camera.h"
#include "../3d/DirectionalLight.h"
#include "../3d/LineRenderer.h"
#include "../3d/Model.h"
#include "../3d/WorldTransform.h"
#include "../2d/UICanvasRenderer.h"
#include "../base/DirectXCommon.h"
#include "../base/FrameProfiler.h"
#include "../base/ProjectPath.h"
#include "../base/Time.h"
#include "../input/Input.h"
#include "../runtime/UIEventBus.h"
#include "../runtime/UIInput.h"
#include "../runtime/PlayState.h"
#include "../runtime/SelectionProvider.h"
#include "../2d/Sprite2DRenderer.h"
#include "../components/ColliderComponent.h"
#include "../components/PointLightComponent.h"
#include "../components/RigidbodyComponent.h"
#include "../components/SpotLightComponent.h"
#include "../math/MathUtil.h"
#include "../components/ModelRendererComponent.h"
#include "../postprocess/PostProcess.h"
#include "../postprocess/VolumeStack.h"
#include "../shadow/ShadowMap.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace KujataEngine {

namespace {

constexpr float kEditorBillboardScale = 0.45f;
constexpr int32_t kColliderSphereSubdivision = 12;
constexpr float kColliderDebugPi = 3.14159265358979323846f;
constexpr Vector4 kColliderDebugColor = {0.1f, 1.0f, 0.35f, 1.0f};
constexpr Vector4 kCameraFrustumColor = {0.85f, 0.85f, 0.95f, 1.0f};
// ライトの影響範囲ギズモ(Unityと同じく黄色系)。内側コーンは減衰開始位置なので一段暗くする。
constexpr Vector4 kLightGizmoColor = {1.0f, 0.86f, 0.35f, 1.0f};
constexpr Vector4 kLightInnerConeColor = {0.75f, 0.62f, 0.22f, 1.0f};

// グローバル重力(Unity既定に合わせる)。RigidbodyのgravityScale倍で各動的ボディに適用する。
constexpr Vector3 kRigidbodyGravity = {0.0f, -9.81f, 0.0f};

// ゲーム中に全Colliderを可視化するデバッグモードのフラグ(Scene全体で共有)。
bool g_showAllColliders = false;

struct EditorBillboardDrawCache {
	std::unordered_map<std::string, std::unique_ptr<Model>> models;
	std::unordered_map<const Component*, std::unique_ptr<WorldTransform>> transforms;
};

EditorBillboardDrawCache& GetEditorBillboardDrawCache() {
	static EditorBillboardDrawCache cache;
	return cache;
}

enum class CollisionEventPhase {
	Enter,
	Stay,
	Exit,
};

std::string MakeCollisionPairKey(const ColliderComponent& colliderA, const ColliderComponent& colliderB) {
	uint64_t idA = colliderA.GetRuntimeId();
	uint64_t idB = colliderB.GetRuntimeId();
	if (idA > idB) {
		std::swap(idA, idB);
	}

	return std::to_string(idA) + ":" + std::to_string(idB);
}

// contact は colliderA→colliderB の接触情報(非トリガーで交差中のみ非null)。
// flipNormal=true のとき normal を反転して self 視点(self→other)に揃える。
void NotifyCollisionToComponents(GameObject* owner, ColliderComponent* self, ColliderComponent* other, bool isTrigger, CollisionEventPhase phase, const Contact* contact, bool flipNormal) {
	if (!owner || !self || !other) {
		return;
	}

	for (const std::unique_ptr<Component>& component : owner->GetComponents()) {
		if (!component || !component->IsEnabled()) {
			continue;
		}

		if (isTrigger) {
			if (phase == CollisionEventPhase::Enter) {
				component->OnTriggerEnter(other);
			} else if (phase == CollisionEventPhase::Stay) {
				component->OnTriggerStay(other);
			} else if (phase == CollisionEventPhase::Exit) {
				component->OnTriggerExit(other);
			}
			continue;
		}

		Collision collision{};
		collision.self = self;
		collision.other = other;
		collision.selfObject = owner;
		collision.otherObject = other->GetOwner();
		if (contact) {
			collision.normal = flipNormal ? contact->normal * -1.0f : contact->normal;
			collision.penetrationDepth = contact->depth;
			collision.point = contact->point;
		} else {
			collision.point = self->GetWorldCenter();
		}

		if (phase == CollisionEventPhase::Enter) {
			component->OnCollisionEnter(collision);
		} else if (phase == CollisionEventPhase::Stay) {
			component->OnCollisionStay(collision);
		} else if (phase == CollisionEventPhase::Exit) {
			component->OnCollisionExit(collision);
		}
	}
}

void NotifyCollisionPair(ColliderComponent* colliderA, ColliderComponent* colliderB, bool isTrigger, CollisionEventPhase phase, const Contact* contact = nullptr) {
	if (!colliderA || !colliderB) {
		return;
	}

	// contact は colliderA→colliderB 基準。B側へは normal を反転して渡す。
	NotifyCollisionToComponents(colliderA->GetOwner(), colliderA, colliderB, isTrigger, phase, contact, false);
	NotifyCollisionToComponents(colliderB->GetOwner(), colliderB, colliderA, isTrigger, phase, contact, true);
}

RigidbodyComponent* FindRigidbody(ColliderComponent* collider) {
	if (!collider) {
		return nullptr;
	}
	GameObject* owner = collider->GetOwner();
	if (!owner) {
		return nullptr;
	}
	return owner->GetComponent<RigidbodyComponent>();
}

// Rigidbodyの物理速度(無ければ0=純粋な静的壁)。
Vector3 PhysicsVelocity(RigidbodyComponent* rigidbody) {
	return rigidbody ? rigidbody->GetVelocity() : Vector3{0.0f, 0.0f, 0.0f};
}

// 押し出し・速度変化の対象になる「動的」ボディか(Rigidbody有 かつ Is Static でない)。
bool IsMovable(RigidbodyComponent* rigidbody) {
	return rigidbody && !rigidbody->IsStatic();
}

// Freeze Position が有効な軸の成分を0にする(移動・速度の凍結)。
Vector3 ApplyPositionFreeze(const RigidbodyComponent* rigidbody, Vector3 value) {
	if (rigidbody->FreezePositionX()) {
		value.x = 0.0f;
	}
	if (rigidbody->FreezePositionY()) {
		value.y = 0.0f;
	}
	if (rigidbody->FreezePositionZ()) {
		value.z = 0.0f;
	}
	return value;
}

// 非トリガーの交差ペアに剛体反発(押し出し+速度反射)を適用する。
// 動的(Rigidbody有・非Static)のみ押し出し/速度変化の対象。キネマティック(Is Static)と
// Rigidbody無しは無限質量として不動だが、キネマティックは自身の速度を相手へ与える。
// contact は colliderA→colliderB 基準(呼び出し側で計算済み)。
void ResolveCollisionResponse(ColliderComponent* colliderA, ColliderComponent* colliderB, const Contact& contact) {
	RigidbodyComponent* rigidbodyA = FindRigidbody(colliderA);
	RigidbodyComponent* rigidbodyB = FindRigidbody(colliderB);

	bool movableA = IsMovable(rigidbodyA);
	bool movableB = IsMovable(rigidbodyB);
	if (!movableA && !movableB) {
		return; // 両方とも不動(静的壁/キネマティック) → 応答なし
	}
	if (contact.depth <= 0.0f) {
		return;
	}

	const Vector3& normal = contact.normal; // A→B の分離方向

	// --- 位置補正(押し出し): 動的なAは-normal、動的なBは+normal。両dynamicは折半、片方のみ動的なら全量。---
	float moveA = 0.0f;
	float moveB = 0.0f;
	if (movableA && movableB) {
		moveA = contact.depth * 0.5f;
		moveB = contact.depth * 0.5f;
	} else if (movableA) {
		moveA = contact.depth;
	} else {
		moveB = contact.depth;
	}

	if (moveA > 0.0f) {
		WorldTransform& transformA = colliderA->GetOwner()->GetTransform();
		transformA.translation_ = transformA.translation_ + ApplyPositionFreeze(rigidbodyA, normal * -moveA);
	}
	if (moveB > 0.0f) {
		WorldTransform& transformB = colliderB->GetOwner()->GetTransform();
		transformB.translation_ = transformB.translation_ + ApplyPositionFreeze(rigidbodyB, normal * moveB);
	}

	// --- 速度反射(相対速度ベース): 動的ボディのみ、相手へ接近している場合に相対速度を反射する。---
	// 相手の物理速度(キネマティックの移動速度含む)を加え戻すことで運動量が伝わる。
	// 相手が静的(velocity 0)のときは従来の絶対速度反射に一致する。Freeze軸の速度は0に固定。
	Vector3 physVelocityA = PhysicsVelocity(rigidbodyA);
	Vector3 physVelocityB = PhysicsVelocity(rigidbodyB);

	if (movableA) {
		Vector3 relative = physVelocityA - physVelocityB;
		if (Dot(relative, normal) > 0.0f) { // AがBへ接近
			rigidbodyA->SetVelocity(ApplyPositionFreeze(rigidbodyA, physVelocityB + ShapeUtil::Reflect(relative, normal) * rigidbodyA->GetBounciness()));
		}
	}
	if (movableB) {
		Vector3 relative = physVelocityB - physVelocityA;
		if (Dot(relative, normal) < 0.0f) { // BがAへ接近
			rigidbodyB->SetVelocity(ApplyPositionFreeze(rigidbodyB, physVelocityA + ShapeUtil::Reflect(relative, normal) * rigidbodyB->GetBounciness()));
		}
	}
}

// Rigidbodyの速度を位置へ積分する。動的ボディには重力(gravityScale倍)を加え、Freeze Position軸を固定する。
// キネマティック(Is Static)は重力・拘束を受けず、自身の速度でそのまま移動する。
void IntegrateRigidbodies(Scene& scene, float deltaTime) {
	if (deltaTime <= 0.0f) {
		return;
	}
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		RigidbodyComponent* rigidbody = gameObject->GetComponent<RigidbodyComponent>();
		if (!rigidbody || !rigidbody->IsEnabled()) {
			continue;
		}
		WorldTransform& transform = gameObject->GetTransform();

		if (rigidbody->IsStatic()) {
			// キネマティック: 重力・拘束なしで自身の速度でそのまま移動。
			transform.translation_ = transform.translation_ + rigidbody->GetVelocity() * deltaTime;
			continue;
		}

		// 動的: 重力を加え、Freeze Position軸の速度を0に固定してから積分する。
		Vector3 velocity = rigidbody->GetVelocity() + kRigidbodyGravity * (rigidbody->GetGravityScale() * deltaTime);
		velocity = ApplyPositionFreeze(rigidbody, velocity);
		rigidbody->SetVelocity(velocity);
		transform.translation_ = transform.translation_ + velocity * deltaTime;
	}
}

void CollectSceneColliders(Scene& scene, std::vector<ColliderComponent*>& outColliders) {
	outColliders.clear();

	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}

		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			if (!component || !component->IsEnabled()) {
				continue;
			}

			ColliderComponent* collider = component->AsColliderComponent();
			if (!collider) {
				continue;
			}

			outColliders.push_back(collider);
		}
	}
}

void DrawAABB(const AABB& aabb, const Vector4& color) {
	Vector3 corners[8] = {
	    {aabb.min.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.max.y, aabb.min.z},
        {aabb.min.x, aabb.max.y, aabb.min.z},
	    {aabb.min.x, aabb.min.y, aabb.max.z},
        {aabb.max.x, aabb.min.y, aabb.max.z},
        {aabb.max.x, aabb.max.y, aabb.max.z},
        {aabb.min.x, aabb.max.y, aabb.max.z},
	};

	LineRenderer::DrawLine(corners[0], corners[1], color);
	LineRenderer::DrawLine(corners[1], corners[2], color);
	LineRenderer::DrawLine(corners[2], corners[3], color);
	LineRenderer::DrawLine(corners[3], corners[0], color);

	LineRenderer::DrawLine(corners[4], corners[5], color);
	LineRenderer::DrawLine(corners[5], corners[6], color);
	LineRenderer::DrawLine(corners[6], corners[7], color);
	LineRenderer::DrawLine(corners[7], corners[4], color);

	LineRenderer::DrawLine(corners[0], corners[4], color);
	LineRenderer::DrawLine(corners[1], corners[5], color);
	LineRenderer::DrawLine(corners[2], corners[6], color);
	LineRenderer::DrawLine(corners[3], corners[7], color);
}

void DrawOBB(const OBB& obb, const Vector4& color) {
	Vector3 axisX = obb.orientations[0] * obb.size.x;
	Vector3 axisY = obb.orientations[1] * obb.size.y;
	Vector3 axisZ = obb.orientations[2] * obb.size.z;
	Vector3 corners[8] = {
	    obb.center - axisX - axisY - axisZ, obb.center + axisX - axisY - axisZ, obb.center + axisX + axisY - axisZ, obb.center - axisX + axisY - axisZ,
	    obb.center - axisX - axisY + axisZ, obb.center + axisX - axisY + axisZ, obb.center + axisX + axisY + axisZ, obb.center - axisX + axisY + axisZ,
	};

	LineRenderer::DrawLine(corners[0], corners[1], color);
	LineRenderer::DrawLine(corners[1], corners[2], color);
	LineRenderer::DrawLine(corners[2], corners[3], color);
	LineRenderer::DrawLine(corners[3], corners[0], color);

	LineRenderer::DrawLine(corners[4], corners[5], color);
	LineRenderer::DrawLine(corners[5], corners[6], color);
	LineRenderer::DrawLine(corners[6], corners[7], color);
	LineRenderer::DrawLine(corners[7], corners[4], color);

	LineRenderer::DrawLine(corners[0], corners[4], color);
	LineRenderer::DrawLine(corners[1], corners[5], color);
	LineRenderer::DrawLine(corners[2], corners[6], color);
	LineRenderer::DrawLine(corners[3], corners[7], color);
}

void DrawSphere(const Sphere& sphere, const Vector4& color) {
	if (sphere.radius <= 0.0f) {
		return;
	}

	float lonEvery = 2.0f * kColliderDebugPi / static_cast<float>(kColliderSphereSubdivision);
	float latEvery = kColliderDebugPi / static_cast<float>(kColliderSphereSubdivision);

	for (int32_t latIndex = 0; latIndex < kColliderSphereSubdivision; ++latIndex) {
		float lat = -kColliderDebugPi / 2.0f + latEvery * static_cast<float>(latIndex);

		for (int32_t lonIndex = 0; lonIndex < kColliderSphereSubdivision; ++lonIndex) {
			float lon = lonEvery * static_cast<float>(lonIndex);

			Vector3 a{};
			Vector3 b{};
			Vector3 c{};

			a.x = sphere.radius * std::cos(lat) * std::cos(lon) + sphere.center.x;
			a.y = sphere.radius * std::sin(lat) + sphere.center.y;
			a.z = sphere.radius * std::cos(lat) * std::sin(lon) + sphere.center.z;

			b.x = sphere.radius * std::cos(lat + latEvery) * std::cos(lon) + sphere.center.x;
			b.y = sphere.radius * std::sin(lat + latEvery) + sphere.center.y;
			b.z = sphere.radius * std::cos(lat + latEvery) * std::sin(lon) + sphere.center.z;

			c.x = sphere.radius * std::cos(lat) * std::cos(lon + lonEvery) + sphere.center.x;
			c.y = sphere.radius * std::sin(lat) + sphere.center.y;
			c.z = sphere.radius * std::cos(lat) * std::sin(lon + lonEvery) + sphere.center.z;

			LineRenderer::DrawLine(a, b, color);
			LineRenderer::DrawLine(a, c, color);
		}
	}
}

void DrawCapsule(const Capsule& capsule, const Vector4& color) {
	if (capsule.radius <= 0.0f) {
		return;
	}

	const float radius = capsule.radius;
	Vector3 axisVector = capsule.p1 - capsule.p0;
	float axisLength = Length(axisVector);
	Vector3 axis = (axisLength > 1e-6f) ? axisVector * (1.0f / axisLength) : Vector3{0.0f, 1.0f, 0.0f};

	// axisに垂直な正規直交基底(right, forward)を作る。
	Vector3 reference = (std::abs(axis.y) < 0.99f) ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
	Vector3 right = Normalize(Cross(reference, axis));
	Vector3 forward = Cross(axis, right);

	const int32_t segment = kColliderSphereSubdivision;
	const float twoPi = 2.0f * kColliderDebugPi;

	// 両端の円(axisに垂直な面)。
	auto drawRing = [&](const Vector3& center) {
		for (int32_t i = 0; i < segment; ++i) {
			float t0 = twoPi * (static_cast<float>(i) / static_cast<float>(segment));
			float t1 = twoPi * (static_cast<float>(i + 1) / static_cast<float>(segment));
			Vector3 a = center + (right * std::cos(t0) + forward * std::sin(t0)) * radius;
			Vector3 b = center + (right * std::cos(t1) + forward * std::sin(t1)) * radius;
			LineRenderer::DrawLine(a, b, color);
		}
	};
	drawRing(capsule.p0);
	drawRing(capsule.p1);

	// 半球のシルエット弧(0..pi の半円)。axisSignで膨らむ向きを切り替える。
	auto drawArc = [&](const Vector3& center, const Vector3& sideDir, float axisSign) {
		for (int32_t i = 0; i < segment; ++i) {
			float f0 = kColliderDebugPi * (static_cast<float>(i) / static_cast<float>(segment));
			float f1 = kColliderDebugPi * (static_cast<float>(i + 1) / static_cast<float>(segment));
			Vector3 a = center + (sideDir * std::cos(f0) + axis * (axisSign * std::sin(f0))) * radius;
			Vector3 b = center + (sideDir * std::cos(f1) + axis * (axisSign * std::sin(f1))) * radius;
			LineRenderer::DrawLine(a, b, color);
		}
	};
	drawArc(capsule.p1, right, 1.0f);
	drawArc(capsule.p1, forward, 1.0f);
	drawArc(capsule.p0, right, -1.0f);
	drawArc(capsule.p0, forward, -1.0f);

	// cylinder側面(4本)。
	LineRenderer::DrawLine(capsule.p0 + right * radius, capsule.p1 + right * radius, color);
	LineRenderer::DrawLine(capsule.p0 - right * radius, capsule.p1 - right * radius, color);
	LineRenderer::DrawLine(capsule.p0 + forward * radius, capsule.p1 + forward * radius, color);
	LineRenderer::DrawLine(capsule.p0 - forward * radius, capsule.p1 - forward * radius, color);
}

// Trigger Colliderは通常Colliderと色を変えて描く(黄=Trigger, 緑=通常)。
constexpr Vector4 kTriggerColliderDebugColor = {1.0f, 0.85f, 0.1f, 1.0f};

// 1つのGameObjectが持つColliderをすべてワイヤーフレームで描く。
void DrawGameObjectColliders(GameObject& gameObject) {
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (!component || !component->IsEnabled()) {
			continue;
		}

		ColliderComponent* collider = dynamic_cast<ColliderComponent*>(component.get());
		if (!collider) {
			continue;
		}

		const Vector4& color = collider->IsTrigger() ? kTriggerColliderDebugColor : kColliderDebugColor;
		if (collider->GetShapeType() == ColliderShapeType::Sphere) {
			DrawSphere(collider->GetWorldSphere(), color);
		} else if (collider->GetShapeType() == ColliderShapeType::Box) {
			BoxColliderComponent* boxCollider = dynamic_cast<BoxColliderComponent*>(collider);
			if (boxCollider && boxCollider->UsesWorldOBB()) {
				DrawOBB(boxCollider->GetWorldOBB(), color);
			} else {
				DrawAABB(collider->GetWorldAABB(), color);
			}
		} else if (collider->GetShapeType() == ColliderShapeType::Capsule) {
			CapsuleColliderComponent* capsuleCollider = dynamic_cast<CapsuleColliderComponent*>(collider);
			if (capsuleCollider) {
				DrawCapsule(capsuleCollider->GetWorldCapsule(), color);
			}
		}
	}
}

// 自分と、その配下すべてのColliderを描く。
void DrawGameObjectCollidersRecursive(GameObject& gameObject) {
	if (!gameObject.IsActiveInHierarchy()) {
		return;
	}
	DrawGameObjectColliders(gameObject);
	for (GameObject* child : gameObject.GetChildren()) {
		if (child) {
			DrawGameObjectCollidersRecursive(*child);
		}
	}
}

void DrawColliderDebugLines(Scene& scene) {
	// 出し分けは呼び出し側(RenderViewのdrawEditorOverlays)が担当する。Sceneビューはプレイ中も表示。
	GameObject* selectedObject = GetSelectionProvider().GetSelectedGameObject();
	if (!selectedObject || !selectedObject->IsActiveInHierarchy()) {
		return;
	}
	if (scene.FindGameObjectByInstanceId(selectedObject->GetInstanceId()) != selectedObject) {
		return;
	}

	// **選んだものだけでなく、その配下も描く。**
	// Colliderを子オブジェクトに分けて持たせている構成(ステージの床/壁など)だと、
	// 親を選んでも一本も出ず、子を1つずつ選んで回らないと形が見えない。
	// 動かすのは親なので、親を選んだ時点で配下がまとめて見えないと調整ができない。
	DrawGameObjectCollidersRecursive(*selectedObject);
}

// シーン内の全アクティブGameObjectのColliderを描く(ゲーム中の当たり判定確認用)。
void DrawAllColliderDebugLines(Scene& scene) {
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		DrawGameObjectColliders(*gameObject);
	}
}

// カメラの視錘台(フラスタム)をワイヤーフレームで描く。Unityのカメラ選択時のギズモ相当。
// 実far(既定1000)は大きすぎるので、向きが分かる固定サイズ(おおよそ5x5x5に収まる)で描く。
// 断面の縦横比は画面(ウィンドウ)の縦横比に沿わせる。cornersはカメラローカル(+Z前方)で作り
// worldMatrixでワールドへ変換する。
void DrawCameraFrustum(const Camera& camera, const Matrix4x4& worldMatrix, const Vector4& color) {
	const float tanHalfFovY = std::tan(camera.fovAngleY * 0.5f);
	const float screenAspect = static_cast<float>(WinApp::kWindowWidth) / static_cast<float>(WinApp::kWindowHeight);

	// far側の奥行きを固定し、半幅/半高/奥行きのどれかが2.5(=5の半分)を超えたら等倍で縮める。
	const float kHalfBox = 2.5f;
	float farDistance = 2.5f;
	const float farHalfHeight = tanHalfFovY * farDistance;
	const float farHalfWidth = farHalfHeight * screenAspect;
	const float maxExtent = (std::max)(farDistance, (std::max)(farHalfWidth, farHalfHeight));
	if (maxExtent > kHalfBox) {
		farDistance *= kHalfBox / maxExtent;
	}
	const float nearDistance = farDistance * 0.1f; // near平面は小さく(四角錐に近い見た目)

	auto MakePlaneCorners = [&](float distance, Vector3 outCorners[4]) {
		const float halfHeight = tanHalfFovY * distance;
		const float halfWidth = halfHeight * screenAspect;
		const Vector3 localCorners[4] = {
		    {-halfWidth, -halfHeight, distance},
		    {halfWidth, -halfHeight, distance},
		    {halfWidth, halfHeight, distance},
		    {-halfWidth, halfHeight, distance},
		};
		for (int i = 0; i < 4; ++i) {
			outCorners[i] = Transform(localCorners[i], worldMatrix);
		}
	};

	Vector3 nearCorners[4];
	Vector3 farCorners[4];
	MakePlaneCorners(nearDistance, nearCorners);
	MakePlaneCorners(farDistance, farCorners);

	for (int i = 0; i < 4; ++i) {
		const int next = (i + 1) % 4;
		LineRenderer::DrawLine(nearCorners[i], nearCorners[next], color); // near平面
		LineRenderer::DrawLine(farCorners[i], farCorners[next], color);   // far平面
		LineRenderer::DrawLine(nearCorners[i], farCorners[i], color);     // 側面エッジ
	}
}

// 選択中のGameObjectがCameraComponentを持つ場合、そのフラスタムを描く。
void DrawCameraDebugLines(Scene& scene) {
	// 出し分けは呼び出し側(RenderViewのdrawEditorOverlays)が担当する。
	GameObject* selectedObject = GetSelectionProvider().GetSelectedGameObject();
	if (!selectedObject || !selectedObject->IsActiveInHierarchy()) {
		return;
	}
	if (scene.FindGameObjectByInstanceId(selectedObject->GetInstanceId()) != selectedObject) {
		return;
	}

	for (const std::unique_ptr<Component>& component : selectedObject->GetComponents()) {
		if (!component || !component->IsEnabled()) {
			continue;
		}
		const ISceneCamera* sceneCamera = dynamic_cast<const ISceneCamera*>(component.get());
		if (!sceneCamera) {
			continue;
		}
		const Camera* camera = sceneCamera->GetSceneCamera();
		if (!camera) {
			continue;
		}
		// カメラが実際に描画へ使う姿勢(=ローカル回転+ワールド位置。SyncFromOwnerTransform参照)で錐体を描く。
		// オーナーのmatWorld_(親の回転/スケールを含む)を使うとGameビューの見え方とズレるため、
		// カメラ自身の向きに合わせる(親が回転していても錐体は実際の描画と一致する)。
		Matrix4x4 cameraWorld = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, camera->rotation_, camera->translation_);
		DrawCameraFrustum(*camera, cameraWorld, kCameraFrustumColor);
	}
}

// SpotLightの照射コーンをワイヤーで描く。Unityのスポットライト選択時のギズモ相当。
// apexから前方rangeの位置に外側角の円を置き、apexとの間を4本のエッジで結ぶ。
// 内側角(減衰開始)の円も同じ距離に薄く描いて、Inner Spot Angleの効きが見えるようにする。
void DrawSpotLightCone(const Vector3& apex, const Vector3& direction, float range, float spotAngleDegrees, float innerSpotAngleDegrees, const Vector4& color) {
	if (range <= 0.0f) {
		return;
	}

	// 照射軸に垂直な基底を作る。directionがほぼY軸のときだけ別の軸を種にする。
	Vector3 seed = (std::abs(direction.y) > 0.99f) ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 1.0f, 0.0f};
	Vector3 right = Normalize(Cross(seed, direction));
	Vector3 up = Normalize(Cross(direction, right));

	const Vector3 center = apex + direction * range;

	auto DrawAngleCircle = [&](float angleDegrees, const Vector4& circleColor, bool drawEdges) {
		// 全開き角なので半分がコーンの半頂角。円の半径は range * tan(半頂角)。
		const float halfAngle = angleDegrees * 0.5f * (kColliderDebugPi / 180.0f);
		const float radius = range * std::tan((std::min)(halfAngle, kColliderDebugPi * 0.5f - 0.01f));

		const int32_t segment = kColliderSphereSubdivision * 2;
		Vector3 previous{};
		for (int32_t i = 0; i <= segment; ++i) {
			const float theta = 2.0f * kColliderDebugPi * (static_cast<float>(i) / static_cast<float>(segment));
			const Vector3 point = center + right * (std::cos(theta) * radius) + up * (std::sin(theta) * radius);
			if (i > 0) {
				LineRenderer::DrawLine(previous, point, circleColor);
			}
			// エッジは4方向だけ引く(全周に引くと塗り潰しになって向きが読めない)。
			if (drawEdges && (i % (segment / 4) == 0) && i < segment) {
				LineRenderer::DrawLine(apex, point, circleColor);
			}
			previous = point;
		}
	};

	// 照射軸。コーンが細いときでも向きが分かるように必ず引く。
	LineRenderer::DrawLine(apex, center, color);
	DrawAngleCircle(spotAngleDegrees, color, true);
	if (innerSpotAngleDegrees > 0.0f && innerSpotAngleDegrees < spotAngleDegrees) {
		DrawAngleCircle(innerSpotAngleDegrees, kLightInnerConeColor, false);
	}
}

// 選択中のGameObjectが持つライトの影響範囲を描く。
// PointLightはradiusの球、SpotLightは照射コーン(Unityのライトギズモ相当)。
void DrawLightDebugLines(Scene& scene) {
	// 出し分けは呼び出し側(RenderViewのdrawEditorOverlays)が担当する。
	GameObject* selectedObject = GetSelectionProvider().GetSelectedGameObject();
	if (!selectedObject || !selectedObject->IsActiveInHierarchy()) {
		return;
	}
	if (scene.FindGameObjectByInstanceId(selectedObject->GetInstanceId()) != selectedObject) {
		return;
	}

	for (const std::unique_ptr<Component>& component : selectedObject->GetComponents()) {
		if (!component || !component->IsEnabled()) {
			continue;
		}

		PointLightComponent* pointLight = dynamic_cast<PointLightComponent*>(component.get());
		if (pointLight) {
			const PointLightData& data = pointLight->GetData();
			if (data.radius > 0.0f) {
				DrawSphere({selectedObject->GetTransform().GetWorldPosition(), data.radius}, kLightGizmoColor);
			}
			continue;
		}

		SpotLightComponent* spotLight = dynamic_cast<SpotLightComponent*>(component.get());
		if (spotLight) {
			DrawSpotLightCone(
			    selectedObject->GetTransform().GetWorldPosition(),
			    spotLight->GetWorldDirection(),
			    spotLight->GetRange(),
			    spotLight->GetSpotAngleDegrees(),
			    spotLight->GetInnerSpotAngleDegrees(),
			    kLightGizmoColor);
		}
	}
}

void DrawGrid(Scene& scene, const Vector4& color) {
	const float kGridHalfWidth = 100.0f;
	const uint32_t kSubdivision = 100;
	const float kGridEvery = (kGridHalfWidth * 2.0f) / float(kSubdivision);

	for (uint32_t xIndex = 0; xIndex <= kSubdivision; ++xIndex) {
		Vector3 lineStart = {-kGridHalfWidth + xIndex * kGridEvery, 0.0f, -kGridHalfWidth};
		Vector3 lineEnd = {-kGridHalfWidth + xIndex * kGridEvery, 0.0f, kGridHalfWidth};

		LineRenderer::DrawLine(lineStart, lineEnd, color);
	}

	for (uint32_t zIndex = 0; zIndex <= kSubdivision; ++zIndex) {
		Vector3 lineStart = {-kGridHalfWidth, 0.0f, -kGridHalfWidth + zIndex * kGridEvery};
		Vector3 lineEnd = {kGridHalfWidth, 0.0f, -kGridHalfWidth + zIndex * kGridEvery};

		LineRenderer::DrawLine(lineStart, lineEnd, color);
	}
}

std::string EscapeJsonString(const std::string& text) {
	std::string escaped;
	escaped.reserve(text.size());

	for (char character : text) {
		if (character == '\\') {
			escaped += "\\\\";
		} else if (character == '"') {
			escaped += "\\\"";
		} else if (character == '\n') {
			escaped += "\\n";
		} else if (character == '\r') {
			escaped += "\\r";
		} else if (character == '\t') {
			escaped += "\\t";
		} else {
			escaped += character;
		}
	}

	return escaped;
}

Model* GetOrCreateEditorBillboardModel(const std::string& iconName) {
	if (iconName.empty()) {
		return nullptr;
	}

	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	auto found = cache.models.find(iconName);
	if (found != cache.models.end()) {
		return found->second.get();
	}

	// Editor用アイコンも通常の3Dモデルと同じTexture/Model経路で扱い、GameWindow内の実体あるPlaneとして描画する。
	std::string texturePath = (GetEditorIconDirectory() / iconName).string();
	std::unique_ptr<Model> model(Model::CreatePlane(texturePath, ShaderModel::kNone));
	Model* rawModel = model.get();
	cache.models.emplace(iconName, std::move(model));
	return rawModel;
}

Model* FindEditorBillboardModel(const std::string& iconName) {
	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	auto found = cache.models.find(iconName);
	if (found == cache.models.end()) {
		return nullptr;
	}

	return found->second.get();
}

WorldTransform* GetOrCreateEditorBillboardTransform(const Component* component) {
	if (!component) {
		return nullptr;
	}

	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	auto found = cache.transforms.find(component);
	if (found != cache.transforms.end()) {
		return found->second.get();
	}

	std::unique_ptr<WorldTransform> transform = std::make_unique<WorldTransform>();
	transform->Initialize();
	WorldTransform* rawTransform = transform.get();
	cache.transforms.emplace(component, std::move(transform));
	return rawTransform;
}

WorldTransform* FindEditorBillboardTransform(const Component* component) {
	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	auto found = cache.transforms.find(component);
	if (found == cache.transforms.end()) {
		return nullptr;
	}

	return found->second.get();
}

void PrepareEditorBillboardComponent(const Component* component) {
#ifdef USE_IMGUI
	const IEditorBillboard* billboard = dynamic_cast<const IEditorBillboard*>(component);
	if (!billboard) {
		return;
	}

	// TextureManager::LoadTextureはCommandListを実行するため、描画中ではなく初期化・追加時にPlaneを作る。
	GetOrCreateEditorBillboardModel(billboard->GetEditorBillboardIconName());
	GetOrCreateEditorBillboardTransform(component);
#else
	// エディタUI無し(ゲーム単体)ビルドではEditorカメラが無く、DrawEditorBillboardsは常に描画しない。
	// アイコン画像(KujataEngine/resources/images)も配布物に含めないため、準備自体を行わない。
	(void)component;
#endif // USE_IMGUI
}

void PrepareEditorBillboards(Scene& scene) {
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject) {
			continue;
		}

		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			PrepareEditorBillboardComponent(component.get());
		}
	}
}

void DrawEditorBillboards(Scene& scene) {
	// 出し分けは呼び出し側(RenderViewのdrawEditorOverlays)が担当する。
	Camera* camera = scene.GetEditorCamera();
	if (!camera) {
		return;
	}

	std::unordered_set<const Component*> activeBillboardComponents;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
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
			const ISceneCamera* sceneCamera = dynamic_cast<const ISceneCamera*>(component.get());
			if (sceneCamera && sceneCamera->GetSceneCamera() == camera) {
				// 表示に使っているCamera自身のアイコンを描くと、視点原点のPlaneが目前でちらつくため描画しない。
				continue;
			}

			// Draw中にテクスチャロードが走るとCommandListの状態が壊れるため、準備済みのPlaneだけ描画する。
			Model* model = FindEditorBillboardModel(billboard->GetEditorBillboardIconName());
			WorldTransform* transform = FindEditorBillboardTransform(component.get());
			if (!model || !transform) {
				continue;
			}

			activeBillboardComponents.insert(component.get());

			// GameObjectのワールド位置へPlaneを置き、WorldTransform側のBillboard行列で常にEditorCameraへ向ける。
			transform->translation_ = gameObject->GetTransform().GetWorldPosition();
			transform->rotation_ = {0.0f, 0.0f, 0.0f};
			transform->scale_ = {kEditorBillboardScale, kEditorBillboardScale, kEditorBillboardScale};
			transform->UpdateMatrix(*camera, true);
			model->Draw(*transform, *camera);
		}
	}

	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	for (auto iterator = cache.transforms.begin(); iterator != cache.transforms.end();) {
		if (activeBillboardComponents.find(iterator->first) == activeBillboardComponents.end()) {
			iterator = cache.transforms.erase(iterator);
		} else {
			++iterator;
		}
	}
}

void ClearEditorBillboardDrawCache() {
	EditorBillboardDrawCache& cache = GetEditorBillboardDrawCache();
	cache.transforms.clear();
	cache.models.clear();
}

} // namespace

Scene::~Scene() = default;

void Scene::Initialize() {
	if (initialized_) {
		return;
	}

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject) {
			gameObject->Initialize();
		}
	}

	initialized_ = true;
	PrepareEditorBillboards(*this);
}

void Scene::Update() {
	// ゲームロジック(Component::Update)がSetVeloc/移動を行う → 速度積分 → 衝突検出+応答 の順。
	//
	// **添字で回すこと。範囲forは使えない。**
	// Update中に PrefabAsset::Instantiate されると gameObjects_ に push_back されて再確保が起き、
	// 範囲forのイテレータが無効化されて以降のUpdateが解放済みメモリを触る(thisが壊れる)。
	// 要素は unique_ptr なので、再確保で動くのはポインタだけ。GameObject自体は動かないため添字なら安全。
	// size()を毎回読み直すことで、その場で増えた分も安全に扱える(同フレームで1回Updateされる)。
	for (size_t index = 0; index < gameObjects_.size(); ++index) {
		GameObject* gameObject = gameObjects_[index].get();
		if (gameObject && gameObject->IsRoot()) {
			gameObject->UpdateHierarchy();
		}
	}

	IntegrateRigidbodies(*this, Time::GetDeltaTime());

	UpdateWorldTransforms();
	{
		FrameProfiler::Scope profile(FrameProfiler::kCollision);
		UpdateCollisions();
	}
}

void Scene::RefreshEditorBillboards() { PrepareEditorBillboards(*this); }

void Scene::Draw() {
	UpdateWorldTransforms();

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject && gameObject->IsRoot()) {
			gameObject->DrawHierarchy();
		}
	}

	DrawEditorBillboards(*this);
	DrawColliderDebugLines(*this);
	DrawCameraDebugLines(*this);
	DrawGrid(*this, Vector4(0.4f, 0.4f, 0.4f, 1.0f));
}

void Scene::PrepareFrame() { UpdateWorldTransforms(); }

void Scene::RenderShadowPass() {
	ShadowMap* shadowMap = ShadowMap::GetInstance();
	if (!shadowMap->IsInitialized()) {
		return;
	}

	// ライトの向きは現在のDirectionalLight(=シーン上のDirectionalLightComponentが毎フレーム反映済み)。
	const Vector3& lightDirection = DirectionalLight::GetInstance()->GetData().direction;
	// TODO: focusPositionをプレイヤー/カメラ注視点へ追従させると、広いシーンでも影の解像度を保てる。
	shadowMap->UpdateLightMatrix(lightDirection, {0.0f, 0.0f, 0.0f}, 40.0f, 100.0f);

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	const uint32_t previousViewIndex = dxCommon->GetRenderViewIndex();
	// ライト視点のWVPをScene/Gameとは別の定数バッファ枠へ書くため、ここで切り替える。
	dxCommon->SetRenderViewIndex(DirectXCommon::kShadowViewIndex);

	shadowMap->BeginWrite();
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (!gameObject || !gameObject->IsActiveInHierarchy()) {
			continue;
		}
		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			if (!component || !component->IsEnabled()) {
				continue;
			}
			ModelRendererComponent* renderer = component->AsModelRendererComponent();
			// **Cast Shadowを切ったものは影パスで描かない。**
			// 小さなパーツを大量に置くと、個別の影は絵に効かないのにドローコールだけ倍になる。
			if (renderer && renderer->CastsShadow()) {
				renderer->DrawShadow(shadowMap->GetLightViewProjection());
			}
		}
	}
	shadowMap->EndWrite();

	dxCommon->SetRenderViewIndex(previousViewIndex);
}

void Scene::ApplyVolumes(const Camera* camera) {
	// カメラが無いビュー(まだシーンカメラが揃っていない等)は原点基準で解決しておく。
	// Global Volumeしか無いシーンではカメラ位置は結果に影響しない。
	Vector3 cameraPosition = camera ? camera->translation_ : Vector3{0.0f, 0.0f, 0.0f};
	VolumeResolveResult resolved = VolumeStack::Resolve(*this, cameraPosition);
	PostProcess::GetInstance()->SetActiveProfile(resolved.profile);
}

void Scene::RenderView(Camera* camera, bool drawEditorOverlays) {
	(void)camera; // モデルのカメラは派生側でApplyRenderCameraToModelRenderers済みの想定。

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject && gameObject->IsRoot()) {
			gameObject->DrawHierarchy();
		}
	}

	// 編集用オーバーレイはSceneビューのみ(呼び出し側のdrawEditorOverlaysで制御)。
	if (drawEditorOverlays) {
		DrawEditorBillboards(*this);
		DrawColliderDebugLines(*this);
		DrawCameraDebugLines(*this);
		DrawLightDebugLines(*this);
		DrawGrid(*this, Vector4(0.4f, 0.4f, 0.4f, 1.0f));
	}

	// 全Collider可視化モードがONなら、Sceneビュー/Gameビュー問わず全Colliderを描く。
	// **F1の受け付けはここで行う。**
	// Scene::Update は Play 中しか回らないので、Update側にトグルを置くと
	// 「止めて配置を直している最中に全部を見る」ことができない(調整したい時ほど使えない)。
	if (drawEditorOverlays && Input::GetKeyTrigger(DIK_F1)) {
		g_showAllColliders = !g_showAllColliders;
	}
	if (g_showAllColliders) {
		DrawAllColliderDebugLines(*this);
	}

	// world空間2Dスプライト(Sprite方式)。3Dメッシュの後・スクリーン空間UIの前に、
	// Sorting Order順でまとめて描く(深度を書かないので描画順が前後関係になる)。
	DrawSceneSprites(*this, camera);

	// World Space Canvas。ワールドに置かれたUIなので、選択状態に関わらず常に描く。
	{
		DirectXCommon* dxCommon = DirectXCommon::GetInstance();
		DrawSceneWorldCanvases(*this, camera, static_cast<float>(dxCommon->GetGameRenderWidth()), static_cast<float>(dxCommon->GetGameRenderHeight()));
	}

	// Screen Space CanvasはここでHDRシーンRTへは描かない。フォグ/ブルーム/トーンマップの影響を
	// 受けないよう、ポストプロセス後のLDR RTへRenderScreenSpaceUIで描く。
}

void Scene::RenderScreenSpaceUI(float targetWidth, float targetHeight, bool drawEditorOverlays) {
	// drawEditorOverlays==true はSceneビュー、false はGameビュー。
	// Gameビューは常にUIを描く。SceneビューはUI(Canvasまたはその子孫)を選択中のときだけ描く。
	bool drawUI = !drawEditorOverlays;
	if (drawEditorOverlays) {
		// UI編集モード中は選択に関わらず常にCanvasを描く。それ以外はUI(またはその子孫)選択時のみ。
		drawUI = IsSceneViewUIEditMode() || IsUIRelatedObject(GetSelectionProvider().GetSelectedGameObject());
	}
	if (drawUI) {
		DrawSceneCanvases(*this, targetWidth, targetHeight);
	}
}

void Scene::SetShowAllColliders(bool show) { g_showAllColliders = show; }

bool Scene::IsShowAllColliders() { return g_showAllColliders; }

void Scene::ToggleShowAllColliders() { g_showAllColliders = !g_showAllColliders; }

void Scene::SetSceneName(const std::string& name) { sceneName_ = name; }

void Scene::Finalize() {
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject) {
			gameObject->Finalize();
		}
	}
	gameObjects_.clear();
	ClearEditorBillboardDrawCache();
	collisionPairStates_.clear();
	initialized_ = false;
}

void Scene::OnPlayStart() {
	// Play毎にUIイベント購読を張り直す(Unity同様、前回のPlayのリスナーを持ち越さない)。
	// 各ComponentはこのあとのOnPlayStartで購読するため、先にクリアしておく。
	UIEventBus::Clear();
	// UIのフォーカスも持ち越さない。前のシーンのGameObject*を握ったままだと
	// 破棄済みのアドレスと新しいオブジェクトが偶然一致したときに誤選択になる。
	SetUISelected(nullptr);
	// 時間スケールも必ず等速へ戻す。ポーズ(死亡メニュー等)で0にしたままStopされると、
	// **次のPlayが止まったまま始まる**(戻す責任はSetTimeScaleを呼んだ側にあるが、保険をここに置く)。
	Time::SetTimeScale(1.0f);

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject) {
			gameObject->OnPlayStart();
		}
	}
}

void Scene::OnPlayStop() {
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (gameObject) {
			gameObject->OnPlayStop();
		}
	}
	collisionPairStates_.clear();
}

void Scene::UpdateCollisions() {
	std::vector<ColliderComponent*> colliders;
	CollectSceneColliders(*this, colliders);

	std::unordered_set<ColliderComponent*> activeColliders;
	for (ColliderComponent* collider : colliders) {
		activeColliders.insert(collider);
	}

	// ブロードフェーズ(Sweep and Prune): X軸のAABB minでソートし、Xレンジが重ならない組は
	// 候補から外す。真に交差するペアは全軸でAABBが重なっているはずなので、X軸だけ見ても
	// 取りこぼしは起きない(候補が減るだけで、絞り込みすぎることはない)。
	// コライダー数がN個ある今の規模(ガーディアン等ボーンだらけの敵込みで100前後)では、
	// 総当たり(N^2)よりペア数を大きく減らせる。
	struct BroadPhaseEntry {
		size_t originalIndex;
		AABB aabb;
	};
	std::vector<BroadPhaseEntry> entries;
	entries.reserve(colliders.size());
	for (size_t index = 0; index < colliders.size(); ++index) {
		entries.push_back({index, colliders[index]->GetWorldAABB()});
	}
	std::sort(entries.begin(), entries.end(), [](const BroadPhaseEntry& lhs, const BroadPhaseEntry& rhs) { return lhs.aabb.min.x < rhs.aabb.min.x; });

	std::unordered_map<std::string, CollisionPairState> currentPairStates;

	std::vector<BroadPhaseEntry> active;
	for (const BroadPhaseEntry& entry : entries) {
		// activeのうち、今回のentryより手前でXレンジが終わっている(=もう重なりようがない)ものを外す。
		active.erase(std::remove_if(active.begin(), active.end(),
		                 [&entry](const BroadPhaseEntry& candidate) { return candidate.aabb.max.x < entry.aabb.min.x; }),
		    active.end());

		for (const BroadPhaseEntry& other : active) {
			// 元のcolliders配列でのindexが小さい方をA、大きい方をBに揃える
			// (Sweep順ではなく元の並びで固定し、接触法線の向き等の既存挙動を変えない)。
			size_t indexA = (std::min)(entry.originalIndex, other.originalIndex);
			size_t indexB = (std::max)(entry.originalIndex, other.originalIndex);
			ColliderComponent* colliderA = colliders[indexA];
			ColliderComponent* colliderB = colliders[indexB];

			GameObject* ownerA = colliderA->GetOwner();
			GameObject* ownerB = colliderB->GetOwner();
			if (ownerA == ownerB) {
				continue;
			}
			// 同じ階層ルート(=同じキャラクター)に属するコライダー同士は既定では判定しない。
			// ガーディアンの脚ボーンのように部位ごとにコライダーを分けた構成で、
			// 自分自身の部位同士が毎フレーム無意味に交差判定されるのを防ぐ
			// (絵にもゲームプレイにも出ない負荷。ボーン数が増えるほどO(n^2)で効いてくる)。
			if (ownerA && ownerB && ownerA->GetHierarchyRoot() == ownerB->GetHierarchyRoot()) {
				continue;
			}
			if (!colliderA->Intersects(*colliderB)) {
				continue;
			}

			bool isTrigger = colliderA->IsTrigger() || colliderB->IsTrigger();

			// 非トリガーは接触情報(A→B)を計算し、物理応答(押し出し+速度反射)を適用する。
			// 同じ接触情報をCollisionイベントのnormal/penetration充填にも使う。
			Contact contact{};
			bool hasContact = false;
			if (!isTrigger) {
				hasContact = colliderA->ComputeContact(*colliderB, contact);
				if (hasContact) {
					ResolveCollisionResponse(colliderA, colliderB, contact);
				}
			}
			const Contact* contactPtr = hasContact ? &contact : nullptr;

			std::string pairKey = MakeCollisionPairKey(*colliderA, *colliderB);
			currentPairStates[pairKey] = {colliderA, colliderB, isTrigger};

			auto previous = collisionPairStates_.find(pairKey);
			if (previous == collisionPairStates_.end()) {
				NotifyCollisionPair(colliderA, colliderB, isTrigger, CollisionEventPhase::Enter, contactPtr);
				continue;
			}

			if (previous->second.isTrigger != isTrigger) {
				NotifyCollisionPair(previous->second.colliderA, previous->second.colliderB, previous->second.isTrigger, CollisionEventPhase::Exit);
				NotifyCollisionPair(colliderA, colliderB, isTrigger, CollisionEventPhase::Enter, contactPtr);
				continue;
			}

			NotifyCollisionPair(colliderA, colliderB, isTrigger, CollisionEventPhase::Stay, contactPtr);
		}

		active.push_back(entry);
	}

	for (const auto& [pairKey, previousState] : collisionPairStates_) {
		if (currentPairStates.contains(pairKey)) {
			continue;
		}
		if (!activeColliders.contains(previousState.colliderA) || !activeColliders.contains(previousState.colliderB)) {
			continue;
		}

		NotifyCollisionPair(previousState.colliderA, previousState.colliderB, previousState.isTrigger, CollisionEventPhase::Exit);
	}

	collisionPairStates_ = std::move(currentPairStates);
}

GameObject* Scene::CreateGameObject(const std::string& name) { return AddGameObject(std::make_unique<GameObject>(name)); }

GameObject* Scene::CreateEditorEntity() { return CreateGameObject("Entity"); }

void Scene::OnEditorComponentAdded(GameObject* gameObject, Component* component) {
	(void)gameObject;
	PrepareEditorBillboardComponent(component);
}

GameObject* Scene::AddGameObject(std::unique_ptr<GameObject> gameObject) {
	GameObject* raw = gameObject.get();
	if (raw) {
		raw->SetScene(this);
	}
	gameObjects_.push_back(std::move(gameObject));

	if (initialized_ && raw) {
		raw->Initialize();
		for (const std::unique_ptr<Component>& component : raw->GetComponents()) {
			PrepareEditorBillboardComponent(component.get());
		}
	}

	return raw;
}

void Scene::RemoveGameObjectHierarchy(GameObject* gameObject) {
	if (!gameObject) {
		return;
	}

	std::vector<GameObject*> removeTargets;
	removeTargets.push_back(gameObject);
	for (size_t index = 0; index < removeTargets.size(); ++index) {
		GameObject* current = removeTargets[index];
		if (!current) {
			continue;
		}
		for (GameObject* child : current->GetChildren()) {
			if (child) {
				removeTargets.push_back(child);
			}
		}
	}

	for (GameObject* target : removeTargets) {
		if (target) {
			target->Finalize();
		}
	}

	auto shouldRemove = [&removeTargets](const std::unique_ptr<GameObject>& current) {
		if (!current) {
			return false;
		}
		return std::find(removeTargets.begin(), removeTargets.end(), current.get()) != removeTargets.end();
	};
	gameObjects_.erase(std::remove_if(gameObjects_.begin(), gameObjects_.end(), shouldRemove), gameObjects_.end());
}

bool Scene::MoveGameObjectOrder(GameObject* dragged, GameObject* target, bool insertAfter) {
	if (!dragged || !target || dragged == target) {
		return false;
	}
	// 自分の子孫の隣には移動できない(自分を自分の中へ入れることになるため)。
	if (target->IsDescendantOf(dragged)) {
		return false;
	}

	GameObject* newParent = target->GetParent();

	// 1. 兄弟にするため、必要なら親を変更(ワールド位置を維持)。
	if (dragged->GetParent() != newParent) {
		if (!dragged->SetParent(newParent, true)) {
			return false;
		}
	}

	// 2. ランタイムの子表示順(children_)を更新。ルート同士(newParent==nullptr)は対象外。
	if (newParent) {
		newParent->ReorderChild(dragged, target, insertAfter);
	}

	// 3. 永続化順(gameObjects_の並び)を、draggedの所有ptrがtargetの直前/直後へ来るよう移動。
	//    保存はgameObjects_順で書き出されるため、これでルート順・子順の両方が保存に反映される。
	std::vector<std::unique_ptr<GameObject>>::iterator draggedIt =
	    std::find_if(gameObjects_.begin(), gameObjects_.end(),
	        [dragged](const std::unique_ptr<GameObject>& current) { return current.get() == dragged; });
	if (draggedIt == gameObjects_.end()) {
		return true;
	}
	std::unique_ptr<GameObject> ownedDragged = std::move(*draggedIt);
	gameObjects_.erase(draggedIt);

	std::vector<std::unique_ptr<GameObject>>::iterator targetIt =
	    std::find_if(gameObjects_.begin(), gameObjects_.end(),
	        [target](const std::unique_ptr<GameObject>& current) { return current.get() == target; });
	if (targetIt == gameObjects_.end()) {
		gameObjects_.push_back(std::move(ownedDragged));
		return true;
	}
	if (insertAfter) {
		++targetIt;
	}
	gameObjects_.insert(targetIt, std::move(ownedDragged));
	return true;
}

GameObject* Scene::FindGameObjectByName(const std::string& name) const {
	if (name.empty()) {
		return nullptr;
	}

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (!gameObject) {
			continue;
		}
		if (gameObject->GetName() == name) {
			return gameObject.get();
		}
	}
	return nullptr;
}

GameObject* Scene::FindGameObjectByInstanceId(const std::string& instanceId) const {
	if (instanceId.empty()) {
		return nullptr;
	}

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		if (!gameObject) {
			continue;
		}
		if (gameObject->GetInstanceId() == instanceId) {
			return gameObject.get();
		}
	}

	return nullptr;
}

void Scene::UpdateWorldTransforms() {
	// ここも添字で回す(Update同様、途中で生成されても壊れないように)。
	for (size_t index = 0; index < gameObjects_.size(); ++index) {
		GameObject* gameObject = gameObjects_[index].get();
		if (gameObject && gameObject->IsRoot()) {
			gameObject->UpdateWorldTransformHierarchy();
		}
	}
}

std::string Scene::ToJson() const {
	std::ostringstream os;
	os << "{\n";
	os << "  \"objects\": [\n";

	for (size_t objectIndex = 0; objectIndex < gameObjects_.size(); ++objectIndex) {
		const std::unique_ptr<GameObject>& gameObject = gameObjects_[objectIndex];
		if (!gameObject) {
			continue;
		}

		os << "    {\n";
		os << "      \"instanceId\": \"" << EscapeJsonString(gameObject->GetInstanceId()) << "\",\n";
		GameObject* parent = gameObject->GetParent();
		std::string parentInstanceId;
		if (parent) {
			parentInstanceId = parent->GetInstanceId();
		}
		os << "      \"parentInstanceId\": \"" << EscapeJsonString(parentInstanceId) << "\",\n";
		os << "      \"prefabAssetPath\": \"" << EscapeJsonString(gameObject->GetPrefabAssetPath()) << "\",\n";
		os << "      \"prefabObjectId\": \"" << EscapeJsonString(gameObject->GetPrefabObjectId()) << "\",\n";
		os << "      \"prefabInstanceRootId\": \"" << EscapeJsonString(gameObject->GetPrefabInstanceRootId()) << "\",\n";
		os << "      \"prefabInstanceRoot\": ";
		if (gameObject->IsPrefabInstanceRoot()) {
			os << "true,\n";
		} else {
			os << "false,\n";
		}
		os << "      \"name\": \"" << EscapeJsonString(gameObject->GetName()) << "\",\n";
		os << "      \"tag\": \"" << EscapeJsonString(gameObject->GetTag()) << "\",\n";
		os << "      \"layer\": " << gameObject->GetLayer() << ",\n";
		os << "      \"active\": ";
		if (gameObject->IsActive()) {
			os << "true,\n";
		} else {
			os << "false,\n";
		}
		os << "      \"components\": [\n";

		const std::vector<std::unique_ptr<Component>>& components = gameObject->GetComponents();
		for (size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
			const std::unique_ptr<Component>& component = components[componentIndex];
			if (!component) {
				continue;
			}

			component->WriteJson(os, 8);
			if (componentIndex + 1 < components.size()) {
				os << ",";
			}
			os << "\n";
		}

		os << "      ]\n";
		os << "    }";
		if (objectIndex + 1 < gameObjects_.size()) {
			os << ",";
		}
		os << "\n";
	}

	os << "  ]\n";
	os << "}\n";
	return os.str();
}

} // namespace KujataEngine

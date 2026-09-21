#include "ScenePhysics.h"
#include "Scene.h"
#include "../components/ColliderComponent.h"
#include "../components/RigidbodyComponent.h"
#include "../math/MathUtil.h"
#include "../shapes/ShapeUtil.h"

namespace KujataEngine {

namespace {

// グローバル重力(Unity既定に合わせる)。RigidbodyのgravityScale倍で各動的ボディに適用する。
constexpr Vector3 kRigidbodyGravity = {0.0f, -9.81f, 0.0f};

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

} // namespace

// 非トリガーの交差ペアに剛体反発(押し出し+速度反射)を適用する。
// 動的(Rigidbody有・非Static)のみ押し出し/速度変化の対象。キネマティック(Is Static)と
// Rigidbody無しは無限質量として不動だが、キネマティックは自身の速度を相手へ与える。
// contact は colliderA→colliderB 基準(呼び出し側で計算済み)。
void ScenePhysics::ResolveCollisionResponse(ColliderComponent* colliderA, ColliderComponent* colliderB, const Contact& contact) {
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
void ScenePhysics::IntegrateRigidbodies(Scene& scene, float deltaTime) {
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

} // namespace KujataEngine

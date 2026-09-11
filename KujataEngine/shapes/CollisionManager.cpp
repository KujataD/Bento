#include "CollisionManager.h"
#include "ShapeUtil.h"

namespace KujataEngine {

using namespace ShapeUtil;

void CollisionManager::Clear() { colliders_.clear(); }

void CollisionManager::Update() {

	std::list<Collider*>::iterator itrA = colliders_.begin();
	for (; itrA != colliders_.end(); ++itrA) {
		Collider* colA = *itrA;
		// イテレータBはイテレータAの次の要素から回す(重複判定を回避)
		std ::list<Collider*>::iterator itrB = itrA;
		itrB++;

		for (; itrB != colliders_.end(); ++itrB) {
			Collider* colB = *itrB;

			CheckCollisionPair(colA, colB);
		}
	}
}

void CollisionManager::CheckCollisionPair(Collider* colliderA, Collider* colliderB) {
	if (!(colliderA->GetCollisionAttribute() & colliderB->GetCollisionMask()) || !(colliderB->GetCollisionAttribute() & colliderA->GetCollisionMask())) {
		return;
	}

	if (IsCollision(colliderA->GetSphere(), colliderB->GetSphere())) {
		colliderA->OnCollision();
		colliderB->OnCollision();
	}
}

} // namespace KujataEngine
#pragma once
#include "../runtime/KujataApi.h"
#include <list>
#include "Collider.h"

namespace KujataEngine{

class KUJATA_API CollisionManager {
public:

	void Clear();

	void Update();

	void CheckCollisionPair(Collider* colliderA, Collider* colliderB);

	void AddCollider(Collider* collider) { colliders_.push_back(collider); }

private:
	std::list<Collider*> colliders_;
};

}

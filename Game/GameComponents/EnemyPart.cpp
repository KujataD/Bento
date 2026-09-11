#include "EnemyPart.h"

#include "EnemyHealth.h"

using namespace KujataEngine;

void EnemyPart::OnPlayStart() {
	// 自分から親へ遡ってHPを探す。**部位自身はHPを持たない**(1本を共有するのが要点)。
	health_ = GetComponentInParent<EnemyHealth>();
}

bool EnemyPart::IsTargetable() const {
	if (!targetable_) {
		return false;
	}
	if (!owner_ || !owner_->IsActiveInHierarchy()) {
		return false;
	}
	// 持ち主が倒れたら部位も的から外れる。HPが見つからない構成では狙える扱いにしておく。
	return !health_ || health_->IsAlive();
}

Vector3 EnemyPart::GetLockOnPoint() const {
	if (!owner_) {
		return {0.0f, 0.0f, 0.0f};
	}
	// **親のTransformが今フレーム書き換わっている可能性がある**ので、自前で合成してから読む
	// (Scene::UpdateWorldTransformsは全Updateの後に走るため、そのままでは1フレーム古い)。
	owner_->UpdateWorldTransformSelfAndAncestors();
	return owner_->GetTransform().GetWorldPosition() + lockOnOffset_;
}

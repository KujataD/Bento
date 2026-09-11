#include "BossHPBarUpdater.h"

#include "components/ImageComponent.h"
#include "EnemyHealth.h"

using namespace KujataEngine;

void BossHPBarUpdater::OnPlayStart() { fillImage_ = nullptr; }

void BossHPBarUpdater::Update() {
	if (!owner_) {
		return;
	}
	if (!fillImage_) {
		if (GameObject* fill = FindDescendant(fillObjectName_)) {
			fillImage_ = fill->GetComponent<ImageComponent>();
		}
		if (!fillImage_) {
			return;
		}
	}

	// 毎フレーム引き直す。撃破やリトライで対象が入れ替わっても追従させたいので、
	// コールバックではなくポーリングにしてある(PlayerHPBarUpdaterと同じ方針)。
	EnemyHealth* health = FindBoss();
	if (!health || !health->IsAlive()) {
		if (hideWhenAbsent_) {
			SetChildrenVisible(false);
		}
		return;
	}

	SetChildrenVisible(true);
	fillImage_->SetFillAmount(health->GetHealthPercent());
}

EnemyHealth* BossHPBarUpdater::FindBoss() const {
	if (!owner_ || !owner_->GetScene() || bossName_.empty()) {
		return nullptr;
	}
	GameObject* boss = owner_->GetScene()->FindGameObjectByName(bossName_);
	return boss ? boss->GetComponent<EnemyHealth>() : nullptr;
}

namespace {

GameObject* FindDescendantByName(GameObject* object, const std::string& name) {
	if (!object) {
		return nullptr;
	}
	for (GameObject* child : object->GetChildren()) {
		if (!child) {
			continue;
		}
		if (child->GetName() == name) {
			return child;
		}
		if (GameObject* found = FindDescendantByName(child, name)) {
			return found;
		}
	}
	return nullptr;
}

} // namespace

GameObject* BossHPBarUpdater::FindDescendant(const std::string& name) const {
	if (name.empty()) {
		return nullptr;
	}
	return FindDescendantByName(owner_, name);
}

void BossHPBarUpdater::SetChildrenVisible(bool visible) {
	if (!owner_) {
		return;
	}
	for (GameObject* child : owner_->GetChildren()) {
		if (child && child->IsActive() != visible) {
			child->SetActive(visible);
		}
	}
}

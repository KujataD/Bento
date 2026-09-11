#include "PlayerHPBarUpdater.h"
#include "components/ImageComponent.h"
#include "PartyManager.h"
#include "PlayerHealth.h"

void PlayerHPBarUpdater::Update() {
	KujataEngine::GameObject* owner = GetOwner();
	if (!owner) {
		return;
	}
	if (!fillImage_) {
		fillImage_ = owner->GetComponent<KujataEngine::ImageComponent>();
		if (!fillImage_) {
			return;
		}
	}

	// リーダーは切替で変わるので毎フレーム引き直す(コールバックに頼らず確実に追従させる)。
	KujataEngine::GameObject* leader = PartyManager::FindLeaderInScene(owner->GetScene(), playerName_);
	PlayerHealth* health = leader ? leader->GetComponent<PlayerHealth>() : nullptr;
	if (!health) {
		return;
	}
	fillImage_->SetFillAmount(health->GetHealthPercent());
}

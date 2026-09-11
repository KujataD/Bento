#include "PlayerStaminaBarUpdater.h"
#include "components/ImageComponent.h"
#include "PartyManager.h"
#include "StaminaComponent.h"

void PlayerStaminaBarUpdater::Update() {
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

	KujataEngine::GameObject* leader = PartyManager::FindLeaderInScene(owner->GetScene(), playerName_);
	StaminaComponent* stamina = leader ? leader->GetComponent<StaminaComponent>() : nullptr;
	if (!stamina) {
		return;
	}
	fillImage_->SetFillAmount(stamina->GetPercent());
}

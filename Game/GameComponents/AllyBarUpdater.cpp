#include "AllyBarUpdater.h"

#include "PartyManager.h"
#include "PlayerHealth.h"

#include <components/ImageComponent.h>

using namespace KujataEngine;

KujataEngine::GameObject* AllyBarUpdater::FindByName(const std::string& name) const {
	if (name.empty() || !owner_ || !owner_->GetScene()) {
		return nullptr;
	}
	return owner_->GetScene()->FindGameObjectByName(name);
}

void AllyBarUpdater::Update() {
	if (!owner_) {
		return;
	}
	if (!fillImage_) {
		fillImage_ = owner_->GetComponent<ImageComponent>();
		if (!fillImage_) {
			return;
		}
	}

	GameObject* hpBar = FindByName(hpBarName_);
	GameObject* reviveBar = FindByName(reviveBarName_);

	// **相方は切替で入れ替わるので毎フレーム引き直す。**
	// 操作キャラを持ち替えると「相方」が指す先も入れ替わる。
	PartyManager* party = PartyManager::FindInScene(owner_->GetScene());
	GameObject* ally = party ? party->GetAlly() : nullptr;
	PlayerHealth* health = ally ? ally->GetComponent<PlayerHealth>() : nullptr;

	if (!health) {
		// 一人のシーンではバーごと畳む。空のバーが残ると「誰かが居る」と誤解させる。
		if (hpBar && hpBar->IsActive()) {
			hpBar->SetActive(false);
		}
		if (reviveBar && reviveBar->IsActive()) {
			reviveBar->SetActive(false);
		}
		return;
	}

	if (hpBar && !hpBar->IsActive()) {
		hpBar->SetActive(true);
	}
	fillImage_->SetFillAmount(health->GetHealthPercent());

	// **蘇生バーは倒れている間だけ。**
	// 生きている間も出しておくと満タンのバーが並ぶだけで、
	// 「今どちらの数字を見ればいいのか」が読めなくなる。
	if (!reviveBar) {
		return;
	}
	const bool down = health->IsDead();
	if (reviveBar->IsActive() != down) {
		reviveBar->SetActive(down);
	}
	if (!down) {
		return;
	}
	if (GameObject* reviveFill = FindByName(reviveFillName_)) {
		if (ImageComponent* image = reviveFill->GetComponent<ImageComponent>()) {
			image->SetFillAmount(health->GetReviveProgress());
		}
	}
}

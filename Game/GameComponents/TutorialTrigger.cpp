#include "TutorialTrigger.h"

#include "PartyManager.h"
#include "ScreenFader.h"
#include "TutorialManager.h"

#include <components/ColliderComponent.h>
#include <vector>

using namespace KujataEngine;

void TutorialTrigger::OnPlayStart() {
	// Playインスタンスは使い回されるので、発火済みフラグは必ず戻す
	// (戻さないと2回目のPlayでチュートリアルが一切出なくなる)。
	fired_ = false;
}

bool TutorialTrigger::IsLeader(GameObject* visitor) const {
	if (!leaderOnly_) {
		return true;
	}
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	GameObject* leader = PartyManager::FindLeaderInScene(scene, "");
	// リーダーが分からないときは止めない(PartyManagerを置いていないシーンでも動くように)。
	return !leader || leader == visitor;
}

bool TutorialTrigger::MatchesCharacter(GameObject* visitor) const {
	if (requireCharacter_.empty() || !visitor) {
		return true;
	}
	return visitor->GetName() == requireCharacter_;
}

void TutorialTrigger::OnTriggerEnter(ColliderComponent* other) {
	if (fired_ || !other) {
		return;
	}
	GameObject* visitor = other->GetOwner();
	if (!visitor) {
		return;
	}
	// Tagで絞る。武器や弾のColliderも飛んでくるので、これが無いと誤爆する。
	if (!requireTag_.empty() && !visitor->CompareTag(requireTag_)) {
		return;
	}
	// **AI相方に踏ませない。** 2人とも同じAllyタグなので、これが無いと
	// 先に歩いた相方の側で説明が出たり出口が開いたりする。
	if (!IsLeader(visitor)) {
		return;
	}
	// 剣士用/術師用の出し分け。条件が合わなければ**発火済みにせず**据え置き、
	// もう一方のトリガーに任せる。
	if (!MatchesCharacter(visitor)) {
		return;
	}

	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene) {
		return;
	}

	if (!nextScene_.empty()) {
		// ステージの出口。**課題を残したままでは通さない。**
		TutorialManager* gate = TutorialManager::FindInScene(scene);
		if (gate && !gate->IsCourseComplete()) {
			gate->NotifyExitBlocked();
			return; // 発火済みにしない。課題を終えてから戻ってくれば開く。
		}
		// 暗転しきってから切り替える(ScreenFader経由)。
		fired_ = true;
		ScreenFader::RequestTransition(scene, nextScene_, true);
		return;
	}

	TutorialManager* manager = TutorialManager::FindInScene(scene);
	if (!manager) {
		return; // 出す先が無いなら発火済みにせず据え置く。
	}
	fired_ = true;
	manager->Show(title_, std::vector<std::string>{line0_, line1_, line2_, line3_, line4_, line5_},
	    TutorialManager::ParseObjective(objective_), objectiveCount_);
}

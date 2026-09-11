#include "TutorialManager.h"

#include "GameEvents.h"
#include "IEnemy.h"
#include "PartyManager.h"

#include <components/TextComponent.h>

using namespace KujataEngine;

namespace {

/// <summary>課題の見出し(残り回数は呼び出し側で足す)。</summary>
std::string ObjectiveLabel(TutorialManager::Objective objective) {
	switch (objective) {
	case TutorialManager::Objective::DefeatEnemies:
		return "目標: 敵を倒す";
	case TutorialManager::Objective::JustGuard:
		return "目標: ジャストガードを成立させる";
	case TutorialManager::Objective::Critical:
		return "目標: 致命の一撃を決める";
	case TutorialManager::Objective::SwapCharacter:
		return "目標: 操作キャラを入れ替える";
	case TutorialManager::Objective::LockOn:
		return "目標: Z注目で敵を捉える";
	default:
		return std::string();
	}
}

} // namespace

TutorialManager::Objective TutorialManager::ParseObjective(const std::string& name) {
	if (name == "DefeatEnemies") {
		return Objective::DefeatEnemies;
	}
	if (name == "JustGuard") {
		return Objective::JustGuard;
	}
	if (name == "Critical") {
		return Objective::Critical;
	}
	if (name == "SwapCharacter") {
		return Objective::SwapCharacter;
	}
	if (name == "LockOn") {
		return Objective::LockOn;
	}
	return Objective::None;
}

void TutorialManager::OnPlayStart() {
	// Playインスタンスは使い回されるので、非シリアライズの状態と時間スケールは必ず戻す。
	showing_ = false;
	pendingObjective_ = Objective::None;
	pendingRequiredCount_ = 1;
	activeObjective_ = Objective::None;
	objectiveRequiredCount_ = 1;
	objectiveBaseCount_ = 0;
	completedObjectives_ = 0;
	bannerTimer_ = 0.0f;
	bannerText_.clear();
	Time::SetTimeScale(1.0f);

	// **回数はこのシーンぶんだけ数える。** 掲示板はシーンを跨いで残るので、
	// ここで戻さないとボス戦で稼いだジャストガードの回数で課題が即達成になる。
	GameEvents::ResetCounters();
	GameEvents::ClearFailure();

	SetObjectActive(popupName_, false); // エディタで開いたまま保存されていても事故らない。
	SetObjectActive(objectiveRootName_, false);
}

void TutorialManager::RegisterInvokableMethods(InvokableMethodRegistry& registry) {
	registry.Add("Close", [this]() { Close(); });
}

TutorialManager* TutorialManager::FindInScene(Scene* scene) {
	if (!scene) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (TutorialManager* manager = object->GetComponent<TutorialManager>()) {
			return manager;
		}
	}
	return nullptr;
}

void TutorialManager::SetText(const std::string& objectName, const std::string& text) {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || objectName.empty()) {
		return;
	}
	GameObject* object = scene->FindGameObjectByName(objectName);
	if (!object) {
		return;
	}
	if (TextComponent* label = object->GetComponent<TextComponent>()) {
		label->SetText(text);
	}
}

void TutorialManager::SetObjectActive(const std::string& objectName, bool active) {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || objectName.empty()) {
		return;
	}
	if (GameObject* object = scene->FindGameObjectByName(objectName)) {
		object->SetActive(active);
	}
}

void TutorialManager::Show(const std::string& title, const std::vector<std::string>& lines, Objective objective, int requiredCount) {
	if (showing_) {
		return; // 連続でトリガーを踏んでも上書きしない。
	}
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene) {
		return;
	}
	GameObject* popup = scene->FindGameObjectByName(popupName_);
	if (!popup) {
		return; // ポップアップが置かれていないシーンでは黙って何もしない。
	}

	SetText(titleName_, title);
	for (int index = 0; index < bodyLineCount_; ++index) {
		const std::string objectName = bodyPrefix_ + std::to_string(index);
		// 行が足りない分は空文字で消す(前回の文面が残らないように)。
		SetText(objectName, (index < static_cast<int>(lines.size())) ? lines[index] : std::string());
	}

	// **課題は閉じてから始める。** 読んでいる間は時間が止まっているので、
	// ここで始めても数えようがない。
	pendingObjective_ = objective;
	pendingRequiredCount_ = (requiredCount > 0) ? requiredCount : 1;

	popup->SetActive(true);
	showing_ = true;
	// **読ませる間はゲームを止める。** UIとフェードはUnscaledで動くので操作は生きている。
	Time::SetTimeScale(0.0f);
}

void TutorialManager::Close() {
	if (!showing_) {
		return;
	}
	SetObjectActive(popupName_, false);
	showing_ = false;
	Time::SetTimeScale(1.0f);

	if (pendingObjective_ != Objective::None) {
		activeObjective_ = pendingObjective_;
		objectiveRequiredCount_ = pendingRequiredCount_;
		pendingObjective_ = Objective::None;
		BeginObjective();
	}
}

int TutorialManager::CountLivingEnemies() const {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene) {
		return 0;
	}
	// **数える中心は操作キャラ。** 区間ごとに置いた敵は最初から全部立っているので、
	// 範囲で切らないと「この先の敵まで全部倒せ」という課題になってしまう。
	GameObject* leader = PartyManager::FindLeaderInScene(scene, "");
	const bool hasCenter = (leader != nullptr);
	const Vector3 center = hasCenter ? leader->GetTransform().translation_ : Vector3{0.0f, 0.0f, 0.0f};
	const float radiusSq = enemyCountRadius_ * enemyCountRadius_;

	int count = 0;
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object || !object->IsActiveInHierarchy()) {
			continue;
		}
		IEnemy* enemy = object->GetComponent<IEnemy>();
		if (!enemy || !enemy->IsTargetable()) {
			continue;
		}
		if (hasCenter) {
			Vector3 diff = object->GetTransform().translation_ - center;
			diff.y = 0.0f;
			if (diff.x * diff.x + diff.z * diff.z > radiusSq) {
				continue;
			}
		}
		++count;
	}
	return count;
}

void TutorialManager::BeginObjective() {
	// 差分で数えるための基準を控える。**前の課題で積んだぶんを持ち越さない。**
	switch (activeObjective_) {
	case Objective::JustGuard:
		objectiveBaseCount_ = GameEvents::JustGuardCountRef();
		break;
	case Objective::Critical:
		objectiveBaseCount_ = GameEvents::CriticalCountRef();
		break;
	case Objective::SwapCharacter:
		objectiveBaseCount_ = GameEvents::SwapCountRef();
		break;
	case Objective::LockOn:
		objectiveBaseCount_ = GameEvents::LockOnCountRef();
		break;
	default:
		objectiveBaseCount_ = 0;
		break;
	}
	bannerTimer_ = 0.0f;
	bannerText_.clear();
	SetObjectActive(objectiveRootName_, true);
	SetText(objectiveTextName_, BuildObjectiveText());
}

bool TutorialManager::IsObjectiveSatisfied() const {
	switch (activeObjective_) {
	case Objective::DefeatEnemies:
		return CountLivingEnemies() <= 0;
	case Objective::JustGuard:
		return GameEvents::JustGuardCountRef() - objectiveBaseCount_ >= objectiveRequiredCount_;
	case Objective::Critical:
		return GameEvents::CriticalCountRef() - objectiveBaseCount_ >= objectiveRequiredCount_;
	case Objective::SwapCharacter:
		return GameEvents::SwapCountRef() - objectiveBaseCount_ >= objectiveRequiredCount_;
	case Objective::LockOn:
		return GameEvents::LockOnCountRef() - objectiveBaseCount_ >= objectiveRequiredCount_;
	default:
		return true;
	}
}

std::string TutorialManager::BuildObjectiveText() const {
	const std::string label = ObjectiveLabel(activeObjective_);
	if (label.empty()) {
		return std::string();
	}
	if (activeObjective_ == Objective::DefeatEnemies) {
		const int living = CountLivingEnemies();
		return label + "  (残り " + std::to_string(living) + ")";
	}
	if (objectiveRequiredCount_ <= 1) {
		return label;
	}
	int done = 0;
	switch (activeObjective_) {
	case Objective::JustGuard:
		done = GameEvents::JustGuardCountRef() - objectiveBaseCount_;
		break;
	case Objective::Critical:
		done = GameEvents::CriticalCountRef() - objectiveBaseCount_;
		break;
	case Objective::SwapCharacter:
		done = GameEvents::SwapCountRef() - objectiveBaseCount_;
		break;
	case Objective::LockOn:
		done = GameEvents::LockOnCountRef() - objectiveBaseCount_;
		break;
	default:
		break;
	}
	if (done < 0) {
		done = 0;
	}
	return label + "  (" + std::to_string(done) + " / " + std::to_string(objectiveRequiredCount_) + ")";
}

void TutorialManager::CompleteObjective() {
	activeObjective_ = Objective::None;
	++completedObjectives_;
	bannerText_ = "達成";
	bannerTimer_ = clearBannerSeconds_;
	SetText(objectiveTextName_, bannerText_);
}

void TutorialManager::NotifyExitBlocked() {
	// 出口で止められたことを伝える。**帯を出したままにはしない**ので、
	// 一定時間で通常の目標表示へ戻る。
	bannerText_ = "まだやることが残っている";
	bannerTimer_ = blockedBannerSeconds_;
	SetObjectActive(objectiveRootName_, true);
	SetText(objectiveTextName_, bannerText_);
}

void TutorialManager::Update() {
	// ポップアップで止めている間も自分は動く必要があるので、常に実時間で数える。
	const float deltaTime = Time::GetUnscaledDeltaTime();

	if (bannerTimer_ > 0.0f) {
		bannerTimer_ -= deltaTime;
		if (bannerTimer_ > 0.0f) {
			return; // 一時的な文言を出している間は目標を書き換えない。
		}
		bannerTimer_ = 0.0f;
		bannerText_.clear();
		if (activeObjective_ == Objective::None) {
			SetObjectActive(objectiveRootName_, false);
			SetText(objectiveTextName_, std::string());
			return;
		}
	}

	if (showing_ || activeObjective_ == Objective::None) {
		return;
	}

	if (IsObjectiveSatisfied()) {
		CompleteObjective();
		return;
	}
	// 残り数の表示を追従させる(倒した敵の数などが見えないと進んでいる実感が出ない)。
	SetText(objectiveTextName_, BuildObjectiveText());
}

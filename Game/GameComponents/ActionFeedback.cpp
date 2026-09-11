#include "ActionFeedback.h"

#include "GameAudio.h"
#include "GameEvents.h"

#include <components/TextComponent.h>

using namespace KujataEngine;

void ActionFeedback::OnPlayStart() {
	// Playインスタンスは使い回されるので、非シリアライズの状態は必ず戻す。
	showTimer_ = 0.0f;
	cooldownTimer_ = 0.0f;
	GameEvents::ClearFailure();
	ShowText(std::string());
}

void ActionFeedback::ShowText(const std::string& text) {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || textObjectName_.empty()) {
		return;
	}
	GameObject* object = scene->FindGameObjectByName(textObjectName_);
	if (!object) {
		return;
	}
	if (TextComponent* label = object->GetComponent<TextComponent>()) {
		label->SetText(text);
	}
}

void ActionFeedback::Update() {
	// **必ず実時間。** ポーズやヒットストップで止まっている間に文字が居座らないように。
	const float deltaTime = Time::GetUnscaledDeltaTime();

	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= deltaTime;
	}
	if (showTimer_ > 0.0f) {
		showTimer_ -= deltaTime;
		if (showTimer_ <= 0.0f) {
			showTimer_ = 0.0f;
			ShowText(std::string());
		}
	}

	const GameEvents::Failure failure = GameEvents::LastFailureRef();
	if (failure == GameEvents::Failure::None) {
		return;
	}
	// **報告は必ず読み捨てる。** 残したままにすると、クールダウンが明けた瞬間に
	// 古い理由がもう一度出てしまう。
	GameEvents::ClearFailure();

	if (cooldownTimer_ > 0.0f) {
		return;
	}
	const std::string text = GameEvents::FailureText(failure);
	if (text.empty()) {
		return;
	}
	ShowText(text);
	showTimer_ = showSeconds_;
	cooldownTimer_ = cooldownSeconds_;
	if (playSound_) {
		// **理由ごとに音を変える。** 文字を読まなくても「何が足りないのか」が分かるようにする。
		GameAudio::PlaySe(failure == GameEvents::Failure::NoStamina ? GameAudio::Se::StaminaEmpty : GameAudio::Se::UiCancel);
	}
}

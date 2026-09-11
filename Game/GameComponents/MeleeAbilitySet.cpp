#include "MeleeAbilitySet.h"
#include "GameEvents.h"
#include "Player.h"
#include "CharacterMotor.h"
#include "StaminaComponent.h"
#include "WeaponComponent.h"

using namespace KujataEngine;

namespace {
constexpr int kChargeIndex = 4;
constexpr int kMaxComboSteps = 3;

/// <summary>
/// スタミナ切れで技が出せなかったことを掲示する。**操作中のキャラのときだけ。**
/// この技セットはAI相方も同じものを使うので、門番を付けないと相方の息切れで画面に文字が出る。
/// </summary>
void ReportOutOfStamina(KujataEngine::GameObject* owner) {
	if (Player::IsControlledObject(owner)) {
		GameEvents::ReportFailure(GameEvents::Failure::NoStamina);
	}
}
} // namespace

void MeleeAbilitySet::OnPlayStart() {
	motor_ = GetComponent<CharacterMotor>();
	stamina_ = GetComponent<StaminaComponent>();
	animator_ = GetComponentInChildren<AnimatorComponent>();
	weapon_ = GetComponentInChildren<WeaponComponent>();
	comboIndex_ = 0;
	attackTimer_ = 0.0f;
	clipLength_ = 0.0f;
	nextQueued_ = false;
	recoveryTimer_ = 0.0f;
}

void MeleeAbilitySet::Update() {
	float deltaTime = Time::GetDeltaTime();

	if (recoveryTimer_ > 0.0f) {
		recoveryTimer_ -= deltaTime;
		if (recoveryTimer_ < 0.0f) {
			recoveryTimer_ = 0.0f;
		}
	}

	if (comboIndex_ == 0) {
		return;
	}

	// 被弾のけぞり・回避で振りが中断されたら(Animatorが別クリップへ移る)、攻撃状態も畳む。
	if (motor_ && motor_->IsActionLocked()) {
		comboIndex_ = 0;
		nextQueued_ = false;
		return;
	}

	attackTimer_ += deltaTime;

	// 先行入力があり、受付窓に入ったら次段へ。
	if (nextQueued_ && comboIndex_ < kMaxComboSteps && attackTimer_ >= clipLength_ * comboWindowFrom_) {
		nextQueued_ = false;
		if (StartStep(comboIndex_ + 1)) {
			return;
		}
	}

	// クリップが終わったら攻撃終了→隙へ。
	bool clipEnded = !animator_ || !animator_->IsPlaying() || (clipLength_ > 0.0f && attackTimer_ >= clipLength_);
	if (clipEnded) {
		FinishAttack(comboIndex_);
	}
}

bool MeleeAbilitySet::TryUse(int slot) {
	if (!animator_) {
		return false;
	}
	if (motor_ && motor_->IsActionLocked()) {
		return false;
	}
	if (recoveryTimer_ > 0.0f) {
		return false;
	}

	if (slot == 1) {
		// 溜め: 振り中は出せない。
		if (comboIndex_ != 0) {
			return false;
		}
		return StartCharge();
	}
	if (slot != 0) {
		return false;
	}

	// 通常: 非攻撃なら1段目。振り中なら次段を予約(窓に入った時点で繋がる)。
	if (comboIndex_ == 0) {
		return StartStep(1);
	}
	if (comboIndex_ >= 1 && comboIndex_ < kMaxComboSteps && !ClipForStep(comboIndex_ + 1).empty()) {
		nextQueued_ = true;
		return true;
	}
	return false;
}

bool MeleeAbilitySet::IsBusy() const {
	return comboIndex_ != 0 || recoveryTimer_ > 0.0f;
}

const std::string& MeleeAbilitySet::ClipForStep(int step) const {
	switch (step) {
	case 1:
		return attackClip1_;
	case 2:
		return attackClip2_;
	case 3:
		return attackClip3_;
	default:
		return chargeClipName_;
	}
}

bool MeleeAbilitySet::StartStep(int step) {
	const std::string& clipName = ClipForStep(step);
	if (clipName.empty() || !animator_) {
		return false;
	}
	if (stamina_ && !stamina_->CanUse()) {
		ReportOutOfStamina(owner_);
		return false;
	}
	if (!animator_->PlayByName(clipName)) {
		return false;
	}

	if (stamina_) {
		stamina_->ConsumePercent(staminaCostNormal_);
	}
	if (weapon_) {
		weapon_->SetSwingParams(comboDamage_, comboPoise_);
	}
	comboIndex_ = step;
	attackTimer_ = 0.0f;
	clipLength_ = animator_->GetClip().GetDuration();
	nextQueued_ = false;
	return true;
}

bool MeleeAbilitySet::StartCharge() {
	if (chargeClipName_.empty() || !animator_) {
		return false;
	}
	if (stamina_ && !stamina_->CanUse()) {
		ReportOutOfStamina(owner_);
		return false;
	}
	if (!animator_->PlayByName(chargeClipName_)) {
		return false;
	}

	if (stamina_) {
		stamina_->ConsumePercent(staminaCostCharge_);
	}
	if (weapon_) {
		weapon_->SetSwingParams(chargeDamage_, chargePoise_);
	}
	comboIndex_ = kChargeIndex;
	attackTimer_ = 0.0f;
	clipLength_ = animator_->GetClip().GetDuration();
	nextQueued_ = false;
	return true;
}

float MeleeAbilitySet::RecoveryForStep(int endedStep) const {
	// 溜めは重く、締め(3段目)は中くらい、途中で止めたら軽い。
	// **隙の重さをどこに置くかがそのまま武器の性格になる。** 大槌は溜め側に寄せている。
	if (endedStep == kChargeIndex) {
		return recoveryChargeSeconds_;
	}
	if (endedStep >= kMaxComboSteps) {
		return recoverySeconds_;
	}
	return recoveryLightSeconds_;
}

void MeleeAbilitySet::FinishAttack(int endedStep) {
	comboIndex_ = 0;
	nextQueued_ = false;
	attackTimer_ = 0.0f;
	recoveryTimer_ = RecoveryForStep(endedStep);
}

#include "SwordGuard.h"
#include "GameEvents.h"
#include "Player.h"
#include "GameAudio.h"
#include "CharacterMotor.h"
#include "EnemyHealth.h"
#include "IAbilitySet.h"
#include "StaminaComponent.h"

#include <scene/MovementUtil.h>

#include <cmath>
#include <numbers>

using namespace KujataEngine;

void SwordGuard::OnPlayStart() {
	motor_ = GetComponent<CharacterMotor>();
	stamina_ = GetComponent<StaminaComponent>();
	abilitySet_ = GetComponent<IAbilitySet>();
	animator_ = GetComponentInChildren<AnimatorComponent>();
	guarding_ = false;
	guardTime_ = 0.0f;
	needRelease_ = false;
}

void SwordGuard::Update() {
	if (!guarding_) {
		return;
	}
	guardTime_ += Time::GetDeltaTime();

	// 被弾硬直・回避に入ったら構えは解ける(離すまで構え直せない)。
	if (motor_ && motor_->IsActionLocked()) {
		guarding_ = false;
		needRelease_ = true;
	}
}

void SwordGuard::SetGuardInput(bool pressed) {
	if (!pressed) {
		needRelease_ = false;
		if (guarding_) {
			EndGuard();
		}
		return;
	}
	if (guarding_ || needRelease_) {
		return;
	}
	// 技のモーション中・行動不能中は構えられない。
	if (motor_ && motor_->IsActionLocked()) {
		return;
	}
	if (abilitySet_ && abilitySet_->IsBusy()) {
		return;
	}
	BeginGuard();
}

void SwordGuard::BeginGuard() {
	guarding_ = true;
	guardTime_ = 0.0f;
	if (animator_ && !guardClipName_.empty()) {
		animator_->PlayByName(guardClipName_);
	}
}

void SwordGuard::EndGuard() {
	guarding_ = false;
	// 構えクリップを再生中なら止めて元の姿勢へ戻す(別のクリップへ移っていれば触らない)。
	if (animator_ && !guardClipName_.empty() && animator_->IsPlaying() && animator_->GetClip().name == guardClipName_) {
		animator_->Stop();
	}
}

bool SwordGuard::IsFrontal(const HitInfo& hit) const {
	if (!hit.attacker || !owner_ || guardAngle_ >= 180.0f) {
		return true;
	}
	Vector3 toAttacker = hit.attacker->GetTransform().translation_ - owner_->GetTransform().translation_;
	toAttacker.y = 0.0f;
	float length = Length(toAttacker);
	if (length < 0.0001f) {
		return true;
	}
	Vector3 forward = MovementUtil::GetForward(*owner_);
	forward.y = 0.0f;
	float forwardLength = Length(forward);
	if (forwardLength < 0.0001f) {
		return true;
	}
	float cosAngle = (toAttacker.x * forward.x + toAttacker.z * forward.z) / (length * forwardLength);
	return cosAngle >= std::cos(guardAngle_ * std::numbers::pi_v<float> / 180.0f);
}

GuardResult SwordGuard::Mitigate(const HitInfo& hit) {
	GuardResult result;
	if (!guarding_ || !IsFrontal(hit)) {
		return result;
	}

	bool physical = (hit.type == DamageType::Physical);

	// ジャストガード(物理のみ): 無傷・消費なし・敵をのけぞらせ体勢崩し値を与える。
	if (physical && guardTime_ <= justGuardWindow_) {
		result.blocked = true;
		result.justGuard = true;
		result.damageScale = 0.0f;
		result.negateKnockback = true;
		// **成立したことが分からないと練習できない技**なので、専用音を一番大きく鳴らす。
		GameAudio::PlaySe(GameAudio::Se::JustGuard);
		// チュートリアルの課題判定用。**操作中のキャラのぶんだけ数える**
		// (AI相方が偶然成立させたぶんで課題が終わってしまわないように)。
		if (Player::IsControlledObject(owner_)) {
			++GameEvents::JustGuardCountRef();
		}
		if (hit.attacker) {
			if (EnemyHealth* enemyHealth = hit.attacker->GetComponentInParent<EnemyHealth>()) {
				enemyHealth->Flinch();
				enemyHealth->AddPoise(justGuardPoise_);
			}
		}
		return result;
	}

	// 通常ガード: 物理は無効化、魔法は半減。どちらもスタミナを消費する。
	result.blocked = true;
	result.negateKnockback = true;
	result.damageScale = physical ? 0.0f : magicDamageScale_;
	GameAudio::PlaySe(GameAudio::Se::Guard);

	bool depleted = stamina_ ? stamina_->ConsumePercent(guardCost_) : false;
	if (depleted) {
		Break(hit);
	}
	return result;
}

void SwordGuard::Break(const HitInfo& hit) {
	guarding_ = false;
	needRelease_ = true;
	GameAudio::PlaySe(GameAudio::Se::GuardBreak);

	// 後ろへ弾かれて硬直。のけぞりクリップの代わりにガードブレイククリップを出す。
	if (motor_) {
		Vector3 direction = hit.knockbackVelocity;
		direction.y = 0.0f;
		float length = Length(direction);
		if (length > 0.0001f) {
			direction = direction / length;
		} else {
			direction = MovementUtil::GetForward(*owner_) * -1.0f;
		}
		motor_->ApplyKnockback(direction * guardBreakKnockback_, guardBreakStun_);
	}
	if (animator_ && !guardBreakClipName_.empty()) {
		animator_->PlayByName(guardBreakClipName_);
	}
}

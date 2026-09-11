#include "BarrierGuard.h"
#include "GameAudio.h"
#include "GameEvents.h"
#include "Player.h"
#include "CharacterMotor.h"
#include "IAbilitySet.h"
#include "MagicAbilitySet.h"
#include "StaminaComponent.h"

#include <Editor/PrefabAsset.h>

using namespace KujataEngine;

void BarrierGuard::OnPlayStart() {
	motor_ = GetComponent<CharacterMotor>();
	stamina_ = GetComponent<StaminaComponent>();
	abilitySet_ = GetComponent<IAbilitySet>();
	magic_ = GetComponent<MagicAbilitySet>();
	active_ = false;
	activeTime_ = 0.0f;
	needRelease_ = false;
	// 見た目はPlayインスタンスごとに作り直す(前回Playのポインタは無効)。
	visual_ = nullptr;
	visualTried_ = false;
}

void BarrierGuard::Update() {
	if (active_) {
		activeTime_ += Time::GetDeltaTime();

		// 被弾硬直・回避に入ったら閉じる(離すまで展開し直せない)。
		if (motor_ && motor_->IsActionLocked()) {
			Close();
			needRelease_ = true;
		} else if (stamina_ && !stamina_->Drain(stamina_->GetMaxStamina() * drainPerSecond_ * 0.01f)) {
			// スタミナ切れで自動的に閉じる(スタンは無い)。
			Close();
			needRelease_ = true;
		}
	}
	UpdateVisual();
}

void BarrierGuard::SetGuardInput(bool pressed) {
	if (!pressed) {
		needRelease_ = false;
		if (active_) {
			Close();
		}
		return;
	}
	if (active_ || needRelease_) {
		return;
	}
	if (motor_ && motor_->IsActionLocked()) {
		return;
	}
	if (abilitySet_ && abilitySet_->IsBusy()) {
		return;
	}
	if (stamina_ && stamina_->IsEmpty()) {
		return;
	}
	Open();
}

void BarrierGuard::Open() {
	active_ = true;
	activeTime_ = 0.0f;
}

void BarrierGuard::Close() {
	active_ = false;
}

Vector3 BarrierGuard::GetCenter() const {
	// 守る相手が指定されていればそこへ張る(離れたまま相方だけを包むため)。
	// 相手が倒れている/消えている場合は自分中心へ黙って戻す。
	const GameObject* anchor = owner_;
	if (protectTarget_ && protectTarget_->IsActiveInHierarchy()) {
		anchor = protectTarget_;
	}
	if (!anchor) {
		return {0.0f, 0.0f, 0.0f};
	}
	return anchor->GetTransform().translation_ + Vector3{0.0f, centerHeight_, 0.0f};
}

bool BarrierGuard::CoversPosition(const Vector3& worldPosition) const {
	if (!active_) {
		return false;
	}
	// 相方は足元の位置で判定するので、球の中心高さぶんは無視して水平+少しの高さで見る。
	Vector3 diff = worldPosition - GetCenter();
	diff.y *= 0.5f;
	return Length(diff) <= radius_;
}

GuardResult BarrierGuard::Mitigate(const HitInfo& hit) {
	if (!active_) {
		return GuardResult{};
	}
	// **球を相方へ預けている間は、自分が球の外なら守られない。**
	// ここを素通しにすると「離れた場所へバリアを張ったのに自分も無傷」という
	// 都合の良すぎる挙動になり、間合いを選ぶ意味が消える。
	if (protectTarget_ && owner_ && !CoversPosition(owner_->GetTransform().translation_)) {
		return GuardResult{};
	}
	return MitigateCommon(hit);
}

GuardResult BarrierGuard::MitigateForAlly(const HitInfo& hit) {
	if (!active_) {
		return GuardResult{};
	}
	return MitigateCommon(hit);
}

GuardResult BarrierGuard::MitigateCommon(const HitInfo& hit) {
	GuardResult result;
	if (hit.type == DamageType::Magic) {
		result.blocked = true;
		result.damageScale = 0.0f;
		result.negateKnockback = true;
		// ジャストガード: 魔法のつぶてで攻撃元へ自動反撃。
		if (activeTime_ <= justGuardWindow_ && magic_) {
			result.justGuard = true;
			// **剣士のジャストガードと同じ音**にする。同じ操作の成功なので手応えを揃える。
			GameAudio::PlaySe(GameAudio::Se::JustGuard);
			magic_->FirePebble(hit.attacker);
			// チュートリアルの課題判定用。**操作中のキャラのぶんだけ数える**
			// (AI相方が偶然成立させたぶんで課題が終わってしまわないように)。
			if (Player::IsControlledObject(owner_)) {
				++GameEvents::JustGuardCountRef();
			}
		} else {
			// 通常の受け止め。**無効化できたことが分かる音**を出さないと、
			// バリアが効いているのか素通しなのか見分けが付かない。
			GameAudio::PlaySe(GameAudio::Se::BarrierHit);
		}
		return result;
	}

	// 物理: 半減。ノックバックはそのまま受ける。
	result.blocked = true;
	result.damageScale = physicalDamageScale_;
	result.negateKnockback = false;
	return result;
}

void BarrierGuard::UpdateVisual() {
	if (!active_) {
		if (visual_) {
			visual_->SetActive(false);
		}
		return;
	}

	if (!visual_ && !visualTried_) {
		visualTried_ = true;
		Scene* scene = owner_ ? owner_->GetScene() : nullptr;
		if (scene && !barrierPrefabPath_.empty()) {
			PrefabAsset::InstantiateResult result = PrefabAsset::Instantiate(*scene, barrierPrefabPath_, false);
			if (result.succeeded && result.rootObject) {
				visual_ = result.rootObject;
			} else {
				Logger::Log("[BarrierGuard] barrier prefab load failed (" + barrierPrefabPath_ + "): " + result.message);
			}
		}
	}
	if (!visual_) {
		return;
	}

	visual_->SetActive(true);
	WorldTransform& transform = visual_->GetTransform();
	transform.translation_ = GetCenter();
	transform.scale_ = {radius_, radius_, radius_};
}

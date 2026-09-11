#include "PlayerHealth.h"
#include "GameAudio.h"
#include "GameFx.h"
#include <components/AnimatorComponent.h>
#include <components/RigidbodyComponent.h>

#include <algorithm>
#include "CharacterMotor.h"
#include "IGuard.h"

#include <algorithm>

using namespace KujataEngine;

void PlayerHealth::Initialize() {
	health_ = maxHealth_;
}

void PlayerHealth::OnPlayStart() {
	// FXのプールは前回Playのポインタを抱えている。**触る前に必ず捨てる。**
	GameFx::ResetPools();
	if (deathPyre_) {
		deathPyre_ = nullptr;
	}
	guard_ = GetComponent<IGuard>();
	motor_ = GetComponent<CharacterMotor>();
	invincible_ = false;

	// **死亡状態を必ず解いてからPlayを始める。**
	// Playインスタンスはコンポーネントを作り直さず使い回すため、ここで戻さないと
	// 前回のPlayで倒れたまま次のPlayが始まり、入力を受け付けない(＝動かせない)。
	// カメラは別コンポーネントなので回り続け、「移動だけ効かない」という形で表面化する。
	dead_ = false;
	reviveTimer_ = 0.0f;
	if (health_ <= 0.0f) {
		health_ = maxHealth_;
	}

	// 死亡時にキネマティックへ落とした体を動的へ戻す。ここも戻さないと押しても動かない。
	if (owner_ && wasDynamicBeforeDeath_) {
		if (KujataEngine::RigidbodyComponent* rigidbody = owner_->GetComponent<KujataEngine::RigidbodyComponent>()) {
			rigidbody->SetStatic(false);
			rigidbody->SetVelocity({0.0f, 0.0f, 0.0f});
		}
	}
	wasDynamicBeforeDeath_ = true;
}

bool PlayerHealth::ReceiveHit(const HitInfo& hit) {
	if (!IsAlive() || !owner_) {
		return false;
	}

	// 無敵中(回避中など)はダメージもノックバックも入らない。
	if (invincible_) {
		return false;
	}

	// 自分のガードで軽減する(ガード中でなければ素通しが返る)。
	GuardResult result;
	if (guard_) {
		result = guard_->Mitigate(hit);
	}

	// 相方の範囲ガード(術師のバリア)の中にいれば、そちらでも軽減する。
	// 自分のガードが成立していないときだけ見る(二重軽減は避ける)。
	if (!result.blocked) {
		if (Scene* scene = owner_->GetScene()) {
			const Vector3& myPosition = owner_->GetTransform().translation_;
			for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
				if (!object || object.get() == owner_ || !object->IsActiveInHierarchy()) {
					continue;
				}
				IGuard* allyGuard = object->GetComponent<IGuard>();
				if (!allyGuard || !allyGuard->IsEnabled() || !allyGuard->IsGuarding()) {
					continue;
				}
				if (!allyGuard->CoversPosition(myPosition)) {
					continue;
				}
				result = allyGuard->MitigateForAlly(hit);
				break;
			}
		}
	}

	float damage = hit.damage * std::max(0.0f, result.damageScale);
	if (damage > 0.0f) {
		TakeDamage(damage);
	}

	// ノックバック: ガード成立時は打ち消す。体(CharacterMotor)へ直接与えるので、
	// 頭脳が入力かAIかに関わらず同じ挙動になる。
	if (!result.negateKnockback && motor_) {
		motor_->ApplyKnockback(hit.knockbackVelocity, hit.stunDuration);
	}
	return true;
}

void PlayerHealth::TakeDamage(float damage) {
	if (!IsAlive()) {
		return;
	}

	// 無敵中(回避中など)はダメージを受けない。
	if (invincible_) {
		return;
	}

	health_ = std::max(0.0f, health_ - damage);
	GameAudio::PlaySe(GameAudio::Se::PlayerDamage);

	if (onHealthChanged_) {
		onHealthChanged_(health_);
	}

	if (health_ <= 0 && !dead_) {
		EnterDeath();
	}
}

void PlayerHealth::EnterDeath() {
	dead_ = true;
	reviveTimer_ = 0.0f;
	// **死んだら無敵にする。** 倒れている間ずっと殴られ続けると、時間で起き上がる意味が無くなる。
	invincible_ = true;
	// 倒れたことと、そこから復帰待ちに入ったことを音でも出す。
	GameAudio::PlaySe(GameAudio::Se::ReviveStart);

	// **押されて動かないように、体をキネマティック(Is Static)へ落とす。**
	// コライダーは残す(倒れた体をすり抜けさせないため)。
	// 衝突応答は「Rigidbodyがあり、かつIs Staticでない」ものだけを押し出す作りなので、
	// Staticにすれば当たり判定を保ったまま無限質量として扱われ、敵に押し込まれなくなる。
	if (motor_) {
		motor_->StopAllMotion();
	}
	if (owner_) {
		if (KujataEngine::RigidbodyComponent* rigidbody = owner_->GetComponent<KujataEngine::RigidbodyComponent>()) {
			wasDynamicBeforeDeath_ = !rigidbody->IsStatic();
			rigidbody->SetVelocity({0.0f, 0.0f, 0.0f});
			rigidbody->SetStatic(true);
		}
	}

	// 倒れた場所に魂の炎を残す。一瞬のバーストと違い、蘇生へ向かうための目印として機能する。
	if (owner_ && !deathPyre_) {
		deathPyre_ = GameFx::Ignite(owner_->GetScene(), GameFx::Prefab::kSoulPyre,
		    owner_->GetTransform().translation_, &GameFx::kSoulColor);
	}
	if (!deathClipName_.empty()) {
		if (KujataEngine::AnimatorComponent* animator = GetComponentInChildren<KujataEngine::AnimatorComponent>()) {
			animator->PlayByName(deathClipName_.c_str());
		}
	}
	if (onDeath_) {
		onDeath_();
	}
}

void PlayerHealth::Update() {
	// **倒れたら時間で起き上がる。**
	// 「相方を叩いて起こす」方式は、起こしに行く側が無防備に棒立ちになる時間が長く、
	// その隙に二人目も落ちて全滅、という流れを量産していた。
	if (!dead_) {
		return;
	}
	reviveTimer_ += Time::GetDeltaTime();
	if (reviveTimer_ >= (std::max)(reviveSeconds_, 0.1f)) {
		Revive();
	}
}

float PlayerHealth::GetReviveProgress() const {
	if (!dead_) {
		return 0.0f;
	}
	return std::clamp(reviveTimer_ / (std::max)(reviveSeconds_, 0.1f), 0.0f, 1.0f);
}

void PlayerHealth::Revive() {
	if (!dead_) {
		return;
	}
	dead_ = false;
	invincible_ = false;
	health_ = maxHealth_ * std::clamp(reviveHealthPercent_ * 0.01f, 0.01f, 1.0f);
	// **起き上がった瞬間を音で知らせる。** 画面から目を離していても復帰が分かる。
	GameAudio::PlaySe(GameAudio::Se::ReviveComplete);

	// 体を元へ戻す。**死ぬ前が動的だったかを覚えておく**のは、
	// 元からキネマティックに置かれた個体を蘇生で勝手に動かさないため。
	if (owner_ && wasDynamicBeforeDeath_) {
		if (KujataEngine::RigidbodyComponent* rigidbody = owner_->GetComponent<KujataEngine::RigidbodyComponent>()) {
			rigidbody->SetStatic(false);
			rigidbody->SetVelocity({0.0f, 0.0f, 0.0f});
		}
	}

	// **炎は消す責任がこちら側にある。** 消し忘れると死亡地点の目印が残り続ける。
	GameFx::Extinguish(deathPyre_);
	deathPyre_ = nullptr;

	if (onHealthChanged_) {
		onHealthChanged_(health_);
	}
}

float PlayerHealth::GetHealthPercent() const {
	if (maxHealth_ <= 0) {
		return 0.0f;
	}
	return health_ / maxHealth_;
}

bool PlayerHealth::IsAlive() const {
	return health_ > 0;
}

void PlayerHealth::SetOnHealthChanged(std::function<void(float)> cb) {
	onHealthChanged_ = cb;
}

void PlayerHealth::SetOnDeath(std::function<void()> cb) {
	onDeath_ = cb;
}

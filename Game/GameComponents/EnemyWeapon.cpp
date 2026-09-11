#include "EnemyWeapon.h"
#include "HitInfo.h"
#include "PlayerHealth.h"

using namespace KujataEngine;

namespace {

// 武器の最上位親(敵本体)を返す。ノックバック方向の基準にする。
GameObject* GetRootObject(GameObject* object) {
	while (object && object->GetParent()) {
		object = object->GetParent();
	}
	return object;
}

} // namespace

void EnemyWeapon::Initialize() {
}

void EnemyWeapon::Update() {
	// attackがOFF→ON(新しい攻撃振りの開始)になった瞬間はヒット履歴をリセットし、即ヒットできるようにする。
	if (attack_ && !prevAttack_) {
		hitCooldowns_.clear();
	}
	prevAttack_ = attack_;

	// 対象ごとの再ヒットクールダウンを進める。
	float deltaTime = Time::GetDeltaTime();
	for (auto it = hitCooldowns_.begin(); it != hitCooldowns_.end();) {
		it->second -= deltaTime;
		if (it->second <= 0.0f) {
			it = hitCooldowns_.erase(it);
		} else {
			++it;
		}
	}
}

void EnemyWeapon::OnTriggerStay(KujataEngine::ColliderComponent* other) {
	if (!other || !attack_) {
		return;
	}

	KujataEngine::GameObject* otherObj = other->GetOwner();
	if (!otherObj) {
		return;
	}

	// 再ヒット間隔中はダメージを与えない(アニメーション中でも間隔が空けば何度でも当たる)。
	if (hitCooldowns_.count(otherObj) > 0) {
		return;
	}

	// 無敵中はヒット不成立(履歴にも残さず、無敵が切れたら即座に当たり得る)。
	if (ApplyDamageToPlayer(otherObj)) {
		hitCooldowns_[otherObj] = hitInterval_;
	}
}

bool EnemyWeapon::ApplyDamageToPlayer(KujataEngine::GameObject* target) {
	// 敵の武器なのでダメージ対象はPlayer(PlayerHealth持ち)のみ。
	auto health = target->GetComponent<PlayerHealth>();
	if (!health) {
		return false;
	}

	// ノックバック方向: 敵本体→プレイヤーの水平方向。
	GameObject* enemyRoot = GetRootObject(GetOwner());
	Vector3 direction = {0.0f, 0.0f, 1.0f};
	if (enemyRoot) {
		Vector3 toTarget = target->GetTransform().translation_ - enemyRoot->GetTransform().translation_;
		toTarget.y = 0.0f;
		float length = Length(toTarget);
		if (length > 0.0001f) {
			direction = toTarget / length;
		}
	}

	// ヒット情報を組み立てて受け側へ渡す。ガードによる軽減・無敵判定・ノックバックの適用は
	// PlayerHealth::ReceiveHit が一手に引き受ける(武器側は「何を当てたか」しか知らない)。
	HitInfo hit;
	hit.damage = damageValue_;
	hit.type = magicDamage_ ? DamageType::Magic : DamageType::Physical;
	hit.knockbackVelocity = direction * knockback_;
	hit.stunDuration = stunDuration_;
	hit.attacker = enemyRoot;
	return health->ReceiveHit(hit);
}

#include "WeaponComponent.h"
#include "EnemyHealth.h"
#include "CharacterMotor.h"
#include "PlayerHealth.h"

void WeaponComponent::Initialize() {
}

void WeaponComponent::Update() {
	// attackがOFF→ON(新しい攻撃振りの開始)になった瞬間だけヒット履歴をクリアする。
	// これにより「1振りで敵1体につき1回だけ」ダメージが入る(多段ヒット防止)。
	if (attack_ && !prevAttack_) {
		hitThisSwing_.clear();
	}
	prevAttack_ = attack_;
}

void WeaponComponent::OnTriggerStay(KujataEngine::ColliderComponent* other) {
	if (!other || !attack_) {
		return;
	}

	KujataEngine::GameObject* otherObj = other->GetOwner();
	if (!otherObj) {
		return;
	}

	ApplyDamageToEnemy(otherObj);
}

void WeaponComponent::ApplyDamageToEnemy(KujataEngine::GameObject* enemy) {
	// 当たったオブジェクト自身から親へ遡ってHPを探す。
	// ガーディアンのように体の一部(脚など)へ当たり判定を分けている敵では、
	// HPはルートにあり当たったオブジェクトには無いため、自身だけを見ると素通りしてしまう。
	// GetComponentInParentは自身から始まるので、単体構成の敵はこれまでどおり動く。
	// **味方には何も起きない**(誤爆でHPを削らない)。倒れている相方も同じで、
	// 蘇生は叩いて起こすのではなく倒れてからの経過時間で進む([[death-and-revive]])。
	if (enemy->GetComponentInParent<PlayerHealth>()) {
		return;
	}

	auto health = enemy->GetComponentInParent<EnemyHealth>();
	if (!health) {
		return;
	}

	// 重複排除は「当たったコライダー」ではなく「ダメージを受ける実体」単位で行う。
	// コライダー単位にすると、1体で複数のコライダーを持つ敵(ガーディアンの脚は
	// ボーンごとに当たり判定を持つ)では1振りで部位の数だけダメージが入ってしまう。
	KujataEngine::GameObject* target = health->GetOwner();
	if (!target || hitThisSwing_.count(target) > 0) {
		return;
	}

	// **ヘイトの主体は武器ではなく振っている本人。**
	// この武器は手→腕→モデル→キャラのルート、と親をたどった先にいるキャラのもの。
	// CharacterMotorはキャラのルートにしか付かないので、それを目印に本人を特定する。
	KujataEngine::GameObject* attacker = nullptr;
	if (CharacterMotor* motor = GetComponentInParent<CharacterMotor>()) {
		attacker = motor->GetOwner();
	}
	health->TakeDamage(damageValue_, poiseDamage_, attacker);
	hitThisSwing_.insert(target);
}

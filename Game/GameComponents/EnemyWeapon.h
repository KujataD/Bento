#pragma once

#include <KujataEngine.h>
#include <unordered_map>

class EnemyWeapon : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "EnemyWeapon"; }

	void Initialize() override;
	void Update() override;
	void OnTriggerStay(KujataEngine::ColliderComponent* other) override;

	/// <summary>
	/// 攻撃判定のON/OFF。通常はアニメーションクリップのboolチャンネルで駆動するが、
	/// 攻撃をコードで組み立てる場合(ガーディアンの踏みつけ等)はここから直接切り替える。
	/// </summary>
	void SetAttack(bool attack) { attack_ = attack; }
	bool IsAttacking() const { return attack_; }

	/// <summary>
	/// ヒット性能をコードから上書きする(ガーディアンの衝撃刃のように、1つの器を複数の攻撃で使い回す場合)。
	/// Inspectorの値は「上書きされなかったときの既定」になる。
	/// </summary>
	void SetHitParams(float damage, float knockback, float stunDuration, float hitInterval) {
		damageValue_ = damage;
		knockback_ = knockback;
		stunDuration_ = stunDuration;
		hitInterval_ = hitInterval;
	}

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		// attack_はbool型アニメーションチャンネル。キーフレームで攻撃判定をON/OFFする(ため中はOFF)。
		KUJATA_REGISTER_BOOL(attack_);
		KUJATA_REGISTER_FLOAT(damageValue_, 1.0f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT(knockback_, 0.1f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT(hitInterval_, 0.05f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT(stunDuration_, 0.05f, 0.0f, 0.0f);
		KUJATA_REGISTER_BOOL_NAMED_TIP(magicDamage_, "Magic Damage",
		    "ONならこの攻撃は「魔法(遠距離)」扱い。OFFは物理。\n"
		    "剣士のガードは物理を完全に防ぎ魔法を半減、術師のバリアは魔法を完全に防ぎ物理を半減する。\n"
		    "ガーディアンのビームだけON、脚・ハンマーはOFF。");
	}

	KUJATA_FIELD_BOOL(attack_, false);
	// 魔法(遠距離)攻撃か。ガードの得意/不得意の判定に使う。
	KUJATA_FIELD_BOOL(magicDamage_, false);
	KUJATA_FIELD_FLOAT(damageValue_, 10);
	// ノックバックの初速[unit/s]。
	KUJATA_FIELD_FLOAT(knockback_, 5);
	// 同一対象への再ヒット間隔[s]。1回のアニメーション中でも、この間隔ごとに当たるたびダメージが入る。
	KUJATA_FIELD_FLOAT(hitInterval_, 0.5f);
	// ヒット時にプレイヤーが動けなくなる時間[s]。
	KUJATA_FIELD_FLOAT(stunDuration_, 0.6f);

	// 対象ごとの再ヒットまでの残り時間。OnTriggerStayは毎フレーム呼ばれるため、
	// これが無いと接触中に毎フレームダメージが入ってしまう。攻撃振り開始時にクリアする。
	std::unordered_map<KujataEngine::GameObject*, float> hitCooldowns_;
	bool prevAttack_ = false;

	// ダメージ+ノックバックを適用する。無敵中などでヒット不成立ならfalse(ヒット履歴に残さない)。
	bool ApplyDamageToPlayer(KujataEngine::GameObject* target);
};

#pragma once

#include "HitInfo.h"
#include <KujataEngine.h>
#include <functional>

class PlayerHealth : public KujataEngine::Component {
public:
	// EnemyHealthが"HealthComponent"名で登録済みのため、衝突しない固有名にする。
	const char* GetTypeName() const override { return "PlayerHealth"; }

	void Initialize() override;
	void OnPlayStart() override;

	/// <summary>
	/// 敵の攻撃を受ける入口。EnemyWeaponはこちらを呼ぶ(TakeDamage直叩きはしない)。
	/// 処理順: 無敵チェック → 自分のIGuard(剣士の盾/術師のバリア)で軽減 →
	///         相方の範囲ガード(バリア内にいれば)で軽減 → ダメージ → ノックバック(CharacterMotor)。
	/// 無敵中などでヒット不成立ならfalse(呼び出し側はヒット履歴に残さない)。
	/// </summary>
	bool ReceiveHit(const HitInfo& hit);

	/// <summary>軽減なしの直接ダメージ(落下・固定ダメージ等。通常の被弾はReceiveHitを使う)。</summary>
	void TakeDamage(float damage);
	float GetHealthPercent() const;
	bool IsAlive() const;

	/// <summary>無敵(回避中など)。trueの間TakeDamageを無視する。</summary>
	void SetInvincible(bool invincible) { invincible_ = invincible; }
	bool IsInvincible() const { return invincible_; }

	/// <summary>
	/// 死亡しているか。HPが0でも「まだ倒れていない」状態を作らないよう、IsAlive()と裏表で持つ。
	/// 死亡中は無敵になり、頭脳(Player / AllyAIBrain)は入力とAIを止める。
	/// </summary>
	bool IsDead() const { return dead_; }

	void Update() override;

	/// <summary>
	/// 蘇生の進み具合(0〜1)。**倒れてからの経過時間**だけで進む(Revive Seconds で満ちる)。
	/// 相方の助けは要らない。ゲージ表示に使う。
	/// </summary>
	float GetReviveProgress() const;
	/// <summary>即座に蘇生させる(進捗を無視する)。</summary>
	void Revive();

	void SetOnHealthChanged(std::function<void(float)> cb);
	void SetOnDeath(std::function<void()> cb);

private:
	/// <summary>HPが0になった瞬間の後始末(無敵化・炎の設置・クリップ再生)。</summary>
	void EnterDeath();

public:

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT(maxHealth_, 1.0f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT(health_, 1.0f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT_NAMED_TIP(reviveSeconds_, "Revive Seconds", 0.1f, 1.0f, 60.0f,
		    "倒れてから自力で立ち上がるまでの秒数。**相方が助けに行く必要はない。**\n"
		    "チュートリアルの文面(「倒れても十秒で起き上がる」)と揃えること。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(reviveHealthPercent_, "Revive Health %", 1.0f, 1.0f, 100.0f,
		    "蘇生したときに戻るHPの割合[%]。満タンにすると死亡の重みが消える。");
		KUJATA_REGISTER_STRING_NAMED_TIP(deathClipName_, "Death Clip", "倒れたときに再生するクリップ名。空なら見た目は変えない。");
	}

	KUJATA_FIELD_FLOAT(maxHealth_, 100);
	KUJATA_FIELD_FLOAT(health_, 100);
	KUJATA_FIELD_FLOAT(reviveSeconds_, 10.0f);
	KUJATA_FIELD_FLOAT(reviveHealthPercent_, 50.0f);
	KUJATA_FIELD_STRING(deathClipName_, "");

	std::function<void(float)> onHealthChanged_;
	std::function<void()> onDeath_;

	// 無敵中か(シリアライズしないランタイム状態)。
	bool invincible_ = false;
	// 死亡中か。HPが0になった瞬間に立ち、蘇生で降りる。
	bool dead_ = false;
	// 倒れてからの経過[s]。reviveSeconds_ に達すると自分で起き上がる。
	float reviveTimer_ = 0.0f;
	// 死ぬ前のRigidbodyが動的だったか。蘇生で元へ戻すために覚えておく。
	bool wasDynamicBeforeDeath_ = true;
	// 死亡地点に置いた炎。Playごとに作り直すのでポインタは持ち越さない。
	KujataEngine::GameObject* deathPyre_ = nullptr;

	// 同じGameObjectのガード(無ければnullptr)。OnPlayStartで解決する。
	class IGuard* guard_ = nullptr;
	// 同じGameObjectの体(ノックバック先)。
	class CharacterMotor* motor_ = nullptr;
};

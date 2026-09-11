#pragma once

#include <KujataEngine.h>
#include <unordered_set>

class WeaponComponent : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "WeaponComponent"; }

	void Initialize() override;
	void Update() override;
	void OnTriggerStay(KujataEngine::ColliderComponent* other) override;

	/// <summary>
	/// 次の振りのダメージ/体勢崩し値を上書きする(MeleeAbilitySetがコンボ段・溜めごとに注入する)。
	/// Inspectorの値は「注入されなかったときの既定」。
	/// </summary>
	void SetSwingParams(float damage, float poiseDamage) {
		damageValue_ = damage;
		poiseDamage_ = poiseDamage;
	}
	float GetDamage() const { return damageValue_; }
	float GetPoiseDamage() const { return poiseDamage_; }

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		// attack_はbool型アニメーションチャンネル。キーフレームで攻撃判定をON/OFFする(ため中はOFF)。
		KUJATA_REGISTER_BOOL(attack_);
		KUJATA_REGISTER_FLOAT(damageValue_, 1.0f, 0.0f, 0.0f);
		KUJATA_REGISTER_FLOAT_NAMED_TIP(poiseDamage_, "Poise Damage", 1.0f, 0.0f, 10000.0f,
		    "1ヒットで敵に与える体勢崩し値。敵のPoise Maxに達するとスタンする。\n"
		    "MeleeAbilitySetが段ごとに上書きするので、ここは既定値。");
		KUJATA_REGISTER_FLOAT(knockback_, 0.1f, 0.0f, 0.0f);
	}

	KUJATA_FIELD_BOOL(attack_, false);
	KUJATA_FIELD_FLOAT(damageValue_, 10);
	// 1ヒットの体勢崩し値。
	KUJATA_FIELD_FLOAT(poiseDamage_, 20.0f);
	KUJATA_FIELD_FLOAT(knockback_, 5);

	// 1回の攻撃振り(attackがOFF→ONの間)でダメージ済みの敵。振り開始時にクリアする。
	// OnTriggerStayは毎フレーム呼ばれるため、これが無いと1振りで多段ヒットしてしまう。
	std::unordered_set<KujataEngine::GameObject*> hitThisSwing_;
	bool prevAttack_ = false;

	void ApplyDamageToEnemy(KujataEngine::GameObject* enemy);
};

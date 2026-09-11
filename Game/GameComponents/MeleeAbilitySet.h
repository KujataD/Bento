#pragma once
#include "IAbilitySet.h"
#include <components/AnimatorComponent.h>
#include <string>

class CharacterMotor;
class StaminaComponent;
class WeaponComponent;

/// <summary>
/// 近接攻撃(Pawn=剣士)のAbilitySet。
///
///   スロット0 = 通常攻撃。3段コンボ(Attack Clip 1〜3)。各段のクリップ後半(Combo Window From以降)に
///              もう一度入力すると次段へ繋がる。どの段で終わっても最後にRecovery Seconds の隙が出る。
///   スロット1 = 溜め攻撃(Charge Clip)。大振りで体勢崩し値が大きい。コンボ中には出せない。
///
/// スタミナ: 通常=Stamina Cost Normal[%] / 溜め=Stamina Cost Charge[%](StaminaComponentの最大値比)。
/// 残量が0より大きければ出せる(0で止まる)。
/// 攻撃判定は従来通りクリップのboolチャンネルが WeaponComponent.attack をON/OFFする。
/// 段ごとのダメージ/体勢崩し値は開始時に WeaponComponent::SetSwingParams で注入する。
/// IsBusy()は「振り中 or 隙」で、頭脳側はこの間移動を止める(回避は可)。
/// </summary>
class MeleeAbilitySet : public IAbilitySet {
public:
	const char* GetTypeName() const override { return "MeleeAbilitySet"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	bool TryUse(int slot) override;
	bool IsBusy() const override;

	/// <summary>コンボの何段目を振っているか(0=非攻撃)。</summary>
	int GetComboIndex() const { return comboIndex_; }
	/// <summary>隙(後隙)中か。</summary>
	bool IsRecovering() const { return recoveryTimer_ > 0.0f; }

private:
	/// <summary>段stepのクリップ名(1〜3)。</summary>
	const std::string& ClipForStep(int step) const;
	/// <summary>段stepを開始する(クリップ再生+武器パラメータ注入+スタミナ消費)。</summary>
	bool StartStep(int step);
	/// <summary>溜め攻撃を開始する。</summary>
	bool StartCharge();
	/// <summary>攻撃の終了処理(隙へ移行)。endedStepは振り終えた段(1〜3, 4=溜め)。</summary>
	void FinishAttack(int endedStep);
	/// <summary>振り終えた段に応じた隙[秒]。</summary>
	float RecoveryForStep(int endedStep) const;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(attackClip1_, "Attack Clip 1", "コンボ1段目のクリップ名。");
		KUJATA_REGISTER_STRING_NAMED_TIP(attackClip2_, "Attack Clip 2", "コンボ2段目のクリップ名。空なら1段で終わる。");
		KUJATA_REGISTER_STRING_NAMED_TIP(attackClip3_, "Attack Clip 3", "コンボ3段目のクリップ名。空なら2段で終わる。");
		KUJATA_REGISTER_STRING_NAMED_TIP(chargeClipName_, "Charge Clip", "溜め攻撃のクリップ名。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(comboDamage_, "Combo Damage", 1.0f, 0.0f, 1000.0f, "通常攻撃1ヒットのダメージ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(comboPoise_, "Combo Poise", 1.0f, 0.0f, 10000.0f, "通常攻撃1ヒットの体勢崩し値。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(chargeDamage_, "Charge Damage", 1.0f, 0.0f, 1000.0f, "溜め攻撃1ヒットのダメージ。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(chargePoise_, "Charge Poise", 1.0f, 0.0f, 10000.0f, "溜め攻撃1ヒットの体勢崩し値。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(staminaCostNormal_, "Stamina Cost Normal", 1.0f, 0.0f, 100.0f, "通常攻撃1回のスタミナ消費[最大値比%]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(staminaCostCharge_, "Stamina Cost Charge", 1.0f, 0.0f, 100.0f, "溜め攻撃1回のスタミナ消費[最大値比%]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(comboWindowFrom_, "Combo Window From", 0.05f, 0.0f, 1.0f,
		    "クリップのこの割合(0〜1)以降に入力すると次段へ繋がる。それより前の入力も先行入力として受け付ける。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(recoveryLightSeconds_, "Recovery Light", 0.01f, 0.0f, 3.0f,
		    "1〜2段目で振り終えた(コンボを繋げなかった)ときの隙[秒]。\n"
		    "**ここを短くするほど攻撃を出しやすくなる。** 繋げば繋ぐほど隙が伸びる形にして、\n"
		    "「軽く当てて引く」か「最後まで振り切る」かの選択を作る。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(recoverySeconds_, "Recovery Finisher", 0.01f, 0.0f, 3.0f,
		    "3段目(締め)まで振り切ったときの隙[秒]。1〜2段目より長くする。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(recoveryChargeSeconds_, "Recovery Charge", 0.01f, 0.0f, 3.0f,
		    "溜め攻撃の後の隙[秒]。**大槌らしさはここに寄せる**(通常攻撃は軽く、溜めは重く)。");
	}

	// コンボ各段のクリップ名。
	KUJATA_FIELD_STRING(attackClip1_, "PawnAttack1");
	KUJATA_FIELD_STRING(attackClip2_, "PawnAttack2");
	KUJATA_FIELD_STRING(attackClip3_, "PawnAttack3");
	// 溜め攻撃のクリップ名。
	KUJATA_FIELD_STRING(chargeClipName_, "PawnAttackCharge");
	// 通常攻撃の性能。
	KUJATA_FIELD_FLOAT(comboDamage_, 10.0f);
	KUJATA_FIELD_FLOAT(comboPoise_, 20.0f);
	// 溜め攻撃の性能。
	KUJATA_FIELD_FLOAT(chargeDamage_, 30.0f);
	KUJATA_FIELD_FLOAT(chargePoise_, 100.0f);
	// スタミナ消費[%]。
	KUJATA_FIELD_FLOAT(staminaCostNormal_, 20.0f);
	KUJATA_FIELD_FLOAT(staminaCostCharge_, 30.0f);
	// コンボ受付開始(クリップ長に対する割合)。
	// 既定のクリップ長(0.6/0.6/0.72秒)と0.6で、3段つないだときの合計が約1.44秒になる
	// (0.6×0.6 + 0.6×0.6 + 0.72 = 1.44)。クリップの尺を変えたらここも見直すこと。
	// 各クリップは打点が窓(0.6)の直前に来るように打ってあるので、繋ぐと当たった瞬間に次段が出る。
	KUJATA_FIELD_FLOAT(comboWindowFrom_, 0.6f);
	// 後隙[s]。**振り切った段数で変える**(途中で止めれば軽く、締めまで出せば重い)。
	KUJATA_FIELD_FLOAT(recoveryLightSeconds_, 0.12f);
	KUJATA_FIELD_FLOAT(recoverySeconds_, 0.24f);
	KUJATA_FIELD_FLOAT(recoveryChargeSeconds_, 0.4f);

	// モデル(子)のAnimator。
	KujataEngine::AnimatorComponent* animator_ = nullptr;
	// 同じGameObjectのCharacterMotor(行動不能チェック用)。
	CharacterMotor* motor_ = nullptr;
	// 同じGameObjectのStaminaComponent(無ければスタミナ無制限)。
	StaminaComponent* stamina_ = nullptr;
	// 子孫の武器(ダメージ/体勢崩し値の注入先)。
	WeaponComponent* weapon_ = nullptr;

	// --- 実行状態 ---
	// 振っている段(0=非攻撃, 1〜3=コンボ, 4=溜め)。
	int comboIndex_ = 0;
	// 現在の振りの経過時間[s]。
	float attackTimer_ = 0.0f;
	// 現在の振りのクリップ長[s]。
	float clipLength_ = 0.0f;
	// 次段の先行入力が入っているか。
	bool nextQueued_ = false;
	// 後隙の残り[s]。
	float recoveryTimer_ = 0.0f;
};

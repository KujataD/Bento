#pragma once
#include "IGuard.h"
#include <components/AnimatorComponent.h>
#include <string>

class CharacterMotor;
class StaminaComponent;
class IAbilitySet;

/// <summary>
/// 剣士(Pawn)のガード。L2を押している間、盾を構える。
///
///   物理攻撃: 完全に防ぐ。防ぐたびにスタミナを Guard Cost[%] 消費し、それで空になるとガードブレイク
///             (盾が弾かれてGuard Break Stun秒の硬直)。構えているだけではスタミナを消費しない。
///   魔法攻撃: ダメージ半減(Magic Damage Scale)。スタミナ消費は同じ。
///   ジャストガード: 構え始めてから Just Guard Window 秒以内に物理攻撃を受けると、無傷・スタミナ消費なしで
///             敵をのけぞらせ、体勢崩し値 Just Guard Poise を与える。
///   正面判定: 攻撃元が Guard Angle(半角)の外(背後)からなら防げない。
///
/// 構え中は移動が鈍足(Player側で0.5倍)、攻撃は出せない。回避は可(構えは解ける)。
/// 構えの見た目は Guard Clip(盾を上げる)を再生し、離したらStopで元の姿勢へ戻す。
/// </summary>
class SwordGuard : public IGuard {
public:
	const char* GetTypeName() const override { return "SwordGuard"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	void SetGuardInput(bool pressed) override;
	bool IsGuarding() const override { return guarding_; }
	GuardResult Mitigate(const HitInfo& hit) override;

private:
	void BeginGuard();
	void EndGuard();
	/// <summary>攻撃元が正面(Guard Angle以内)にいるか。攻撃元不明なら正面扱い。</summary>
	bool IsFrontal(const HitInfo& hit) const;
	/// <summary>ガードブレイク(スタミナ枯渇)。</summary>
	void Break(const HitInfo& hit);

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(guardClipName_, "Guard Clip", "構え中に再生するクリップ名(盾を上げた姿勢)。空なら姿勢は変えない。");
		KUJATA_REGISTER_STRING_NAMED_TIP(guardBreakClipName_, "Guard Break Clip", "ガードブレイク時に再生するクリップ名。空ならのけぞりクリップのまま。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(guardCost_, "Guard Cost", 1.0f, 0.0f, 100.0f, "攻撃を1回防ぐごとのスタミナ消費[最大値比%]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(magicDamageScale_, "Magic Damage Scale", 0.05f, 0.0f, 1.0f, "魔法攻撃を防いだときのダメージ倍率(0.5=半減)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(justGuardWindow_, "Just Guard Window", 0.01f, 0.0f, 1.0f, "構え始めからこの秒数以内の物理ヒットがジャストガードになる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(justGuardPoise_, "Just Guard Poise", 1.0f, 0.0f, 10000.0f, "ジャストガード成立時に敵へ与える体勢崩し値。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(guardAngle_, "Guard Angle", 1.0f, 0.0f, 180.0f, "防げる範囲の半角[deg]。180で全方位。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(guardBreakStun_, "Guard Break Stun", 0.05f, 0.0f, 5.0f, "ガードブレイク時の硬直秒数。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(guardBreakKnockback_, "Guard Break Knockback", 0.1f, 0.0f, 50.0f, "ガードブレイク時に後ろへ弾かれる初速。");
	}

	// 構えクリップ。
	KUJATA_FIELD_STRING(guardClipName_, "PawnGuard");
	// ガードブレイククリップ。
	KUJATA_FIELD_STRING(guardBreakClipName_, "PawnGuardBreak");
	// 1回防ぐごとのスタミナ消費[%]。
	KUJATA_FIELD_FLOAT(guardCost_, 25.0f);
	// 魔法の軽減率。
	KUJATA_FIELD_FLOAT(magicDamageScale_, 0.5f);
	// ジャストガード猶予[s]。
	KUJATA_FIELD_FLOAT(justGuardWindow_, 0.2f);
	// ジャストガードの体勢崩し値。
	KUJATA_FIELD_FLOAT(justGuardPoise_, 100.0f);
	// 防げる半角[deg]。
	KUJATA_FIELD_FLOAT(guardAngle_, 100.0f);
	// ガードブレイクの硬直[s]。
	KUJATA_FIELD_FLOAT(guardBreakStun_, 1.5f);
	// ガードブレイクのノックバック初速。
	KUJATA_FIELD_FLOAT(guardBreakKnockback_, 4.0f);

	// モデル(子)のAnimator。
	KujataEngine::AnimatorComponent* animator_ = nullptr;
	// 同じGameObjectの体・スタミナ・技。
	CharacterMotor* motor_ = nullptr;
	StaminaComponent* stamina_ = nullptr;
	IAbilitySet* abilitySet_ = nullptr;

	// --- 実行状態 ---
	// 構え中か。
	bool guarding_ = false;
	// 構え始めてからの時間[s](ジャストガード判定)。
	float guardTime_ = 0.0f;
	// 一度ボタンを離すまで構え直せない(ガードブレイク後・中断後にジャストガードを連発させない)。
	bool needRelease_ = false;
};

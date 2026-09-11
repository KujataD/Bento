#pragma once
#include <KujataEngine.h>

/// <summary>
/// プレイアブルキャラのスタミナ。攻撃・ガードが消費し、時間で回復する。
/// 「残量が0より大きければ技は出せる(消費で0に張り付く)」フロム式を前提にしている:
///   使う側は CanUse() で可否を見て、Consume() で引く。足りない分は0で止まる。
/// 回復は最後に消費してからRegen Delay秒待ってから始まる。
/// </summary>
class StaminaComponent : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "StaminaComponent"; }
	bool AllowMultiple() const override { return false; }

	void Initialize() override;
	void OnPlayStart() override;
	void Update() override;

	/// <summary>技を出せるか(残量>0)。</summary>
	bool CanUse() const { return stamina_ > 0.0f; }

	/// <summary>残量が空か。</summary>
	bool IsEmpty() const { return stamina_ <= 0.0f; }

	/// <summary>
	/// amountだけ消費する(0未満にはならない)。回復待ちタイマーをリセットする。
	/// この消費で空になった(消費前は>0、消費後は0)ならtrueを返す。剣士のガードブレイク判定に使う。
	/// </summary>
	bool Consume(float amount);

	/// <summary>最大値に対する割合[%]で消費する(攻撃の「20%」指定用)。戻り値はConsumeと同じ。</summary>
	bool ConsumePercent(float percent) { return Consume(maxStamina_ * percent * 0.01f); }

	/// <summary>
	/// 継続消費(バリア展開中など)。毎フレーム呼ぶ。回復待ちも毎回リセットされるので、
	/// 展開中は回復しない。空になったらfalseを返す(呼び出し側は展開を閉じる)。
	/// </summary>
	bool Drain(float perSecond);

	float GetStamina() const { return stamina_; }
	float GetMaxStamina() const { return maxStamina_; }
	float GetPercent() const { return maxStamina_ > 0.0f ? stamina_ / maxStamina_ : 0.0f; }

	/// <summary>全回復(切替時・リスポーン時など)。</summary>
	void Refill() { stamina_ = maxStamina_; }

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED_TIP(maxStamina_, "Max Stamina", 1.0f, 1.0f, 10000.0f,
		    "スタミナの最大値。攻撃の消費は「最大値の○%」で指定するので、ここを変えると全体の手数が変わる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(regenPerSecond_, "Regen Per Second", 0.5f, 0.0f, 1000.0f,
		    "1秒あたりの回復量。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(regenDelay_, "Regen Delay", 0.05f, 0.0f, 10.0f,
		    "最後に消費してから回復が始まるまでの待ち時間[秒]。");
	}

	// 最大値。
	KUJATA_FIELD_FLOAT(maxStamina_, 100.0f);
	// 回復速度[/s]。
	KUJATA_FIELD_FLOAT(regenPerSecond_, 25.0f);
	// 回復開始までの待ち[s]。
	KUJATA_FIELD_FLOAT(regenDelay_, 1.0f);

	// 現在値(ランタイム)。
	float stamina_ = 100.0f;
	// 回復開始までの残り待ち時間[s]。
	float regenDelayTimer_ = 0.0f;
};

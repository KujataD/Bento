#pragma once
#include <KujataEngine.h>
#include <string>
#include <vector>

/// <summary>
/// 敵1体ぶんのヘイト表。**敵のGameObjectに付ける**(パーティ側ではなく敵側が持つ)。
///
/// 敵はこれを見て「今どちらを狙うか」を決める。全員が最寄りを狙う方式と違い、
/// **殴ってきた相手を覚える**ので、キャラを切り替えて攻めると敵の狙いもそちらへ移る。
/// 敵ごとに独立した表を持つため、集団が一人へ雪崩を打つことにもならない。
///
/// ヘイトが動く要因は3つ:
///   1. **ダメージ**  … 与ダメージ × Damage Scale。主役。EnemyHealthが被弾時に通知する
///   2. **距離**      … 近い相手ほど毎秒じわじわ加算。誰にも殴られていない間の狙いを決める
///   3. **時間減衰**  … 毎秒一定割合で目減りする。昔の殴打をいつまでも引きずらせない
///
/// **乗り換えにはヒステリシスを入れてある**(Switch Ratio)。単に最大値を選ぶと、
/// 2人のヘイトが拮抗したときに毎フレーム狙いが入れ替わって挙動が壊れる。
/// </summary>
class HateTable : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "HateTable"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	/// <summary>
	/// ヘイトを積む。sourceは味方キャラのルート(候補タグが付いたオブジェクト)。
	/// 候補に含まれない相手を渡した場合は無視する。
	/// </summary>
	void AddHate(KujataEngine::GameObject* source, float amount);

	/// <summary>与ダメージからヘイトを積む(Damage Scaleを掛ける)。EnemyHealthが呼ぶ。</summary>
	void AddDamageHate(KujataEngine::GameObject* source, float damage);

	/// <summary>いま狙うべき相手。候補がいなければnullptr。</summary>
	KujataEngine::GameObject* GetTarget() const { return target_; }

	/// <summary>指定相手の現在のヘイト値(デバッグ表示・BT条件用)。</summary>
	float GetHate(const KujataEngine::GameObject* source) const;

	/// <summary>表を空にする(蘇生・戦闘リセット用)。</summary>
	void Clear();

private:
	struct Entry {
		KujataEngine::GameObject* source = nullptr;
		float hate = 0.0f;
	};

	/// <summary>候補タグが付いた生存キャラを集める。</summary>
	void GatherCandidates(std::vector<KujataEngine::GameObject*>& outCandidates) const;
	/// <summary>候補に無くなったエントリを捨てる。**ポインタの比較だけで、実体は触らない**(解放済みでも安全)。</summary>
	void DropMissing(const std::vector<KujataEngine::GameObject*>& candidates);
	Entry* Find(const KujataEngine::GameObject* source);

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(targetTag_, "Target Tag",
		    "ヘイトの対象になるタグ。このタグが付いた生存キャラ(PlayerHealth持ち)だけを候補にする。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(damageHateScale_, "Damage Scale", 0.05f, 0.0f, 20.0f,
		    "与ダメージ1あたりに積むヘイト量。**ここが主役**なので、他の要因より十分大きくしておく。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(proximityHatePerSecond_, "Proximity Per Second", 0.1f, 0.0f, 100.0f,
		    "真横(距離0)にいる相手へ毎秒積むヘイト量。Proximity Rangeまで直線的に0へ落ちる。\n"
		    "**誰にも殴られていない間の狙いを決める**ための値。大きすぎるとダメージを無視して近い方へ寄る。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(proximityRange_, "Proximity Range", 0.1f, 0.5f, 60.0f,
		    "距離によるヘイトが0になる距離。これより遠い相手には距離ぶんのヘイトが乗らない。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(decayPerSecond_, "Decay Per Second", 0.005f, 0.0f, 1.0f,
		    "毎秒この割合だけヘイトが目減りする(0.1で毎秒10%減)。\n"
		    "0にすると永久に蓄積し、序盤に殴った側から一生狙いが動かなくなる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(switchRatio_, "Switch Ratio", 0.01f, 1.0f, 3.0f,
		    "**今の狙いを乗り換える閾値。** 現ターゲットのヘイトのこの倍を超えて初めて相手を変える。\n"
		    "1.0にすると拮抗時に毎フレーム狙いが入れ替わってガクガクするので、1.15〜1.3を推奨。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(loseTargetDistance_, "Lose Target Distance", 0.5f, 0.0f, 200.0f,
		    "この距離より離れた相手はヘイトが最大でも狙わない(0で無制限)。逃げ切りを成立させたい場合に使う。");
	}

	KUJATA_FIELD_STRING(targetTag_, "Ally");
	KUJATA_FIELD_FLOAT(damageHateScale_, 1.0f);
	KUJATA_FIELD_FLOAT(proximityHatePerSecond_, 6.0f);
	KUJATA_FIELD_FLOAT(proximityRange_, 12.0f);
	KUJATA_FIELD_FLOAT(decayPerSecond_, 0.06f);
	KUJATA_FIELD_FLOAT(switchRatio_, 1.2f);
	KUJATA_FIELD_FLOAT(loseTargetDistance_, 0.0f);

	// --- 実行状態 ---
	// 味方は2人想定なので、mapよりvectorの線形探索のほうが速くて素直。
	std::vector<Entry> entries_;
	KujataEngine::GameObject* target_ = nullptr;
};

#pragma once
#include <KujataEngine.h>

/// <summary>
/// キャラクターの攻撃手段(技)の共通インターフェース。
/// 頭脳(Player=入力 / AllyAIBrain=BT)は「スロットNを使え」としか言わず、
/// それが剣振りか魔法弾かは実装側(MeleeAbilitySet / MagicAbilitySet)だけが知る。
/// GetComponent&lt;IAbilitySet&gt;()で実装型を問わず取得できる(dynamic_castベース)。
/// </summary>
class IAbilitySet : public KujataEngine::Component {
public:
	/// <summary>
	/// スロットslotの技を使う。使えたらtrue(クールダウン中・行動不能中などはfalse)。
	/// </summary>
	virtual bool TryUse(int slot) = 0;

	/// <summary>技の実行中(モーション中・詠唱中など)か。頭脳側の次行動判断に使う。</summary>
	virtual bool IsBusy() const = 0;

	/// <summary>
	/// 致命の一撃の当たった瞬間に呼ばれる。**技ごとの決め方**をここで実装する。
	/// trueを返すと「自分でダメージを出した」とみなされ、CriticalStrikeComponentは素のダメージを与えない。
	///
	/// 既定はfalse(=何もしない)なので、剣士のように「その場で斬るだけ」の技は実装不要。
	/// 術師はこれを実装し、杖を地面へ突き立てて上空へ弾をばら撒く。
	/// totalDamageは致命1回ぶんの総ダメージ。複数の弾に分ける場合は割って使う。
	/// </summary>
	virtual bool TryCritical(KujataEngine::GameObject* target, float totalDamage) {
		(void)target;
		(void)totalDamage;
		return false;
	}

	/// <summary>
	/// 致命の振りかぶり中に毎フレーム呼ばれる(progressは0→1)。溜めの見せ方に使う。
	/// 術師は杖の球をここで育てる。既定は何もしない。
	/// </summary>
	virtual void OnCriticalWindup(float progress) { (void)progress; }

	/// <summary>致命が終わった/中断されたときに呼ばれる。演出を元へ戻す。</summary>
	virtual void OnCriticalEnd() {}
};

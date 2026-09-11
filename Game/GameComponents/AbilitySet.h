#pragma once
#include <KujataEngine.h>

/// <summary>
/// キャラクターの攻撃手段(技)の共通インターフェース。
/// </summary>
class AbilitySet : public KujataEngine::Component {
public:
	/// <summary>
	/// スロットslotの技を使う。使えたらtrue(クールダウン中・行動不能中などはfalse)。
	/// </summary>
	virtual bool TryUse(int slot) = 0;

	/// <summary>技の実行中(モーション中・詠唱中など)か。頭脳側の次行動判断に使う。</summary>
	virtual bool IsBusy() const = 0;
};

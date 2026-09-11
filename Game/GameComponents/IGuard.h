#pragma once
#include "HitInfo.h"
#include <KujataEngine.h>

/// <summary>
/// ガード手段の共通インターフェース(IAbilitySetと同じ流儀。Component派生の抽象クラス)。
/// 剣士=SwordGuard(物理を防ぐ・魔法を半減)、術師=BarrierGuard(魔法を防ぐ・物理を半減・範囲)。
/// PlayerHealth::ReceiveHitが同じGameObjectのIGuardへMitigateを問い合わせ、結果でダメージを軽減する。
/// 頭脳(Player/AllyAIBrain)はSetGuardInput(押しているか)を毎フレーム流すだけでよい。
/// </summary>
class IGuard : public KujataEngine::Component {
public:
	/// <summary>ガードボタンの入力状態を渡す(押している間true)。構え/展開の開始・終了は実装側が判断する。</summary>
	virtual void SetGuardInput(bool pressed) = 0;

	/// <summary>構え中/展開中か(移動速度の低下や攻撃抑制の判断に使う)。</summary>
	virtual bool IsGuarding() const = 0;

	/// <summary>
	/// 自分自身へのヒットを軽減する。ガード中でなければ素通し(damageScale=1)を返す。
	/// 副作用(スタミナ消費・ガードブレイク・ジャストガードの反撃など)はここで起こす。
	/// </summary>
	virtual GuardResult Mitigate(const HitInfo& hit) = 0;

	/// <summary>
	/// 他者(相方)の位置を守っているか。範囲ガード(術師のバリア)だけがtrueを返す。
	/// trueならPlayerHealth::ReceiveHitはMitigateForAllyでその者のヒットも軽減する。
	/// </summary>
	virtual bool CoversPosition(const KujataEngine::Vector3& worldPosition) const {
		(void)worldPosition;
		return false;
	}

	/// <summary>範囲内の相方へのヒットを軽減する(CoversPositionがtrueのときだけ呼ばれる)。既定は素通し。</summary>
	virtual GuardResult MitigateForAlly(const HitInfo& hit) {
		(void)hit;
		return GuardResult{};
	}
};

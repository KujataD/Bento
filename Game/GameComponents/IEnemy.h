#pragma once
#include <KujataEngine.h>

/// <summary>
/// 「これは敵である」ことを表すインターフェース(IAbilitySet / IGuard と同じ流儀のComponent派生の抽象)。
///
/// これまで敵の判定は「EnemyHealth(HealthComponent)を持っているか」で行っていたが、
/// それだと「HPを持つ敵」しか敵として扱えず、破壊できないギミックや複数部位を持つ敵を
/// 素直に表現できない。敵かどうかは体力の有無とは別の性質なので、専用のインターフェースへ分けた。
///
/// 探す側は GetComponent&lt;IEnemy&gt;() / GetComponentInParent&lt;IEnemy&gt;() で問い合わせる:
///   - Z注目(LockOnController)の候補列挙と狙い点
///   - 味方AI(AllyAIBrain)の索敵
///   - 魔法弾のホーミング先
///
/// 実装は今のところ EnemyHealth(体力を持つ通常の敵)。別種の敵を足すときはこれを実装するだけでよい。
/// </summary>
class IEnemy : public KujataEngine::Component {
public:
	/// <summary>
	/// 狙われる対象として有効か。死亡・撃破済み・無敵状態などではfalseを返す。
	/// falseになった敵はZ注目から自動的に外れ、AIの索敵対象からも外れる。
	/// </summary>
	virtual bool IsTargetable() const = 0;

	/// <summary>
	/// Z注目のレティクル位置・カメラの注視点・ホーミング弾の目標になるワールド座標。
	/// 敵ごとに自由に決められる(小型敵は頭、ボスは胴体中心、など)。
	/// </summary>
	virtual KujataEngine::Vector3 GetLockOnPoint() const = 0;
};

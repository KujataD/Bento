#pragma once

#include <KujataEngine.h>

/// <summary>ガーディアンの脚の本数。前左/前右/後右/後左の4本で固定。</summary>
/// <summary>脚のスロット数(=階層に用意できる脚の上限)。実際に使う本数はIGuardianLegRig::GetLegCountが返す。</summary>
inline constexpr int kGuardianLegSlotCount = 4;
/// <summary>旧名。配列サイズ用途で残してある(意味はスロット数)。</summary>
inline constexpr int kGuardianLegCount = kGuardianLegSlotCount;

/// <summary>
/// 脚リグの共通インターフェース。「足先をどこへ置くか」を決める側(GuardianGait)と
/// 「胴体をどこへ置くか」を決める側(GuardianBody)が、関節の解き方を知らずに済むようにする。
///
/// 現在の実装は GuardianSplineRig(3次ベジェに関節を並べる方式)のみ。
/// GetComponent&lt;IGuardianLegRig&gt;() で実装型を問わず取得できる(IAbilitySetと同じdynamic_castベース)。
/// </summary>
class IGuardianLegRig : public KujataEngine::Component {
public:
	virtual int GetLegCount() const = 0;

	/// <summary>接合部のワールド位置。脚の根元であり、届く範囲の中心。</summary>
	virtual KujataEngine::Vector3 GetHipWorld(int index) const = 0;

	/// <summary>
	/// 接合部から足先までの最大距離(ボーン長の合計)。これを超える位置には物理的に届かない。
	/// 歩行側が「足を地面から離すべきか」を判断するのに使う。
	/// </summary>
	virtual float GetMaxReach(int index) const = 0;

	/// <summary>
	/// 足の基準接地位置(水平位置のみ)のワールド座標。高さはルートのYが入っているので、
	/// 接地させたい場合は呼び出し側でレイキャストしてYを差し替える。
	/// ボディではなくGuardianルートの向きが基準なので、球体が回っても足の定位置は動かない。
	/// </summary>
	virtual KujataEngine::Vector3 GetHomeWorld(int index) const = 0;

	/// <summary>プロシージャル側の足先目標(ワールド)を指示する。</summary>
	virtual void SetProceduralTarget(int index, const KujataEngine::Vector3& worldPosition) = 0;

	virtual KujataEngine::Vector3 GetProceduralTarget(int index) const = 0;

	/// <summary>ブレンド後に実際に解決した足先のワールド位置。</summary>
	virtual KujataEngine::Vector3 GetFootWorld(int index) const = 0;

	/// <summary>曲線レイヤーが主導権を持っているか(weightが半分を超えているか)。</summary>
	virtual bool IsCurveDriven(int index) const = 0;

	// --- 曲線レイヤーの操作 ---
	// AnimationClipから駆動するのが本筋だが、攻撃をコードで組み立てる場合はここから直接動かす。
	// どちらもチャンネルは同じなので、後からクリップへ移し替えても歩行側の扱いは変わらない。

	/// <summary>0=プロシージャル歩行 / 1=curveTargetへ完全追従。</summary>
	virtual void SetCurveWeight(int index, float weight) = 0;
	virtual float GetCurveWeight(int index) const = 0;

	/// <summary>曲線制御時の足先目標。**Guardianルート基準のローカル座標**。</summary>
	virtual void SetCurveTargetLocal(int index, const KujataEngine::Vector3& rootLocalPosition) = 0;
	virtual KujataEngine::Vector3 GetCurveTargetLocal(int index) const = 0;

	/// <summary>
	/// 直線モード。onにするとハンドル(hipTangent/footTangent)と弧長合わせを無視し、
	/// 接合部から目標までを一直線に結ぶ。**脚をピンと伸ばした表現**に使う。
	/// 通常の歩行では常にoffにしておくこと(膝が消えて棒になる)。
	/// </summary>
	virtual void SetCurveStraight(int index, bool straight) = 0;
	virtual bool IsCurveStraight(int index) const = 0;

	/// <summary>足の定位置をルート基準のローカル座標で返します(攻撃の起点/戻り先に使う)。</summary>
	virtual KujataEngine::Vector3 GetHomeLocal(int index) const = 0;

	/// <summary>球体ボディのGameObject(見つからなければnullptr)。</summary>
	virtual KujataEngine::GameObject* GetBodyObject() const = 0;
};

#pragma once

#include <BahamutAI/AI.h>
#include <KujataEngine.h>
#include <memory>
#include <string>

namespace KujataEngine {
class AnimatorComponent;
}
class EnemyHealth;
class EnemyWeapon;
class HateTable;

/// <summary>
/// 雑魚敵の共通基盤。**近距離型も遠距離型もこの1つで賄う。**
/// 体(移動・旋回・のけぞり・ヘイト参照)は共通で、「どう戦うか」はBehaviorTreeの組み方だけで分ける。
///
/// 型ごとにコンポーネントを分けなかったのは、雑魚のバリエーションを増やすたびに
/// C++を書き足す作りにしたくないため。武器を持たせてBTを差し替えれば新種になる。
///
/// **ターゲットはHateTable任せ。** 同じGameObjectにHateTableがあればその狙いに従い、
/// 無ければ従来どおり対象タグの最寄りを狙う。
///
/// 登録Condition:
///   IsTargetWithin  : 対象との水平距離がdistance以内か
///
/// 登録Action(移動):
///   Chase           : 対象へ旋回しつつ寄る。stopDistance以内なら止まる(毎TickSuccess)
///   Backstep        : 対象から離れる。向きは変えない(毎TickSuccess)
///   Strafe          : 対象を向いたまま横へ回り込む。dir>0で右回り(毎TickSuccess)
///   FaceTarget      : 対象を向く。向き切るまでRunning
///
/// 登録Action(近距離): 3つをSequenceで並べて1回の攻撃にする
///   SwingRaise      : 振りかぶり。ここではまだ当たらない
///   SwingHit        : 振り。攻撃判定ON
///   SwingRecover    : 後隙。判定OFF
///
/// 登録Action(遠距離): 同じく3つで1回の射撃
///   BeamWarn        : 予兆。ビームを細く出して狙いを見せる(まだ当たらない)
///   BeamFire        : 照射。攻撃判定ON。旋回を遅くして避けられる余地を残す
///   BeamRecover     : 消えるまでの余韻
///
/// 攻撃判定は近距離・遠距離とも子オブジェクトのEnemyWeaponをON/OFFして行う。
/// 遠距離のビームはCube1個をZ方向へ伸ばして表現する(ガーディアンのビームと同じ方式)。
/// </summary>
class GruntEnemyComponent : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "GruntEnemyComponent"; }
	bool AllowMultiple() const override { return false; }

	void Initialize() override;
	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;

private:
	void LoadBTSet();
	void RegisterBTFunctions();

	// --- BT Condition ---
	bool IsTargetWithin(const BahamutAI::NodeParams& params);

	// --- BT Action: 移動 ---
	BahamutAI::BTStatus Chase(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus Backstep(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus Strafe(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus FaceTarget(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	// --- BT Action: 近距離 ---
	BahamutAI::BTStatus SwingRaise(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus SwingHit(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus SwingRecover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	// --- BT Action: 遠距離 ---
	BahamutAI::BTStatus BeamWarn(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus BeamFire(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus BeamRecover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	// --- helpers ---
	/// <summary>狙う相手。HateTableがあればその判断に従い、無ければ対象タグの最寄り。</summary>
	KujataEngine::GameObject* FindTarget();
	/// <summary>フェーズの時間管理。durationを過ぎたらtrueを返して次へ渡す。</summary>
	bool TickPhase(const char* phaseName, float duration, float deltaTime, float& outProgress);
	/// <summary>対象へYawだけ旋回する。ほぼ向いていればtrue。</summary>
	bool RotateTowardsTarget(float turnSpeed, float deltaTime);
	/// <summary>水平距離。対象がいなければ非常に大きな値。</summary>
	float HorizontalDistanceToTarget() const;

	KujataEngine::GameObject* FindChild(const std::string& name) const;
	KujataEngine::GameObject* GetMeleeWeapon();
	KujataEngine::GameObject* GetBeamObject();
	void SetWeaponAttack(KujataEngine::GameObject* weaponObject, bool active);
	/// <summary>ビームの長さ・太さ・狙いの高さを反映する。</summary>
	void UpdateBeam(float length, float thickness, float aimHeight);
	void HideBeam();
	/// <summary>攻撃を中断して見た目を戻す(のけぞり・スタン・Play停止から呼ぶ)。</summary>
	void AbortAttack();

	void OnDamaged();
	void OnStaggered();
	bool IsStunned() const;
	bool IsRecoiling() const { return recoilTimer_ > 0.0f; }
	KujataEngine::AnimatorComponent* GetAnimator();

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(btSetFolder_, "BT Set Folder",
		    "この個体が使うBTセットのフォルダ名(Data/Resources/bt_set/以下)。\n"
		    "**近距離型と遠距離型はここだけで切り替える。** 例: MeleeGruntBT / RangedGruntBT");
		KUJATA_REGISTER_STRING_NAMED_TIP(treeKey_, "Monitor Tree Key",
		    "ライブ監視のツリー識別子。**BehaviorTree.jsonのツリー名と完全に一致させること**。\n"
		    "違っているとエディタで購読してもパケットが飛ばない(エラーも出ない)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(targetTag_, "Target Tag",
		    "HateTableが無い場合に狙う相手のタグ。この タグの生存キャラのうち最寄りを狙う。");
		KUJATA_REGISTER_STRING_NAMED_TIP(weaponObjectName_, "Melee Weapon Object",
		    "近接の当たり判定を持つ子オブジェクト名(EnemyWeapon付き)。空なら近接攻撃を持たない個体。");
		KUJATA_REGISTER_STRING_NAMED_TIP(beamObjectName_, "Beam Object",
		    "ビーム用の子オブジェクト名(Cube + EnemyWeapon)。空ならビームを持たない個体。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(beamHeight_, "Beam Height", 0.01f, 0.0f, 10.0f,
		    "ビームの発射位置の高さ(ルート基準)。胸のあたりに合わせる。");
		KUJATA_REGISTER_STRING_NAMED_TIP(stunClipName_, "Stun Clip",
		    "体勢崩しでスタンしたときのクリップ名。空なら見た目は変えずに硬直だけする。");
		KUJATA_REGISTER_STRING_NAMED_TIP(recoilClipName_, "Recoil Clip", "被弾のけぞりのクリップ名。空なら再生しない。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(recoilEnabled_, "Recoil Enabled",
		    "被弾でのけぞるか。**雑魚はONにしておくと手応えが出る**(攻撃が中断される)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(recoilDuration_, "Recoil Duration", 0.01f, 0.0f, 3.0f, "のけぞりの硬直[秒]。");
	}

	KUJATA_FIELD_STRING(btSetFolder_, "MeleeGruntBT");
	KUJATA_FIELD_STRING(treeKey_, "MeleeGrunt");
	KUJATA_FIELD_STRING(targetTag_, "Ally");
	KUJATA_FIELD_STRING(weaponObjectName_, "Weapon");
	KUJATA_FIELD_STRING(beamObjectName_, "");
	KUJATA_FIELD_FLOAT(beamHeight_, 1.0f);
	KUJATA_FIELD_STRING(stunClipName_, "");
	KUJATA_FIELD_STRING(recoilClipName_, "");
	KUJATA_FIELD_BOOL(recoilEnabled_, true);
	KUJATA_FIELD_FLOAT(recoilDuration_, 0.35f);

	// --- BT ---
	BahamutAI::BehaviorTreeFactory btFactory_;
	BahamutAI::BehaviorTreeRuntime btRuntime_;
	std::unique_ptr<BahamutAI::UdpTreeObserver> btObserver_;
	BahamutAI::Blackboard localBlackboard_;

	// --- 実行状態 ---
	// 現在のフェーズ名。空なら非攻撃。TickPhaseが名前の変化で開始を検出する。
	std::string currentPhase_;
	float phaseTimer_ = 0.0f;
	float recoilTimer_ = 0.0f;
	// 出している攻撃予告のID(ThreatBoard)。中断時に必ず取り下げる。
	int threatId_ = 0;
	// 直前フレームでスタンしていたか(スタン開始の瞬間だけ後始末したいので保持する)。
	bool wasStunned_ = false;

	EnemyHealth* health_ = nullptr;
	HateTable* hate_ = nullptr;
};

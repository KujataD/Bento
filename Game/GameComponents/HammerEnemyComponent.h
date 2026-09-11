#pragma once

#include <BahamutAI/AI.h>
#include <KujataEngine.h>
#include <memory>
#include <string>

namespace KujataEngine {
class AnimatorComponent;
}
class EnemyHealth;

/// <summary>
/// ハンマーを持つ敵。BehaviorTree(BahamutAI)で行動する。
/// ツリー本体はBahamutAIEditorで作成し、ここではCondition/Actionの実装と登録だけを行う。
/// 調整パラメータはすべてBTノードのParamsで指定する(コンポーネント側には持たない)。
///
/// 登録Condition(その場でtrue/false):
///   IsPlayerInRange : プレイヤーが範囲内か(range)。移動の停止判定にも使う
///
/// 登録Action:
///   StepToPlayer   : プレイヤーへ旋回しつつ1Tickぶん歩く。毎回Successを返す(speed / turnSpeed)。
///                    停止判定は持たない(条件部分はIsPlayerInRangeで組む)
///   FacePlayer     : プレイヤーの方向へ旋回する(turnSpeed)
///   --- 攻撃フェーズ(Sequenceで組み合わせて1つの攻撃を構成する) ---
///   SpinRaise      : 回転攻撃の振り上げ(duration)
///   SpinSwing      : 回転切り本体。攻撃判定ON(duration)
///   SlamRaise      : たたきつけの振り上げ+溜め。プレイヤーを向き続ける(duration / turnSpeed)
///   SlamSwing      : 振り下し。歩きながらたたきつけ、攻撃判定ON(duration / walkSpeed / turnSpeed)
///   Recovery       : 後隙。duration経過後にハンマーを収納して攻撃終了(duration)
///
/// 接近の例: Sequence[ Selector[ IsPlayerInRange(3), StepToPlayer ], FacePlayer ]
///   (3以内でなければ1歩近づく。ループはRootの毎Tick再評価に任せる)
///
/// ハンマー(子孫オブジェクト"HammerPivot")は攻撃フェーズ中だけSetActiveで取り出す。
/// 各フェーズクリップは末尾を長くホールドしてあり、フェーズ遷移(クリップ切替)で継ぎ目なく繋がる。
///
/// 被弾時はのけぞり(HammerRecoil)を再生する。のけぞるかどうかは状態ごとのパラメータで制御し、
/// 既定では回転攻撃中はのけぞらない。のけぞり中は行動アクションがFailureを返す。
/// </summary>
class HammerEnemyComponent : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "HammerEnemyComponent"; }

	void Initialize() override;
	void OnPlayStart() override;
	void Update() override;

	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

private:
	void LoadBTSet();

	// 攻撃フェーズが所属する攻撃種別(のけぞり可否パラメータの判定に使う)。
	enum class AttackKind {
		Keep, // 現在の種別を維持(Recovery用)
		Spin,
		Slam,
	};

	// --- BT Conditions (bool) ---
	bool IsPlayerInRange(const BahamutAI::NodeParams& params);

	// --- BT Actions ---
	// 移動部分のみ。停止の条件部分はIsPlayerInRangeでツリー側に組む。Runningは返さず毎TickSuccess。
	BahamutAI::BTStatus StepToPlayer(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus FacePlayer(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus SpinRaise(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus SpinSwing(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus SlamRaise(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus SlamSwing(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus Recovery(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	/// <summary>
	/// 攻撃フェーズ共通処理。初回Tickでハンマーを取り出しクリップを再生(clipName=nullptrなら現行クリップ継続)、
	/// duration経過までRunning、経過したらSuccessを返す。turnSpeed/walkSpeedが正なら毎Tickプレイヤーへ旋回/前進する。
	/// </summary>
	BahamutAI::BTStatus TickAttackPhase(
	    const char* phaseName, const char* clipName, AttackKind kind, float duration, float turnSpeed, float walkSpeed, float deltaTime);

	// --- helpers ---
	KujataEngine::GameObject* FindPlayer();
	KujataEngine::GameObject* FindHammerPivot();
	KujataEngine::AnimatorComponent* GetAnimator();
	/// 被弾時に呼ばれる。状態ごとのパラメータを見て、のけぞりを再生するか決める。
	void OnDamaged();
	/// のけぞりを再生する(force=trueならスーパーアーマー設定を無視する。ジャストガード用)。
	void PlayRecoil(bool force);
	/// 体勢崩し(スタン)開始。攻撃を中断しBTをリセット、スタンクリップを再生する。
	void OnStaggered();
	/// スタン中か(EnemyHealthの残り時間で判定)。
	bool IsStunned() const;
	/// のけぞり(硬直)中か。
	bool IsRecoiling() const { return recoilTimer_ > 0.0f; }
	void SetHammerVisible(bool visible);
	/// プレイヤーへYawだけ旋回する。deltaTimeぶん回して、ほぼ向いていればtrue。
	bool RotateTowardsPlayer(float turnSpeed, float deltaTime);
	void MoveTowards(KujataEngine::Vector3 desired, float speed, float deltaTime);
	/// 現在の向きへ関係なく、プレイヤーへ直線的にdeltaTimeぶん歩く。
	/// 攻撃終了/中断時の後始末(ハンマー収納・フラグリセット)。
	void FinishAttack();

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED(targetTag_, "Target Tag");
		KUJATA_REGISTER_BOOL_NAMED(recoilEnabled_, "Recoil Enabled");
		KUJATA_REGISTER_BOOL_NAMED(recoilDuringSpin_, "Recoil During Spin");
		KUJATA_REGISTER_BOOL_NAMED(recoilDuringSlam_, "Recoil During Slam");
		KUJATA_REGISTER_FLOAT_NAMED(recoilDuration_, "Recoil Duration", 0.05f, 0.0f, 5.0f);
		KUJATA_REGISTER_STRING_NAMED_TIP(stunClipName_, "Stun Clip",
		    "体勢崩しでスタンしたときに再生するクリップ名。尺はHealthComponentのStun Durationに合わせる。");
	}

	// 攻撃対象のタグ。このタグが付いた生存キャラ(PlayerHealth持ち)のうち最寄りを狙う。
	// プレイアブル2人+味方NPC構成のため名前ではなくタグで選ぶ。
	// ※アクションの調整値(速度・時間など)はBTノードのParamsで指定する。ここには持たない。
	KUJATA_FIELD_STRING(targetTag_, "Ally");

	// --- のけぞり制御 ---
	// のけぞり自体の有効/無効。
	KUJATA_FIELD_BOOL(recoilEnabled_, true);
	// 回転攻撃(SpinAttack)中に被弾したときのけぞるか。既定はのけぞらない(スーパーアーマー)。
	KUJATA_FIELD_BOOL(recoilDuringSpin_, false);
	// たたきつけ(SlamAttack)中に被弾したときのけぞるか。のけぞると攻撃は中断される。
	KUJATA_FIELD_BOOL(recoilDuringSlam_, true);
	// のけぞりの硬直時間[s](HammerRecoil.anim.jsonの長さに合わせる)。
	KUJATA_FIELD_FLOAT(recoilDuration_, 0.5f);
	// スタン時のクリップ名。
	KUJATA_FIELD_STRING(stunClipName_, "HammerStun");

	// BT作成用
	BahamutAI::BehaviorTreeFactory btFactory_;

	// BT実行本体(毎フレームTickする)
	BahamutAI::BehaviorTreeRuntime btRuntime_;

	// ライブ監視オブザーバー。Playインスタンスだけが遅延生成する(EnemyComponentと同じ方針)。
	std::unique_ptr<BahamutAI::UdpTreeObserver> btObserver_;

	// このEnemy固有のBlackboard(AIContextに渡す)
	BahamutAI::Blackboard localBlackboard_;

	// --- 攻撃フェーズの実行状態 ---
	// 実行中フェーズ名(空=非攻撃中)。フェーズアクションはRunningをまたぐため、開始済みかをこれで判定する。
	std::string currentPhase_;
	// 現在フェーズの経過時間[s]。
	float phaseTimer_ = 0.0f;
	// のけぞり可否判定用: 現在どの攻撃種別の最中か。
	bool spinActive_ = false;
	bool slamActive_ = false;

	// のけぞりの残り硬直時間[s]。0より大きい間は行動アクションがFailureを返す。
	float recoilTimer_ = 0.0f;

	// 自分のHP/体勢崩し(OnPlayStartで解決)。
	EnemyHealth* health_ = nullptr;
};

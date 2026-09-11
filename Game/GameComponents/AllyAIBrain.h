#pragma once

#include <BahamutAI/AI.h>
#include <KujataEngine.h>
#include <memory>
#include <string>

class CharacterMotor;
class IAbilitySet;

/// <summary>
/// 味方NPCの「頭脳」(BehaviorTree)。操作されていない側のプレイアブルキャラを
/// エルデンリングの霊灰のように参戦させる。PartyManagerが入力頭脳(Player)と排他で有効化する。
///
/// 体は同じGameObjectのCharacterMotor、攻撃はIAbilitySet(Melee/Magic)を使うため、
/// PawnにもBishopにも同じままで付けられる。性格付けはBTセットとノードのParamsで行う。
///
/// **中核は「敵の予告を読んで避ける」こと。** 敵は攻撃の予備動作を始めた時点で
/// Threat(ThreatBoard.h)へ「いつ・どこに・どんな形の危険が出るか」を掲示する。
/// ここではそれを読み、逃げる/待って無敵を重ねる/ガードする、を選ぶ。
///
/// 登録Condition:
///   IsEnemyInRange(range) / IsEnemyInAttackRange(range) / HasStamina(percent)
///   IsThreatened(horizon)        = 自分に当たる予告が horizon 秒以内にあるか
///   IsPartnerThreatened(horizon) = 相方に当たる予告が horizon 秒以内にあるか
///   IsPartnerDown()              = 相方が倒れているか
///   IsEnemyStaggered(range)      = range内に体勢を崩した敵がいるか
///   IsLeaderFar(distance)        = リーダーが遠いか(追従を使う構成向けに残してある)
///
/// 登録Action:
///   EvadeThreat(...)  = 予告への対処。**この木の最優先に置く**
///   ShieldPartner(..) = 術師が相方の位置へバリアを張る(離れたまま剣士の隙を覆う)
///   CriticalStrike(..)= スタンした敵へ寄って致命を入れる
///   KeepSpacing(...)  = 間合いを保つ(近すぎれば下がり、遠すぎれば寄る)
///   StepToEnemy / FaceEnemy / UseAbility(slot) / Guard(duration) / FollowLeader(...)
///
/// **ガードとバリアの入力は「今フレーム誰かが要求したか」で毎フレーム決め直す。**
/// 押しっぱなしのまま木の別の枝へ移ると、構えたまま動けなくなるため。
/// </summary>
class AllyAIBrain : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "AllyAIBrain"; }
	bool AllowMultiple() const override { return false; }

	void Initialize() override;
	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;

	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

	/// <summary>
	/// 操作キャラへ切り替えられて無効化されるときにPartyManagerが呼ぶ。
	/// 監視オブザーバーを手放し、実行中だったBTの分岐をリセットし、構えを解く。
	/// </summary>
	void OnRelievedFromDuty();

private:
	void LoadBTSet();
	std::string BTSetFolder() const;

	// --- BT Conditions ---
	bool IsLeaderFar(const BahamutAI::NodeParams& params);
	bool IsEnemyInRange(const BahamutAI::NodeParams& params);
	bool IsEnemyInAttackRange(const BahamutAI::NodeParams& params);
	bool HasStamina(const BahamutAI::NodeParams& params);
	bool IsThreatened(const BahamutAI::NodeParams& params);
	bool IsPartnerThreatened(const BahamutAI::NodeParams& params);
	bool IsPartnerDown(const BahamutAI::NodeParams& params);
	/// <summary>
	/// 狙っている敵へ今攻め込んでよいか。**死にゲーの間合いは「常に殴る」ではない** —
	/// 攻撃モーション中に踏み込めば、避けたはずの相手と相打ちになる。
	/// 判断材料は敵自身の申告(ThreatBoardの構え)で、姿勢の推測はしない。
	/// </summary>
	/// <summary>
	/// 敵に狙われているか(param targeted=0で反転)。**役割を分けるための条件。**
	/// 狙われている側は避けに専念し、狙われていない側が攻める。
	/// </summary>
	bool IsTargetedByEnemy(const BahamutAI::NodeParams& params);
	bool IsEnemyOpen(const BahamutAI::NodeParams& params);
	bool IsEnemyStaggered(const BahamutAI::NodeParams& params);

	// --- BT Actions ---
	BahamutAI::BTStatus FollowLeader(const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus StepToEnemy(const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus FaceEnemy(const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus UseAbility(const BahamutAI::NodeParams& params);
	BahamutAI::BTStatus Guard(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params);

	/// <summary>
	/// 予告への対処。上から順に「歩いて逃げる → 待って無敵を重ねる → ガード」。
	///
	/// **すぐ避けるのは最悪手。** 無敵は0.45秒、最短の予告は0.46秒なので、
	/// 予告と同時に転がると命中の直前に無敵が切れる。命中の dodgeLead 秒前まで待つ。
	/// </summary>
	BahamutAI::BTStatus EvadeThreat(const BahamutAI::NodeParams& params);

	/// <summary>相方の位置へバリアを張る(術師専用。自分は間合いを保ったまま)。</summary>
	BahamutAI::BTStatus ShieldPartner(const BahamutAI::NodeParams& params);

	/// <summary>(廃止)常に失敗する。**登録を消すとツリーが読み込めなくなる**ので残してある。</summary>
	BahamutAI::BTStatus ReviveAlly(const BahamutAI::NodeParams& params);

	/// <summary>スタンした敵へ寄って致命を入れる。</summary>
	BahamutAI::BTStatus CriticalStrike(const BahamutAI::NodeParams& params);

	/// <summary>間合いを保つ。近すぎれば下がり、遠すぎれば寄り、範囲内なら横へ回る。</summary>
	BahamutAI::BTStatus KeepSpacing(const BahamutAI::NodeParams& params);

	// --- helpers ---
	/// <summary>リーダー(操作中キャラ)を探す。「自分以外のAllyタグでPlayerが有効な者」。</summary>
	KujataEngine::GameObject* FindLeader();
	/// <summary>相方(自分以外のAllyタグのキャラ)。操作中かどうかは問わない。</summary>
	KujataEngine::GameObject* FindPartner();
	/// <summary>最寄りの狙える敵(IEnemy持ちでIsTargetable)。</summary>
	KujataEngine::GameObject* FindNearestEnemy();
	/// <summary>range内でスタンしている敵(最寄り)。</summary>
	KujataEngine::GameObject* FindStaggeredEnemy(float range);
	/// <summary>自分の水平位置。</summary>
	KujataEngine::Vector3 SelfPosition() const;
	/// <summary>ガードを要求する(今フレーム)。protectはバリアを預ける相手(nullptrで自分)。</summary>
	void RequestGuard(KujataEngine::GameObject* protectTarget);

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(btSetFolder_, "BT Set Folder",
		    "この個体が使うBTセットのフォルダ名(Data/Resources/bt_set/以下)。\n"
		    "**剣士と術師はここだけで切り替える。** 例: AllyMeleeBT / AllyMagicBT");
		KUJATA_REGISTER_STRING_NAMED_TIP(treeKey_, "Monitor Tree Key",
		    "ライブ監視のツリー識別子。**BehaviorTree.jsonのツリー名と完全に一致させること**。\n"
		    "違っているとBahamutAIEditorで購読してもパケットが飛ばない(エラーも出ない)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(bodyRadius_, "Body Radius", 0.05f, 0.1f, 5.0f,
		    "危険域の判定に使う体の太さ[m]。この分だけ危険域を広く見るので、\n"
		    "大きくすると早めに逃げ、小さくするとギリギリまで粘る。");
		KUJATA_REGISTER_STRING_NAMED_TIP(preferredTargetName_, "Preferred Target",
		    "優先して狙う**部位**のGameObject名(例: \"Eye\" / \"Legs\")。\n"
		    "第2形態のように的が複数ある敵で、**術師は目・戦士は脚**と役割を分けるために使う。\n"
		    "見つからなければ最寄りの的を狙う。空なら常に最寄り。");
	}

	// BTセットのフォルダ名。
	KUJATA_FIELD_STRING(btSetFolder_, "AllyBT");
	// ライブ監視のキー(=ツリー名)。
	KUJATA_FIELD_STRING(treeKey_, "Ally");
	// 危険域判定に使う体の太さ。
	KUJATA_FIELD_FLOAT(bodyRadius_, 0.8f);
	// 優先して狙う部位の名前(空=最寄り)。
	KUJATA_FIELD_STRING(preferredTargetName_, "");

	// BT作成用
	BahamutAI::BehaviorTreeFactory btFactory_;
	// BT実行本体(毎フレームTickする)
	BahamutAI::BehaviorTreeRuntime btRuntime_;
	// ライブ監視オブザーバー(Playインスタンスだけが遅延生成)
	std::unique_ptr<BahamutAI::UdpTreeObserver> btObserver_;
	// このキャラ固有のBlackboard
	BahamutAI::Blackboard localBlackboard_;

	CharacterMotor* motor_ = nullptr;
	IAbilitySet* abilitySet_ = nullptr;
	class IGuard* guard_ = nullptr;
	class StaminaComponent* stamina_ = nullptr;
	class CriticalStrikeComponent* critical_ = nullptr;

	// Guardアクションの経過時間[s](Runningをまたぐ)。負なら未開始。
	float guardTimer_ = -1.0f;

	// --- 構えの毎フレーム決め直し ---
	// 今フレーム、いずれかのノードがガードを要求したか。
	bool guardRequested_ = false;
	// 要求と一緒に指定されたバリアの預け先(nullptr=自分)。
	KujataEngine::GameObject* guardProtectTarget_ = nullptr;

	// 既に回避を切った予告のID。**1つの予告に回避は1回だけ。**
	// でないと同じ危険で連続して転がり、スタミナが枯れる。
	int evadedThreatId_ = 0;
};

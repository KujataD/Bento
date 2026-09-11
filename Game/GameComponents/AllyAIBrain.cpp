#include "AllyAIBrain.h"

#include "BarrierGuard.h"
#include "BossIntroCutscene.h"
#include "CharacterMotor.h"
#include "CriticalStrikeComponent.h"
#include "EnemyHealth.h"
#include "IAbilitySet.h"
#include "IEnemy.h"
#include "IGuard.h"
#include "MagicAbilitySet.h"
#include "Player.h"
#include "PlayerHealth.h"
#include "StaminaComponent.h"
#include "ThreatBoard.h"

#include <algorithm>
#include <cmath>

namespace {

// 自分から見た相手への水平ベクトル。
KujataEngine::Vector3 HorizontalTo(const KujataEngine::GameObject& from, const KujataEngine::GameObject& to) {
	KujataEngine::Vector3 diff = to.GetTransform().translation_ - from.GetTransform().translation_;
	diff.y = 0.0f;
	return diff;
}

float HorizontalLength(const KujataEngine::Vector3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }

KujataEngine::Vector3 SafeDirection(const KujataEngine::Vector3& v, const KujataEngine::Vector3& fallback) {
	float length = HorizontalLength(v);
	if (length <= 1.0e-5f) {
		return fallback;
	}
	return {v.x / length, 0.0f, v.z / length};
}

} // namespace

using namespace KujataEngine;

std::string AllyAIBrain::BTSetFolder() const {
	return (KujataEngine::GetProjectDataRoot() / "Resources" / "bt_set" / btSetFolder_).generic_string();
}

void AllyAIBrain::Initialize() {

	// BTアクション登録
	// ---------------------------------------------
	BahamutAI::FunctionCatalog catalog;

	// 調整値はすべてBTノードのParamsで指定する(HammerEnemyと同方針。コンポーネント側には持たない)。

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("IsLeaderFar").Category("Sense").Description("リーダーがdistanceより遠いか").Float("distance", {10.0f}, "距離"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsLeaderFar(params);
	    });

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("IsEnemyInRange").Category("Sense").Description("range以内に敵がいるか").Float("range", {12.0f}, "索敵距離"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsEnemyInRange(params);
	    });

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("IsEnemyInAttackRange").Category("Sense").Description("敵が攻撃射程内か").Float("range", {2.5f}, "攻撃射程"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsEnemyInAttackRange(params);
	    });

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("HasStamina").Category("Sense").Description("スタミナがpercent%以上あるか").Float("percent", {40.0f}, "必要残量[%]"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return HasStamina(params);
	    });

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("IsThreatened")
	        .Category("Threat")
	        .Description("自分に当たる攻撃予告がhorizon秒以内にあるか")
	        .Float("horizon", {1.5f}, "先読みする秒数"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsThreatened(params);
	    });

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("IsPartnerThreatened")
	        .Category("Threat")
	        .Description("相方に当たる攻撃予告がhorizon秒以内にあるか")
	        .Float("horizon", {1.2f}, "先読みする秒数"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsPartnerThreatened(params);
	    });

	BahamutAI::RegisterCondition(
	    btFactory_, catalog, BahamutAI::ConditionDef("IsPartnerDown").Category("Sense").Description("相方が倒れているか"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsPartnerDown(params);
	    });

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("IsTargetedByEnemy")
	        .Category("Threat")
	        .Description("敵に狙われているか。**狙われている側は避けに、いない側は攻めに専念する**")
	        .Float("targeted", {1.0f}, "1=狙われているときtrue / 0=狙われていないときtrue(反転)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsTargetedByEnemy(params);
	    });

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("IsEnemyOpen")
	        .Category("Threat")
	        .Description("狙っている敵に今攻め込んでよいか(後隙・非攻撃中ならtrue、攻撃モーション中はfalse)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsEnemyOpen(params);
	    });

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("IsEnemyStaggered")
	        .Category("Sense")
	        .Description("range内に体勢を崩した敵がいるか")
	        .Float("range", {8.0f}, "探す距離"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsEnemyStaggered(params);
	    });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("FollowLeader")
	        .Category("Movement")
	        .Description("リーダーへ歩く。stopDistance以内で停止")
	        .Float("stopDistance", {3.0f}, "停止距離")
	        .Float("speedScale", {1.0f}, "速度倍率"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return FollowLeader(params);
	    });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("StepToEnemy").Category("Movement").Description("最寄りの敵へ歩く").Float("speedScale", {1.0f}, "速度倍率"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return StepToEnemy(params);
	    });

	BahamutAI::RegisterAction(
	    btFactory_, catalog, BahamutAI::ActionDef("FaceEnemy").Category("Movement").Description("最寄りの敵へ旋回"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return FaceEnemy(params);
	    });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("KeepSpacing")
	        .Category("Movement")
	        .Description("敵との間合いを保つ。近すぎれば下がり、遠すぎれば寄り、範囲内なら横へ回る")
	        .Float("min", {3.0f}, "これより近ければ下がる")
	        .Float("max", {9.0f}, "これより遠ければ寄る")
	        .Float("speedScale", {1.0f}, "速度倍率")
	        .Float("strafe", {0.35f}, "範囲内で横へ回る強さ(0で止まる)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return KeepSpacing(params);
	    });

	BahamutAI::RegisterAction(
	    btFactory_, catalog, BahamutAI::ActionDef("UseAbility").Category("Attack").Description("技を使う").Int("slot", {0}, "技スロット"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return UseAbility(params);
	    });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("Guard").Category("Defense").Description("duration秒ガードし続ける(剣士=盾/術師=バリア)").Float("duration", {1.5f}, "ガード秒数"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return Guard(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("EvadeThreat")
	        .Category("Threat")
	        .Description("予告された攻撃を避ける。歩いて逃げる→待って無敵を重ねる→ガード、の順で選ぶ")
	        .Float("horizon", {1.5f}, "先読みする秒数")
	        .Float("dodgeLead", {0.35f}, "命中の何秒前に回避を切るか(無敵0.45秒の中央に命中を置く)")
	        .Float("safety", {0.15f}, "歩いて逃げ切れると判断するときの余裕[s]")
	        .Float("spread", {0.4f}, "相方から離れる向きを混ぜる強さ(2人が重なって巻き込まれないように)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return EvadeThreat(params);
	    });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("ShieldPartner")
	        .Category("Defense")
	        .Description("相方の位置へバリアを張る(術師専用。自分は間合いを保ったまま)")
	        .Float("horizon", {1.2f}, "この秒数以内に相方へ当たる予告があれば張る")
	        .Float("maxDistance", {18.0f}, "この距離を超えて離れていたら張らない"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return ShieldPartner(params);
	    });

	// **登録を消してはいけない。** 蘇生は時間経過へ変わったので中身は何もしないが、
	// 配布済みのツリー(AllyMeleeBT / AllyMagicBT)がこのノードを参照している。
	// 登録を外すと **ツリーごと読み込みに失敗し、AIが完全に無反応になる**
	// (`action is not registered: ReviveAlly`)。落ちないので原因が非常に読みにくい。
	// 消すときは先に両方のBehaviorTree.jsonからノードを外すこと。
	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("ReviveAlly")
	        .Category("Support")
	        .Description("(廃止)常に失敗する。蘇生は倒れてからの経過時間で自力に進む")
	        .Float("range", {2.2f}, "未使用")
	        .Float("speedScale", {1.0f}, "未使用"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return ReviveAlly(params);
	    });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("CriticalStrike")
	        .Category("Attack")
	        .Description("スタンした敵へ寄って致命を入れる")
	        .Float("range", {8.0f}, "探す距離")
	        .Float("speedScale", {1.0f}, "寄る速度倍率"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return CriticalStrike(params);
	    });

	catalog.SaveToBTSetFolder(BTSetFolder());
}

void AllyAIBrain::OnPlayStart() {
	motor_ = GetComponent<CharacterMotor>();
	abilitySet_ = GetComponent<IAbilitySet>();
	guard_ = GetComponent<IGuard>();
	stamina_ = GetComponent<StaminaComponent>();
	critical_ = GetComponent<CriticalStrikeComponent>();

	// **コンポーネントは使い回されるので、非シリアライズの状態はここで必ず戻す。**
	guardTimer_ = -1.0f;
	guardRequested_ = false;
	guardProtectTarget_ = nullptr;
	evadedThreatId_ = 0;

	LoadBTSet();
}

void AllyAIBrain::OnPlayStop() {
	if (guard_) {
		guard_->SetGuardInput(false);
	}
	if (BarrierGuard* barrier = GetComponent<BarrierGuard>()) {
		barrier->SetProtectTarget(nullptr);
	}
	btObserver_.reset();
}

void AllyAIBrain::Update() {
	// 倒れている間はAIも止める。自力で起き上がるまで、何もできないのが死亡状態。
	if (PlayerHealth* health = GetComponent<PlayerHealth>()) {
		if (health->IsDead()) {
			if (guard_) {
				guard_->SetGuardInput(false);
			}
			return;
		}
	}
	if (!owner_ || !btRuntime_.IsLoaded()) {
		return;
	}

	// **開幕演出が終わるまでは動かない。** 有効/無効の切り替えだけに頼ると、
	// コンポーネントの更新順によっては演出が始まる前にAIが1回動いてしまう
	// (幕が上がる前にNPCが斬りかかる、という形で見える)。
	// 演出そのものに毎フレーム聞けば、更新順に左右されない。
	if (BossIntroCutscene::IsSceneIntroPlaying(owner_->GetScene())) {
		if (guard_) {
			guard_->SetGuardInput(false);
		}
		return;
	}

	// 致命の演出中はBTを止める。動かすと絵が壊れる(操作キャラと同じ扱い)。
	if (critical_ && critical_->IsExecuting()) {
		return;
	}

	// 監視オブザーバーは「実際にTickする個体」だけが初回Tickで遅延生成する。
	// 同一キー(=ツリー名)の最初の登録者だけがprimaryとして送信権を持つため、
	// OnPlayStartで作るとPartyManagerに無効化された側(Tickしない側)がprimaryを
	// 取ってしまい、BahamutAIEditorに実行フローが届かなくなる。
	if (!btObserver_) {
		btObserver_ = std::make_unique<BahamutAI::UdpTreeObserver>(treeKey_);
	}

	// **構えは毎フレーム決め直す。** 押しっぱなしのまま別の枝へ移ると、
	// 構えたまま動けなくなる(reactiveなSelectorでは枝の移動が頻繁に起きる)。
	guardRequested_ = false;
	guardProtectTarget_ = nullptr;

	// **狙いを技側へ渡す。** 術師の弾はこれで仰角が付く — 渡さないと水平にしか飛ばず、
	// 浮いている目には永久に当たらない。剣を振るだけの剣士には影響しない。
	if (MagicAbilitySet* magic = GetComponent<MagicAbilitySet>()) {
		magic->SetAimTarget(FindNearestEnemy());
	}

	BahamutAI::AIContext context{localBlackboard_};
	context.deltaTime = Time::GetDeltaTime();
	context.SetOwner(*owner_);
	context.observer = btObserver_.get();

	btRuntime_.Tick(context);

	if (guard_) {
		guard_->SetGuardInput(guardRequested_);
	}
	if (BarrierGuard* barrier = GetComponent<BarrierGuard>()) {
		barrier->SetProtectTarget(guardRequested_ ? guardProtectTarget_ : nullptr);
	}
}

void AllyAIBrain::OnRelievedFromDuty() {
	btObserver_.reset();
	// ガードし続けたまま操作側へ渡さない。
	if (guard_) {
		guard_->SetGuardInput(false);
	}
	if (BarrierGuard* barrier = GetComponent<BarrierGuard>()) {
		barrier->SetProtectTarget(nullptr);
	}
	guardRequested_ = false;
	guardProtectTarget_ = nullptr;
	guardTimer_ = -1.0f;
	evadedThreatId_ = 0;
	if (btRuntime_.IsLoaded()) {
		btRuntime_.Reset();
	}
}

void AllyAIBrain::RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) {
	registry.Add("LoadBTSet", [this]() { LoadBTSet(); });
}

void AllyAIBrain::LoadBTSet() {
	if (!btRuntime_.LoadFromBTSetFolder(BTSetFolder(), btFactory_)) {
		const BahamutAI::BehaviorTreeLoadResult& result = btRuntime_.GetLastLoadResult();
		KujataEngine::Logger::Log(std::string("[AllyAIBrain] BT load failed: ") + result.GetErrorMessage());
	}
}

// ---------------------------------------------------------------------------
// BT Conditions
// ---------------------------------------------------------------------------

bool AllyAIBrain::IsLeaderFar(const BahamutAI::NodeParams& params) {
	GameObject* leader = FindLeader();
	if (!owner_ || !leader) {
		return false;
	}
	float distance = params.GetFloat("distance", 10.0f);
	return Length(HorizontalTo(*owner_, *leader)) > distance;
}

bool AllyAIBrain::IsEnemyInRange(const BahamutAI::NodeParams& params) {
	GameObject* enemy = FindNearestEnemy();
	if (!owner_ || !enemy) {
		return false;
	}
	float range = params.GetFloat("range", 12.0f);
	return Length(HorizontalTo(*owner_, *enemy)) <= range;
}

bool AllyAIBrain::IsEnemyInAttackRange(const BahamutAI::NodeParams& params) {
	GameObject* enemy = FindNearestEnemy();
	if (!owner_ || !enemy) {
		return false;
	}
	float range = params.GetFloat("range", 2.5f);
	return Length(HorizontalTo(*owner_, *enemy)) <= range;
}

bool AllyAIBrain::HasStamina(const BahamutAI::NodeParams& params) {
	if (!stamina_) {
		return true;
	}
	float percent = params.GetFloat("percent", 40.0f);
	return stamina_->GetPercent() * 100.0f >= percent;
}

bool AllyAIBrain::IsThreatened(const BahamutAI::NodeParams& params) {
	if (!owner_) {
		return false;
	}
	float horizon = params.GetFloat("horizon", 1.5f);
	return Threat::Assess(SelfPosition(), bodyRadius_, horizon).valid;
}

bool AllyAIBrain::IsPartnerThreatened(const BahamutAI::NodeParams& params) {
	GameObject* partner = FindPartner();
	if (!partner) {
		return false;
	}
	// 倒れている相方は無敵なので守る必要がない(時間で自力に起き上がる)。
	if (PlayerHealth* health = partner->GetComponent<PlayerHealth>()) {
		if (health->IsDead()) {
			return false;
		}
	}
	float horizon = params.GetFloat("horizon", 1.2f);
	return Threat::Assess(partner->GetTransform().translation_, bodyRadius_, horizon).valid;
}

bool AllyAIBrain::IsPartnerDown(const BahamutAI::NodeParams& params) {
	(void)params;
	GameObject* partner = FindPartner();
	if (!partner) {
		return false;
	}
	PlayerHealth* health = partner->GetComponent<PlayerHealth>();
	return health && health->IsDead();
}

bool AllyAIBrain::IsTargetedByEnemy(const BahamutAI::NodeParams& params) {
	// **狙われている側と狙われていない側では、正しい行動が正反対になる。**
	// 狙われている者が攻撃を差し込めば相打ちになり、
	// 狙われていない者が身構えていても誰もHPを削らない。だから役割で分ける。
	bool targeted = Threat::IsTargeted(owner_);
	bool want = params.GetFloat("targeted", 1.0f) >= 0.5f;
	return targeted == want;
}

bool AllyAIBrain::IsEnemyOpen(const BahamutAI::NodeParams& params) {
	(void)params;
	GameObject* enemy = FindNearestEnemy();
	if (!enemy) {
		return false;
	}
	// **隙を申告するのは敵の本体。** 部位(目・脚)を狙っていても、構えを持っているのは持ち主。
	GameObject* owner = enemy;
	if (EnemyHealth* health = enemy->GetComponentInParent<EnemyHealth>()) {
		if (health->GetOwner()) {
			owner = health->GetOwner();
		}
	}
	return Threat::IsOpen(owner);
}

bool AllyAIBrain::IsEnemyStaggered(const BahamutAI::NodeParams& params) {
	return FindStaggeredEnemy(params.GetFloat("range", 8.0f)) != nullptr;
}

// ---------------------------------------------------------------------------
// BT Actions
// ---------------------------------------------------------------------------

BahamutAI::BTStatus AllyAIBrain::FollowLeader(const BahamutAI::NodeParams& params) {
	GameObject* leader = FindLeader();
	if (!owner_ || !leader || !motor_) {
		return BahamutAI::BTStatus::Failure;
	}

	float stopDistance = params.GetFloat("stopDistance", 3.0f);
	float speedScale = params.GetFloat("speedScale", 1.0f);

	Vector3 toLeader = HorizontalTo(*owner_, *leader);
	if (Length(toLeader) <= stopDistance) {
		return BahamutAI::BTStatus::Success;
	}

	// 技のモーション中(振り・詠唱・隙)は足を止める(プレイヤー操作と同じ規則)。
	if (abilitySet_ && abilitySet_->IsBusy()) {
		return BahamutAI::BTStatus::Success;
	}
	motor_->MoveWorld(toLeader, speedScale);
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus AllyAIBrain::StepToEnemy(const BahamutAI::NodeParams& params) {
	GameObject* enemy = FindNearestEnemy();
	if (!owner_ || !enemy || !motor_) {
		return BahamutAI::BTStatus::Failure;
	}

	if (abilitySet_ && abilitySet_->IsBusy()) {
		return BahamutAI::BTStatus::Success;
	}
	float speedScale = params.GetFloat("speedScale", 1.0f);
	motor_->MoveWorld(HorizontalTo(*owner_, *enemy), speedScale);
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus AllyAIBrain::KeepSpacing(const BahamutAI::NodeParams& params) {
	GameObject* enemy = FindNearestEnemy();
	if (!owner_ || !enemy || !motor_) {
		return BahamutAI::BTStatus::Failure;
	}

	// 技のモーション中は足を止める(踏み込みや詠唱を邪魔しない)。
	if (abilitySet_ && abilitySet_->IsBusy()) {
		motor_->FaceWorld(HorizontalTo(*owner_, *enemy));
		return BahamutAI::BTStatus::Success;
	}

	float minDistance = params.GetFloat("min", 3.0f);
	float maxDistance = params.GetFloat("max", 9.0f);
	float speedScale = params.GetFloat("speedScale", 1.0f);
	float strafe = params.GetFloat("strafe", 0.35f);

	Vector3 toEnemy = HorizontalTo(*owner_, *enemy);
	float distance = HorizontalLength(toEnemy);
	Vector3 forward = SafeDirection(toEnemy, {0.0f, 0.0f, 1.0f});

	if (distance < minDistance) {
		// 近すぎる。**敵の方を向いたまま**下がる(背を向けると次の攻撃に反応できない)。
		motor_->FaceWorld(toEnemy);
		motor_->MoveWorld(Vector3{-forward.x, 0.0f, -forward.z}, speedScale);
		return BahamutAI::BTStatus::Success;
	}
	if (distance > maxDistance) {
		motor_->MoveWorld(toEnemy, speedScale);
		return BahamutAI::BTStatus::Success;
	}

	// ちょうど良い間合い。棒立ちにならないよう横へ回りながら正面を維持する。
	motor_->FaceWorld(toEnemy);
	if (strafe > 0.0f) {
		Vector3 side = {-forward.z, 0.0f, forward.x};
		motor_->MoveWorld(side, speedScale * strafe);
		// MoveWorldは進行方向を向いてしまうので、向きだけ敵へ戻す。
		motor_->FaceWorld(toEnemy);
	}
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus AllyAIBrain::FaceEnemy(const BahamutAI::NodeParams& params) {
	(void)params;
	GameObject* enemy = FindNearestEnemy();
	if (!owner_ || !enemy || !motor_) {
		return BahamutAI::BTStatus::Failure;
	}

	motor_->FaceWorld(HorizontalTo(*owner_, *enemy));
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus AllyAIBrain::UseAbility(const BahamutAI::NodeParams& params) {
	if (!abilitySet_) {
		return BahamutAI::BTStatus::Failure;
	}

	int slot = params.GetInt("slot", 0);
	// クールダウン等で出せなくてもSuccess扱いにする(その場で構え続け、次のTickで再試行する)。
	abilitySet_->TryUse(slot);
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus AllyAIBrain::Guard(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	if (!guard_) {
		return BahamutAI::BTStatus::Failure;
	}
	float duration = params.GetFloat("duration", 1.5f);

	if (guardTimer_ < 0.0f) {
		guardTimer_ = 0.0f;
	}
	guardTimer_ += context.deltaTime;
	RequestGuard(nullptr);

	// 構え/展開できなかった(スタミナ切れ・モーション中)ならその場で終える。
	if (!guard_->IsGuarding() || guardTimer_ >= duration) {
		guardTimer_ = -1.0f;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus AllyAIBrain::EvadeThreat(const BahamutAI::NodeParams& params) {
	if (!owner_ || !motor_) {
		return BahamutAI::BTStatus::Failure;
	}

	float horizon = params.GetFloat("horizon", 1.5f);
	float dodgeLead = params.GetFloat("dodgeLead", 0.35f);
	float safety = params.GetFloat("safety", 0.15f);
	float spread = params.GetFloat("spread", 0.4f);

	Threat::Assessment assessment = Threat::Assess(SelfPosition(), bodyRadius_, horizon);
	if (!assessment.valid) {
		// 危険が去った。次の予告でまた1回だけ回避できるようにする。
		evadedThreatId_ = 0;
		return BahamutAI::BTStatus::Failure;
	}

	// **相方から離れる向きを弱く混ぜる。**
	// 2人が同じ方向へ逃げて重なると、次の1発で2人まとめて巻き込まれる。
	Vector3 escape = assessment.escape;
	if (spread > 0.0f) {
		if (GameObject* partner = FindPartner()) {
			Vector3 away = SafeDirection(owner_->GetTransform().translation_ - partner->GetTransform().translation_, escape);
			escape = SafeDirection(Vector3{escape.x + away.x * spread, 0.0f, escape.z + away.z * spread}, escape);
		}
	}

	// ① 足で逃げられるなら逃げる(スタミナを使わない)。
	//    持続型(コマ回転・ビーム照射)は無敵0.45秒では覆えないので、必ずこちらを選ぶ。
	float margin = assessment.timeToHit - safety;
	float walkReach = motor_->GetSpeed() * (std::max)(margin, 0.0f);
	if (assessment.sustained || assessment.penetration <= walkReach) {
		motor_->MoveWorld(escape, 1.0f);
		return BahamutAI::BTStatus::Running;
	}

	// ② 間に合わない。**命中の dodgeLead 秒前まで待ってから**回避する。
	//    予告と同時に転がると、無敵(0.45秒)が命中の直前に切れて結局当たる。
	if (assessment.timeToHit > dodgeLead) {
		motor_->FaceWorld(escape); // 向きだけ作って待つ
		return BahamutAI::BTStatus::Running;
	}

	// **1つの予告につき回避は1回だけ。** でないと同じ危険で連続して転がりスタミナが枯れる。
	if (assessment.id != evadedThreatId_ && motor_->TryDodgeWorld(escape)) {
		evadedThreatId_ = assessment.id;
		return BahamutAI::BTStatus::Running;
	}

	// ③ 回避が出せない(クールダウン・スタミナ切れ・既に1回使った)。
	//    属性に合うガードで受ける。剣士の盾は物理を完全に防ぎ、術師のバリアは魔法を完全に防ぐ。
	if (guard_) {
		RequestGuard(nullptr);
		return BahamutAI::BTStatus::Running;
	}

	// 何も無ければせめて逃げ続ける。
	motor_->MoveWorld(escape, 1.0f);
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus AllyAIBrain::ShieldPartner(const BahamutAI::NodeParams& params) {
	BarrierGuard* barrier = GetComponent<BarrierGuard>();
	GameObject* partner = FindPartner();
	if (!barrier || !partner || !owner_) {
		return BahamutAI::BTStatus::Failure;
	}

	float horizon = params.GetFloat("horizon", 1.2f);
	float maxDistance = params.GetFloat("maxDistance", 18.0f);

	if (Length(HorizontalTo(*owner_, *partner)) > maxDistance) {
		return BahamutAI::BTStatus::Failure;
	}

	Threat::Assessment assessment = Threat::Assess(partner->GetTransform().translation_, bodyRadius_, horizon);
	if (!assessment.valid) {
		return BahamutAI::BTStatus::Failure;
	}

	// **中心を相方へ預ける。** 自分は間合いを保ったまま、剣士だけを球で包める。
	RequestGuard(partner);
	// 張っている間も敵は見ておく(展開が閉じた瞬間に構えの向きが残らないように)。
	if (GameObject* enemy = FindNearestEnemy()) {
		motor_->FaceWorld(HorizontalTo(*owner_, *enemy));
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus AllyAIBrain::ReviveAlly(const BahamutAI::NodeParams& params) {
	// **常に失敗する。** 蘇生は倒れてからの経過時間で自力に進むので、助けに行く必要はない
	// ([[death-and-revive]])。ここで駆け寄ると、起こし終わるまで無防備に棒立ちになり、
	// その隙にもう一人も落ちる、を繰り返していた。
	// **登録と実体は残す。** 配布済みのツリーがこのノードを参照しており、
	// 外すとツリーの読み込みごと失敗してAIが止まる。
	(void)params;
	return BahamutAI::BTStatus::Failure;
}

BahamutAI::BTStatus AllyAIBrain::CriticalStrike(const BahamutAI::NodeParams& params) {
	if (!owner_ || !motor_ || !critical_) {
		return BahamutAI::BTStatus::Failure;
	}

	float range = params.GetFloat("range", 8.0f);
	float speedScale = params.GetFloat("speedScale", 1.0f);

	GameObject* target = FindStaggeredEnemy(range);
	if (!target) {
		return BahamutAI::BTStatus::Failure;
	}

	Vector3 toTarget = HorizontalTo(*owner_, *target);
	// 届く距離より少し内側を目標にする(境界で出たり入ったりしないように)。
	float wanted = critical_->GetTriggerDistance() * 0.8f;
	if (HorizontalLength(toTarget) > wanted) {
		if (!(abilitySet_ && abilitySet_->IsBusy())) {
			motor_->MoveWorld(toTarget, speedScale);
		}
		return BahamutAI::BTStatus::Running;
	}

	motor_->FaceWorld(toTarget);
	if (critical_->TryExecute(target)) {
		return BahamutAI::BTStatus::Success;
	}
	// まだ出せない(硬直中など)。窓が閉じるまで粘る。
	return BahamutAI::BTStatus::Running;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

KujataEngine::Vector3 AllyAIBrain::SelfPosition() const {
	return owner_ ? owner_->GetTransform().translation_ : Vector3{0.0f, 0.0f, 0.0f};
}

void AllyAIBrain::RequestGuard(GameObject* protectTarget) {
	guardRequested_ = true;
	guardProtectTarget_ = protectTarget;
}

KujataEngine::GameObject* AllyAIBrain::FindLeader() {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}

	// 「自分以外のAllyタグで、入力頭脳(Player)が有効な者」=現在操作中のキャラ。
	// PartyManagerが頭脳を切り替えた結果を見るだけなので、直接の依存を持たない。
	GameObject* fallback = nullptr;
	for (const auto& object : owner_->GetScene()->GetGameObjects()) {
		if (!object || object.get() == owner_ || !object->IsActiveInHierarchy() || object->GetTag() != "Ally") {
			continue;
		}
		if (!object->GetComponent<CharacterMotor>()) {
			continue;
		}
		Player* inputBrain = object->GetComponent<Player>();
		if (inputBrain && inputBrain->IsEnabled()) {
			return object.get();
		}
		// 入力頭脳が見つからない場合の保険(PartyManager未設定でもそれらしく動く)。
		if (!fallback) {
			fallback = object.get();
		}
	}
	return fallback;
}

KujataEngine::GameObject* AllyAIBrain::FindPartner() {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}
	// 「自分以外のAllyタグのキャラ」。**操作中かAIかは問わない** —
	// AI×2でもプレイヤー+AIでも、守る相手は同じ「もう一人」だから。
	for (const auto& object : owner_->GetScene()->GetGameObjects()) {
		if (!object || object.get() == owner_ || !object->IsActiveInHierarchy() || object->GetTag() != "Ally") {
			continue;
		}
		if (object->GetComponent<CharacterMotor>()) {
			return object.get();
		}
	}
	return nullptr;
}

KujataEngine::GameObject* AllyAIBrain::FindNearestEnemy() {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}

	// **役割で狙う部位を分ける。** 的が複数ある敵(第2形態の目と脚)で、
	// 術師は目・戦士は脚、と分担させるための指名。見つからなければ最寄りへ落ちる。
	if (!preferredTargetName_.empty()) {
		for (const auto& object : owner_->GetScene()->GetGameObjects()) {
			if (!object || !object->IsActiveInHierarchy() || object->GetName() != preferredTargetName_) {
				continue;
			}
			IEnemy* part = object->GetComponent<IEnemy>();
			if (part && part->IsTargetable()) {
				return object.get();
			}
		}
	}

	GameObject* nearest = nullptr;
	float nearestDistSq = 0.0f;
	const Vector3 selfPosition = owner_->GetTransform().translation_;
	for (const auto& object : owner_->GetScene()->GetGameObjects()) {
		if (!object || !object->IsActiveInHierarchy()) {
			continue;
		}
		IEnemy* enemy = object->GetComponent<IEnemy>();
		if (!enemy || !enemy->IsTargetable()) {
			continue;
		}
		Vector3 diff = object->GetTransform().translation_ - selfPosition;
		diff.y = 0.0f;
		float distSq = diff.x * diff.x + diff.z * diff.z;
		if (!nearest || distSq < nearestDistSq) {
			nearest = object.get();
			nearestDistSq = distSq;
		}
	}
	return nearest;
}

KujataEngine::GameObject* AllyAIBrain::FindStaggeredEnemy(float range) {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}

	GameObject* nearest = nullptr;
	float nearestDistSq = 0.0f;
	const Vector3 selfPosition = owner_->GetTransform().translation_;
	for (const auto& object : owner_->GetScene()->GetGameObjects()) {
		if (!object || !object->IsActiveInHierarchy()) {
			continue;
		}
		EnemyHealth* health = object->GetComponent<EnemyHealth>();
		if (!health || !health->IsAlive() || !health->IsStaggered()) {
			continue;
		}
		Vector3 diff = object->GetTransform().translation_ - selfPosition;
		diff.y = 0.0f;
		float distSq = diff.x * diff.x + diff.z * diff.z;
		if (distSq > range * range) {
			continue;
		}
		if (!nearest || distSq < nearestDistSq) {
			nearest = object.get();
			nearestDistSq = distSq;
		}
	}
	return nearest;
}

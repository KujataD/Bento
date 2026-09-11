#include "HammerEnemyComponent.h"

#include "components/AnimatorComponent.h"
#include "EnemyHealth.h"
#include "HateTable.h"
#include "PlayerHealth.h"
#include <cmath>
#include <numbers>

namespace {

std::string BTSetFolder() {
	return (KujataEngine::GetProjectDataRoot() / "Resources" / "bt_set" / "HammerEnemyBT").generic_string();
}

// 攻撃フェーズごとのクリップ名(Animations/*.anim.jsonのnameと一致させる)。
// 各クリップは末尾を長くホールドしてあり、フェーズのdurationはBTのParamsで決める。
constexpr const char* kSpinRaiseClipName = "HammerSpinRaise";
constexpr const char* kSpinSwingClipName = "HammerSpinSwing";
constexpr const char* kSlamRaiseClipName = "HammerSlamRaise";
constexpr const char* kSlamSwingClipName = "HammerSlamSwing";
constexpr const char* kRecoilClipName = "HammerRecoil";

// ハンマーの回転中心となる子孫オブジェクト名。
constexpr const char* kHammerPivotName = "HammerPivot";

// 角度を[-π, π]へ折り返す(KujataEngine::WrapAngleと衝突しないローカル名)。
float WrapYawAngle(float angle) {
	constexpr float pi = std::numbers::pi_v<float>;
	angle = std::fmod(angle + pi, 2.0f * pi);
	if (angle < 0.0f) {
		angle += 2.0f * pi;
	}
	return angle - pi;
}

// 自身または子孫から名前でGameObjectを探す(モデルは子のHammerEnemyModelに分離されている)。
KujataEngine::GameObject* FindDescendantByName(KujataEngine::GameObject* object, const char* name) {
	if (!object) {
		return nullptr;
	}
	for (KujataEngine::GameObject* child : object->GetChildren()) {
		if (!child) {
			continue;
		}
		if (child->GetName() == name) {
			return child;
		}
		if (KujataEngine::GameObject* found = FindDescendantByName(child, name)) {
			return found;
		}
	}
	return nullptr;
}

// 自身または子孫からAnimatorComponentを探す。
KujataEngine::AnimatorComponent* FindAnimatorRecursive(KujataEngine::GameObject* object) {
	if (!object) {
		return nullptr;
	}
	if (KujataEngine::AnimatorComponent* animator = object->GetComponent<KujataEngine::AnimatorComponent>()) {
		return animator;
	}
	for (KujataEngine::GameObject* child : object->GetChildren()) {
		if (KujataEngine::AnimatorComponent* animator = FindAnimatorRecursive(child)) {
			return animator;
		}
	}
	return nullptr;
}

} // namespace

using namespace KujataEngine;

void HammerEnemyComponent::Initialize() {
	// BTのアクション登録。FunctionCatalogへ同時登録するとBahamutAIEditorのAction一覧に反映される。
	BahamutAI::FunctionCatalog catalog;

	// --- Condition: その場でtrue/falseを返す判定。移動の停止条件などツリー側の分岐に使う ---

	BahamutAI::RegisterCondition(
	    btFactory_, catalog, BahamutAI::ConditionDef("IsPlayerInRange").Category("Sense").Description("プレイヤーが範囲内か").Float("range", {6.0f}, "判定距離"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsPlayerInRange(params);
	    });

	// --- Movement ---

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("StepToPlayer")
	        .Category("Movement")
	        .Description("プレイヤーへ旋回しつつ1Tickぶん歩く(移動部分のみ。停止条件はIsPlayerInRangeで組む)")
	        .Float("speed", {3.0f}, "歩行速度(m/s)")
	        .Float("turnSpeed", {6.0f}, "旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return StepToPlayer(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog, BahamutAI::ActionDef("FacePlayer").Category("Movement").Description("プレイヤーの方向へ旋回する").Float("turnSpeed", {6.0f}, "旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return FacePlayer(context, params); });

	// --- 攻撃フェーズ。Sequenceで 振り上げ→振り下し(回転)→後隙 と組み合わせて1つの攻撃を構成する ---

	BahamutAI::RegisterAction(
	    btFactory_, catalog, BahamutAI::ActionDef("SpinRaise").Category("Attack").Description("回転攻撃の振り上げ。ハンマーを取り出して構える").Float("duration", {1.0f}, "フェーズの長さ(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return SpinRaise(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog, BahamutAI::ActionDef("SpinSwing").Category("Attack").Description("回転切り本体。自身を中心にハンマーを回転させる(攻撃判定ON)").Float("duration", {2.0f}, "フェーズの長さ(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return SpinSwing(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("SlamRaise").Category("Attack").Description("たたきつけの振り上げ+溜め。プレイヤーを向き続ける").Float("duration", {1.4f}, "フェーズの長さ(s)").Float("turnSpeed", {6.0f}, "旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return SlamRaise(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("SlamSwing").Category("Attack").Description("振り下し。プレイヤーへ歩きながらたたきつける(攻撃判定ON)").Float("duration", {0.5f}, "フェーズの長さ(s)").Float("walkSpeed", {4.0f}, "前進速度(m/s)").Float("turnSpeed", {6.0f}, "旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return SlamSwing(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog, BahamutAI::ActionDef("Recovery").Category("Attack").Description("後隙。duration経過後にハンマーを収納して攻撃を終了する").Float("duration", {0.5f}, "フェーズの長さ(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return Recovery(context, params); });

	catalog.SaveToBTSetFolder(BTSetFolder());
}

void HammerEnemyComponent::OnPlayStart() {
	LoadBTSet();

	// 実プレイインスタンスとして初めて監視オブザーバーを生成する(EnemyComponentの"Main"と衝突しない別チャンネル)。
	if (!btObserver_) {
		btObserver_ = std::make_unique<BahamutAI::UdpTreeObserver>("HammerEnemy");
	}

	// ハンマーは攻撃時だけ取り出す。開始時は収納。
	spinActive_ = false;
	slamActive_ = false;
	currentPhase_.clear();
	phaseTimer_ = 0.0f;
	recoilTimer_ = 0.0f;
	SetHammerVisible(false);

	// 被弾(HPの変化)でのけぞり、体勢崩しでスタン、ジャストガードで強制のけぞり。
	health_ = owner_ ? owner_->GetComponent<EnemyHealth>() : nullptr;
	if (health_) {
		health_->SetOnHealthChanged([this](float) { OnDamaged(); });
		health_->SetOnStagger([this]() { OnStaggered(); });
		health_->SetOnFlinch([this]() { PlayRecoil(true); });
	}
}

bool HammerEnemyComponent::IsStunned() const {
	return health_ && health_->IsStaggered();
}

void HammerEnemyComponent::OnStaggered() {
	// 攻撃を中断し、実行中の分岐も捨てる(スタン明けに途中から再開しないように)。
	FinishAttack();
	if (btRuntime_.IsLoaded()) {
		btRuntime_.Reset();
	}
	recoilTimer_ = 0.0f;
	if (AnimatorComponent* animator = GetAnimator()) {
		if (!stunClipName_.empty()) {
			animator->PlayByName(stunClipName_.c_str());
		}
	}
}

void HammerEnemyComponent::RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) {
	registry.Add("LoadBTSet", [this]() { LoadBTSet(); });
}

void HammerEnemyComponent::Update() {
	if (!owner_ || !btRuntime_.IsLoaded()) {
		return;
	}

	// スタン中はBTを回さない(何もせず立ち尽くす。姿勢はスタンクリップが担当)。
	if (IsStunned()) {
		return;
	}

	// のけぞりの硬直時間を進める。
	if (recoilTimer_ > 0.0f) {
		recoilTimer_ -= Time::GetDeltaTime();
		if (recoilTimer_ < 0.0f) {
			recoilTimer_ = 0.0f;
		}
	}

	BahamutAI::AIContext context{localBlackboard_};
	context.deltaTime = Time::GetDeltaTime();
	context.SetOwner(*owner_);
	context.observer = btObserver_.get();

	btRuntime_.Tick(context);
}

void HammerEnemyComponent::LoadBTSet() {
	if (!btRuntime_.LoadFromBTSetFolder(BTSetFolder(), btFactory_)) {
		const BahamutAI::BehaviorTreeLoadResult& result = btRuntime_.GetLastLoadResult();
		KujataEngine::Logger::Log(std::string("[HammerEnemyComponent] BT load failed: ") + result.GetErrorMessage());
	}
}

// ---------------------------------------------------------------------------
// BT Actions
// ---------------------------------------------------------------------------

bool HammerEnemyComponent::IsPlayerInRange(const BahamutAI::NodeParams& params) {
	GameObject* player = FindPlayer();
	if (!owner_ || !player) {
		return false;
	}

	float range = params.GetFloat("range", 6.0f);
	Vector3 toPlayer = player->GetTransform().translation_ - owner_->GetTransform().translation_;
	toPlayer.y = 0.0f;
	return Length(toPlayer) <= range;
}

BahamutAI::BTStatus HammerEnemyComponent::StepToPlayer(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	if (IsRecoiling()) {
		return BahamutAI::BTStatus::Failure;
	}
	GameObject* player = FindPlayer();
	if (!owner_ || !player) {
		return BahamutAI::BTStatus::Failure;
	}

	// 移動部分のみ: 旋回しつつ1Tickぶん前進して常にSuccess。
	// 「どこまで近づいたら止まるか」の条件部分はツリー側でIsPlayerInRangeと組み合わせる。
	RotateTowardsPlayer(params.GetFloat("turnSpeed", 6.0f), context.deltaTime);
	Vector3 toPlayer = player->GetTransform().translation_ - owner_->GetTransform().translation_;
	MoveTowards(toPlayer, params.GetFloat("speed", 3.0f), context.deltaTime);
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus HammerEnemyComponent::FacePlayer(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	if (IsRecoiling()) {
		return BahamutAI::BTStatus::Failure;
	}
	GameObject* player = FindPlayer();
	if (!owner_ || !player) {
		return BahamutAI::BTStatus::Failure;
	}

	float turnSpeed = params.GetFloat("turnSpeed", 6.0f);
	if (RotateTowardsPlayer(turnSpeed, context.deltaTime)) {
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus HammerEnemyComponent::SpinRaise(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	return TickAttackPhase("SpinRaise", kSpinRaiseClipName, AttackKind::Spin, params.GetFloat("duration", 1.0f), 0.0f, 0.0f, context.deltaTime);
}

BahamutAI::BTStatus HammerEnemyComponent::SpinSwing(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	return TickAttackPhase("SpinSwing", kSpinSwingClipName, AttackKind::Spin, params.GetFloat("duration", 2.0f), 0.0f, 0.0f, context.deltaTime);
}

BahamutAI::BTStatus HammerEnemyComponent::SlamRaise(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	// 振り上げ+溜め: その場でプレイヤーへ向き続ける。
	return TickAttackPhase("SlamRaise", kSlamRaiseClipName, AttackKind::Slam, params.GetFloat("duration", 1.4f), params.GetFloat("turnSpeed", 6.0f), 0.0f, context.deltaTime);
}

BahamutAI::BTStatus HammerEnemyComponent::SlamSwing(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	// 振り下し: プレイヤーの方向へ歩きながらたたきつける。
	return TickAttackPhase(
	    "SlamSwing", kSlamSwingClipName, AttackKind::Slam, params.GetFloat("duration", 0.5f), params.GetFloat("turnSpeed", 6.0f), params.GetFloat("walkSpeed", 4.0f), context.deltaTime);
}

BahamutAI::BTStatus HammerEnemyComponent::Recovery(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	// 後隙: 現行クリップの終端ポーズを保ったまま待ち、経過したらハンマーを収納して攻撃終了。
	BahamutAI::BTStatus status = TickAttackPhase("Recovery", nullptr, AttackKind::Keep, params.GetFloat("duration", 0.5f), 0.0f, 0.0f, context.deltaTime);
	if (status == BahamutAI::BTStatus::Success) {
		if (AnimatorComponent* animator = GetAnimator()) {
			// 停止で書き込み値が元へ戻る(ハンマーは収納済みなので見えない)。
			animator->Stop();
		}
		FinishAttack();
	}
	return status;
}

BahamutAI::BTStatus HammerEnemyComponent::TickAttackPhase(
    const char* phaseName, const char* clipName, AttackKind kind, float duration, float turnSpeed, float walkSpeed, float deltaTime) {
	if (IsRecoiling()) {
		return BahamutAI::BTStatus::Failure;
	}
	AnimatorComponent* animator = GetAnimator();
	if (!owner_ || !animator) {
		return BahamutAI::BTStatus::Failure;
	}

	// フェーズ開始(前回と別フェーズなら開始扱い。クリップ切替で前フェーズの書き込みは自動復元される)。
	if (currentPhase_ != phaseName) {
		SetHammerVisible(true);
		if (clipName && !animator->PlayByName(clipName)) {
			FinishAttack();
			return BahamutAI::BTStatus::Failure;
		}
		currentPhase_ = phaseName;
		phaseTimer_ = 0.0f;
		if (kind == AttackKind::Spin) {
			spinActive_ = true;
			slamActive_ = false;
		} else if (kind == AttackKind::Slam) {
			slamActive_ = true;
			spinActive_ = false;
		}
	}

	phaseTimer_ += deltaTime;

	if (turnSpeed > 0.0f) {
		RotateTowardsPlayer(turnSpeed, deltaTime);
	}
	if (walkSpeed > 0.0f) {
		GameObject* player = FindPlayer();
		if (player) {
			Vector3 toPlayer = player->GetTransform().translation_ - owner_->GetTransform().translation_;
			MoveTowards(toPlayer, walkSpeed, deltaTime);
		}
	}

	if (phaseTimer_ >= duration) {
		// フェーズ完了。ハンマー収納や姿勢の後始末は次フェーズ(またはRecovery)に任せる。
		currentPhase_.clear();
		phaseTimer_ = 0.0f;
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

KujataEngine::GameObject* HammerEnemyComponent::FindPlayer() {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}

	// **ヘイト表があればそちらに従う。**
	// 最寄りを狙う方式だと、殴ってきた相手より単に近いだけの相手へ寄ってしまい、
	// キャラを切り替えて攻めても敵の狙いが変わらない。表が無い個体は従来どおり最寄りを狙う。
	if (HateTable* hate = GetComponent<HateTable>()) {
		if (GameObject* hated = hate->GetTarget()) {
			return hated;
		}
	}

	// targetTag_の付いた生存キャラ(PlayerHealth持ち)のうち、最も近いものを狙う。
	// プレイアブル2人+味方NPC参戦の構成のため、名前固定ではなくタグで選ぶ。
	// 片方が倒れたら自動的にもう片方へターゲットが移る。
	GameObject* nearest = nullptr;
	float nearestDistSq = 0.0f;
	const Vector3 selfPosition = owner_->GetTransform().translation_;
	for (const auto& object : owner_->GetScene()->GetGameObjects()) {
		if (!object || !object->IsActiveInHierarchy() || object->GetTag() != targetTag_) {
			continue;
		}
		PlayerHealth* health = object->GetComponent<PlayerHealth>();
		if (!health || !health->IsAlive()) {
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

KujataEngine::GameObject* HammerEnemyComponent::FindHammerPivot() {
	// モデル分離により HammerEnemyModel/HammerPivot の階層になったため子孫まで探す。
	return FindDescendantByName(owner_, kHammerPivotName);
}

KujataEngine::AnimatorComponent* HammerEnemyComponent::GetAnimator() {
	// Animatorはモデル側(子のHammerEnemyModel)にある。
	return FindAnimatorRecursive(owner_);
}

void HammerEnemyComponent::OnDamaged() {
	PlayRecoil(false);
}

void HammerEnemyComponent::PlayRecoil(bool force) {
	// スタン中はスタンクリップを上書きしない。
	if (IsStunned()) {
		return;
	}
	if (!force) {
		if (!recoilEnabled_) {
			return;
		}
		// 状態ごとののけぞり可否。既定では回転攻撃中はのけぞらない(スーパーアーマー)。
		if (spinActive_ && !recoilDuringSpin_) {
			return;
		}
		if (slamActive_ && !recoilDuringSlam_) {
			return;
		}
	}

	AnimatorComponent* animator = GetAnimator();
	if (!animator) {
		return;
	}

	// 攻撃中なら中断してハンマーを収納し、のけぞりを再生する。
	FinishAttack();
	if (animator->PlayByName(kRecoilClipName)) {
		recoilTimer_ = recoilDuration_;
	}
}

void HammerEnemyComponent::SetHammerVisible(bool visible) {
	GameObject* hammer = FindHammerPivot();
	if (hammer) {
		// 非アクティブ化で描画もトリガー判定も同時に消える(CollectSceneCollidersはActiveInHierarchyのみ収集)。
		hammer->SetActive(visible);
	}
}

bool HammerEnemyComponent::RotateTowardsPlayer(float turnSpeed, float deltaTime) {
	GameObject* player = FindPlayer();
	if (!owner_ || !player) {
		return false;
	}
	Vector3 toPlayer = player->GetTransform().translation_ - owner_->GetTransform().translation_;
	toPlayer.y = 0.0f;
	if (Length(toPlayer) <= 0.0001f) {
		return true;
	}

	// +Z前方の左手系Yawはatan2(x, z)。
	float targetYaw = std::atan2(toPlayer.x, toPlayer.z);
	float currentYaw = owner_->GetTransform().rotation_.y;
	float difference = WrapYawAngle(targetYaw - currentYaw);

	float maxStep = turnSpeed * deltaTime;
	if (std::fabs(difference) <= maxStep) {
		owner_->GetTransform().rotation_.y = targetYaw;
		return true;
	}

	owner_->GetTransform().rotation_.y = WrapYawAngle(currentYaw + (difference > 0.0f ? maxStep : -maxStep));
	return false;
}

void HammerEnemyComponent::MoveTowards(Vector3 desired, float speed, float deltaTime) {
	GameObject* player = FindPlayer();
	if (!owner_ || !player) {
		return;
	}

	Vector3 toPlayer = player->GetTransform().translation_ - owner_->GetTransform().translation_;
	toPlayer.y = 0.0f;
	float distance = Length(toPlayer);
	if (distance <= 0.0001f) {
		return;
	}

	owner_->GetTransform().translation_ += toPlayer * (speed * deltaTime / distance);
}

void HammerEnemyComponent::FinishAttack() {
	spinActive_ = false;
	slamActive_ = false;
	currentPhase_.clear();
	phaseTimer_ = 0.0f;
	SetHammerVisible(false);
}

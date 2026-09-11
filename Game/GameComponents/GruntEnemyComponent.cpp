#include "GruntEnemyComponent.h"

#include "EnemyHealth.h"
#include "EnemyWeapon.h"
#include "HateTable.h"
#include "GameFx.h"
#include "PlayerHealth.h"
#include "ThreatBoard.h"

#include <components/AnimatorComponent.h>
#include <components/ParticleSystemComponent.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace KujataEngine;

namespace {

constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;

/// <summary>角度を-π〜πへ畳む。旋回の差分を最短回りにするため。</summary>
float WrapYawAngle(float radian) {
	while (radian > std::numbers::pi_v<float>) {
		radian -= kTwoPi;
	}
	while (radian < -std::numbers::pi_v<float>) {
		radian += kTwoPi;
	}
	return radian;
}

GameObject* FindDescendantByName(GameObject* object, const std::string& name) {
	if (!object || name.empty()) {
		return nullptr;
	}
	if (object->GetName() == name) {
		return object;
	}
	for (GameObject* child : object->GetChildren()) {
		if (GameObject* found = FindDescendantByName(child, name)) {
			return found;
		}
	}
	return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// ライフサイクル
// ---------------------------------------------------------------------------

void GruntEnemyComponent::Initialize() {
	RegisterBTFunctions();
}

void GruntEnemyComponent::OnPlayStart() {
	LoadBTSet();

	if (!btObserver_) {
		// キーはBehaviorTree.jsonのツリー名と一致させること(不一致だと監視が沈黙する)。
		btObserver_ = std::make_unique<BahamutAI::UdpTreeObserver>(treeKey_);
	}

	health_ = GetComponent<EnemyHealth>();
	hate_ = GetComponent<HateTable>();
	currentPhase_.clear();
	phaseTimer_ = 0.0f;
	recoilTimer_ = 0.0f;
	wasStunned_ = false;

	if (health_) {
		health_->SetOnFlinch([this]() { OnDamaged(); });
		health_->SetOnStagger([this]() { OnStaggered(); });
	}

	// エンジンの炎はミニガーディアンにも付いている。色の出どころはボスと同じ。
	if (GameObject* flame = FindChild("EngineFlame")) {
		if (ParticleSystemComponent* system = flame->GetComponent<ParticleSystemComponent>()) {
			system->SetColorOverride(GameFx::kSoulColor);
		}
	}

	AbortAttack();
}

void GruntEnemyComponent::OnPlayStop() {
	AbortAttack();
}

void GruntEnemyComponent::Update() {
	if (!owner_) {
		return;
	}
	float deltaTime = Time::GetDeltaTime();

	// --- スタン中はBTを回さない ---
	// 体勢崩しは「行動を奪う」ことに意味があるので、ここで完全に止める。
	bool stunned = IsStunned();
	if (stunned) {
		if (!wasStunned_) {
			wasStunned_ = true;
			AbortAttack();
		}
		return;
	}
	if (wasStunned_) {
		wasStunned_ = false;
		// スタン明けは途中まで進んでいた枝を捨てて、最初から選び直させる。
		btRuntime_.Reset();
	}

	// --- のけぞり中も止める ---
	if (recoilTimer_ > 0.0f) {
		recoilTimer_ -= deltaTime;
		if (recoilTimer_ > 0.0f) {
			return;
		}
		recoilTimer_ = 0.0f;
		btRuntime_.Reset();
	}

	BahamutAI::AIContext context{localBlackboard_};
	context.deltaTime = deltaTime;
	context.SetOwner(*owner_);
	context.observer = btObserver_.get();

	btRuntime_.Tick(context);
}

void GruntEnemyComponent::LoadBTSet() {
	// BTセットはフォルダ名だけをインスペクタに持たせ、ここで実パスへ組み立てる。
	// **近距離型と遠距離型の違いは、突き詰めるとこの1文字列だけ**。
	std::string path = (GetProjectDataRoot() / "Resources" / "bt_set" / btSetFolder_).generic_string();
	if (!btRuntime_.LoadFromBTSetFolder(path, btFactory_)) {
		const BahamutAI::BehaviorTreeLoadResult& result = btRuntime_.GetLastLoadResult();
		Logger::Log(std::string("[GruntEnemyComponent] BT load failed (") + btSetFolder_ + "): " + result.GetErrorMessage());
	}
}

// ---------------------------------------------------------------------------
// ヘルパー
// ---------------------------------------------------------------------------

GameObject* GruntEnemyComponent::FindTarget() {
	if (!owner_ || !owner_->GetScene()) {
		return nullptr;
	}

	// **狙いはヘイト表に任せる。** 殴ってきた相手を覚えるので、キャラ切替に反応する。
	if (hate_) {
		if (GameObject* hated = hate_->GetTarget()) {
			return hated;
		}
	}

	// 表を持たない個体、または表が空の間は対象タグの最寄りを狙う。
	GameObject* nearest = nullptr;
	float nearestDistSq = 0.0f;
	const Vector3 selfPosition = owner_->GetTransform().translation_;
	for (const std::unique_ptr<GameObject>& object : owner_->GetScene()->GetGameObjects()) {
		if (!object || !object->IsActiveInHierarchy() || object->GetTag() != targetTag_) {
			continue;
		}
		PlayerHealth* playerHealth = object->GetComponent<PlayerHealth>();
		if (!playerHealth || !playerHealth->IsAlive()) {
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

float GruntEnemyComponent::HorizontalDistanceToTarget() const {
	GameObject* target = const_cast<GruntEnemyComponent*>(this)->FindTarget();
	if (!owner_ || !target) {
		return 1.0e9f;
	}
	Vector3 diff = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	diff.y = 0.0f;
	return std::sqrt(diff.x * diff.x + diff.z * diff.z);
}

bool GruntEnemyComponent::TickPhase(const char* phaseName, float duration, float deltaTime, float& outProgress) {
	if (currentPhase_ != phaseName) {
		currentPhase_ = phaseName;
		phaseTimer_ = 0.0f;
	}
	phaseTimer_ += deltaTime;

	float safeDuration = (std::max)(duration, 1.0e-3f);
	outProgress = std::clamp(phaseTimer_ / safeDuration, 0.0f, 1.0f);
	if (phaseTimer_ >= safeDuration) {
		currentPhase_.clear();
		phaseTimer_ = 0.0f;
		return true;
	}
	return false;
}

bool GruntEnemyComponent::RotateTowardsTarget(float turnSpeed, float deltaTime) {
	GameObject* target = FindTarget();
	if (!owner_ || !target || turnSpeed <= 0.0f) {
		return false;
	}

	Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	toTarget.y = 0.0f;
	if (Length(toTarget) <= 0.0001f) {
		return true;
	}

	// +Z前方の左手系Yawはatan2(x, z)。
	float targetYaw = std::atan2(toTarget.x, toTarget.z);
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

GameObject* GruntEnemyComponent::FindChild(const std::string& name) const {
	return FindDescendantByName(owner_, name);
}

GameObject* GruntEnemyComponent::GetMeleeWeapon() { return FindChild(weaponObjectName_); }
GameObject* GruntEnemyComponent::GetBeamObject() { return FindChild(beamObjectName_); }

void GruntEnemyComponent::SetWeaponAttack(GameObject* weaponObject, bool active) {
	if (!weaponObject) {
		return;
	}
	if (EnemyWeapon* weapon = weaponObject->GetComponent<EnemyWeapon>()) {
		weapon->SetAttack(active);
	}
}

void GruntEnemyComponent::UpdateBeam(float length, float thickness, float aimHeight) {
	GameObject* beam = GetBeamObject();
	if (!beam || !owner_) {
		return;
	}

	// ヨーはルートの旋回が担当し、ここではピッチだけ書く。
	// 役割を分けておくと「狙いの追従の遅さ」と「高さが合うか」を別々に調整できる。
	float pitch = 0.0f;
	if (GameObject* target = FindTarget()) {
		Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
		float horizontal = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
		float verticalDelta = (toTarget.y + aimHeight) - beamHeight_;
		// +Zを前とする左手系では、X軸まわりの正の回転が下を向く。だから符号を反転する。
		pitch = std::atan2(-verticalDelta, (std::max)(horizontal, 1.0e-3f));
	}

	WorldTransform& transform = beam->GetTransform();
	transform.rotation_ = {pitch, 0.0f, 0.0f};

	// Cubeは原点が中心なので、長さLに伸ばすと根元がL/2だけ後ろへ突き抜ける。
	// 半分だけ前へ出して根元を体に合わせる。前へ出す向きは**傾けた後の向き**であること。
	Vector3 forward = {0.0f, -std::sin(pitch), std::cos(pitch)};
	transform.scale_ = {thickness, thickness, length};
	transform.translation_ = Vector3{0.0f, beamHeight_, 0.0f} + forward * (length * 0.5f);
}

void GruntEnemyComponent::HideBeam() {
	if (GameObject* beam = GetBeamObject()) {
		SetWeaponAttack(beam, false);
		beam->SetActive(false);
	}
}

void GruntEnemyComponent::AbortAttack() {
	currentPhase_.clear();
	phaseTimer_ = 0.0f;
	SetWeaponAttack(GetMeleeWeapon(), false);
	HideBeam();
	// **出した予告は必ず取り下げる。** 攻撃が消えたのに危険域が残ると、
	// 味方AIは存在しない攻撃から永久に逃げ続ける。
	Threat::Withdraw(threatId_);
	threatId_ = 0;
}

AnimatorComponent* GruntEnemyComponent::GetAnimator() { return GetComponentInChildren<AnimatorComponent>(); }

bool GruntEnemyComponent::IsStunned() const { return health_ && health_->IsStaggered(); }

void GruntEnemyComponent::OnDamaged() {
	if (!recoilEnabled_ || IsStunned()) {
		return;
	}
	// **のけぞりは攻撃を中断する。** 中断せずに判定だけ残ると、硬直中に殴られ続ける理不尽になる。
	AbortAttack();
	btRuntime_.Reset();
	recoilTimer_ = recoilDuration_;
	if (AnimatorComponent* animator = GetAnimator()) {
		if (!recoilClipName_.empty()) {
			animator->PlayByName(recoilClipName_.c_str());
		}
	}
}

void GruntEnemyComponent::OnStaggered() {
	AbortAttack();
	btRuntime_.Reset();
	recoilTimer_ = 0.0f;
	if (AnimatorComponent* animator = GetAnimator()) {
		if (!stunClipName_.empty()) {
			animator->PlayByName(stunClipName_.c_str());
		}
	}
}

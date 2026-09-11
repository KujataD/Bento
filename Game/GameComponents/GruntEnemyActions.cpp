// GruntEnemyComponent のうち、BehaviorTree から呼ばれる Condition/Action の実装と登録。
// 本体(移動・のけぞり・ターゲット選択などの土台)は GruntEnemyComponent.cpp 側にある。
#include "GruntEnemyComponent.h"
#include "GameFx.h"

#include "EnemyWeapon.h"
#include "ThreatBoard.h"

#include <algorithm>
#include <cmath>

using namespace KujataEngine;

namespace {

/// <summary>このコンポーネントが使うBTセットの絶対パス。フォルダ名はインスペクタで切り替える。</summary>
std::string BTSetPath(const std::string& folderName) {
	return (KujataEngine::GetProjectDataRoot() / "Resources" / "bt_set" / folderName).generic_string();
}

} // namespace

void GruntEnemyComponent::RegisterBTFunctions() {
	// FunctionCatalogへ同時登録すると BahamutAIEditor のノード一覧に出てくる。
	BahamutAI::FunctionCatalog catalog;

	// --- Condition ---

	BahamutAI::RegisterCondition(
	    btFactory_, catalog,
	    BahamutAI::ConditionDef("IsTargetWithin")
	        .Category("Sense")
	        .Description("狙っている相手との水平距離がdistance以内か")
	        .Float("distance", {6.0f}, "判定距離(m)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
		    (void)context;
		    return IsTargetWithin(params);
	    });

	// --- Movement ---

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("Chase")
	        .Category("Movement")
	        .Description("相手へ旋回しつつ寄る。stopDistance以内なら足を止める(毎TickSuccess)")
	        .Float("speed", {3.0f}, "移動速度(m/s)")
	        .Float("turnSpeed", {6.0f}, "旋回速度(rad/s)。0で旋回しない")
	        .Float("stopDistance", {2.0f}, "この距離まで近づいたら前進をやめる")
	        .Float("chaseRange", {0.0f},
	            "この距離より遠い相手は追わない(0で無制限)。"
	            "部屋の反対側から一斉に走ってくるのを止めるための門番。"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return Chase(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("Backstep")
	        .Category("Movement")
	        .Description("相手から離れる。間合いを取り直す遠距離型の要(毎TickSuccess)")
	        .Float("speed", {2.5f}, "後退速度(m/s)")
	        .Float("turnSpeed", {6.0f}, "旋回速度(rad/s)。相手を向いたまま下がる"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return Backstep(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("Strafe")
	        .Category("Movement")
	        .Description("相手を向いたまま横へ回り込む。棒立ちを避けて狙いを絞らせない(毎TickSuccess)")
	        .Float("speed", {2.0f}, "横移動の速度(m/s)")
	        .Float("turnSpeed", {6.0f}, "旋回速度(rad/s)")
	        .Float("dir", {1.0f}, "正で右回り、負で左回り"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return Strafe(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("FaceTarget")
	        .Category("Movement")
	        .Description("相手の方向へ旋回する。向き切るまでRunning")
	        .Float("turnSpeed", {6.0f}, "旋回速度(rad/s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return FaceTarget(context, params); });

	// --- 近距離攻撃(3つをSequenceで並べて1回の攻撃にする) ---

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("SwingRaise")
	        .Category("Melee")
	        .Description("振りかぶり。**ここが避けるための猶予**なので、短すぎると理不尽になる")
	        .Float("duration", {0.45f}, "フェーズの長さ(s)")
	        .Float("turnSpeed", {4.0f}, "振りかぶり中の旋回速度(rad/s)。0で狙いを固定"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return SwingRaise(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("SwingHit")
	        .Category("Melee")
	        .Description("振り抜き。攻撃判定ON。前へ踏み込みながら振る")
	        .Float("duration", {0.18f}, "フェーズの長さ(s)")
	        .Float("step", {2.0f}, "踏み込みの速度(m/s)。0で足を止めて振る")
	        .Float("turnSpeed", {0.0f}, "振り中の旋回速度(rad/s)。**上げると避けにくくなる**"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return SwingHit(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("SwingRecover")
	        .Category("Melee")
	        .Description("後隙。判定OFF。**ここが反撃の窓**になる")
	        .Float("duration", {0.5f}, "フェーズの長さ(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return SwingRecover(context, params); });

	// --- 遠距離攻撃(同じく3つで1回の射撃) ---

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("BeamWarn")
	        .Category("Ranged")
	        .Description("予兆。細いビームで狙いを見せる。**まだ当たらない**")
	        .Float("duration", {0.7f}, "フェーズの長さ(s)")
	        .Float("length", {12.0f}, "ビームの長さ(m)")
	        .Float("thickness", {0.06f}, "予兆時の太さ。照射時よりずっと細くする")
	        .Float("turnSpeed", {3.5f}, "狙いを合わせる旋回速度(rad/s)")
	        .Float("aimHeight", {1.0f}, "相手のどの高さを狙うか(m)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return BeamWarn(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("BeamFire")
	        .Category("Ranged")
	        .Description("照射。攻撃判定ON。旋回を遅くして、走れば抜けられる余地を残す")
	        .Float("duration", {0.5f}, "フェーズの長さ(s)")
	        .Float("length", {12.0f}, "ビームの長さ(m)")
	        .Float("thickness", {0.28f}, "照射時の太さ")
	        .Float("turnSpeed", {0.6f}, "照射中の旋回速度(rad/s)。**上げると回避不能になる**")
	        .Float("aimHeight", {1.0f}, "相手のどの高さを狙うか(m)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return BeamFire(context, params); });

	BahamutAI::RegisterAction(
	    btFactory_, catalog,
	    BahamutAI::ActionDef("BeamRecover")
	        .Category("Ranged")
	        .Description("照射後の余韻。判定OFF。終わるとビームを消す")
	        .Float("duration", {0.35f}, "フェーズの長さ(s)"),
	    [this](BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) { return BeamRecover(context, params); });

	catalog.SaveToBTSetFolder(BTSetPath(btSetFolder_));
}

// ---------------------------------------------------------------------------
// Condition
// ---------------------------------------------------------------------------

bool GruntEnemyComponent::IsTargetWithin(const BahamutAI::NodeParams& params) {
	if (!FindTarget()) {
		return false;
	}
	return HorizontalDistanceToTarget() <= params.GetFloat("distance", 6.0f);
}

// ---------------------------------------------------------------------------
// Action: 移動
// ---------------------------------------------------------------------------

BahamutAI::BTStatus GruntEnemyComponent::Chase(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* target = FindTarget();
	if (!owner_ || !target) {
		return BahamutAI::BTStatus::Failure;
	}

	Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	toTarget.y = 0.0f;
	float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

	// **遠すぎる相手は追わない。**
	// 追跡に距離の制限が無いと、通路の反対側にいる個体まで一斉に走ってきて、
	// 目の前の一体と戦っているうちに全部集まってしまう。
	// ここで弾いておけば、近づいた分だけが起きる。旋回もしないので気付いていないように見える。
	float chaseRange = params.GetFloat("chaseRange", 0.0f);
	if (chaseRange > 0.0f && distance > chaseRange) {
		return BahamutAI::BTStatus::Failure;
	}

	RotateTowardsTarget(params.GetFloat("turnSpeed", 6.0f), context.deltaTime);
	float stopDistance = params.GetFloat("stopDistance", 2.0f);

	// 止まる距離はツリー側の条件と重複させず、ここで持つ。
	// 「近づく」と「止まる」を1ノードに閉じておくと、間合いの調整がこのノードだけで済む。
	if (distance > stopDistance && distance > 0.0001f) {
		float speed = params.GetFloat("speed", 3.0f);
		owner_->GetTransform().translation_ += toTarget * (speed * context.deltaTime / distance);
	}
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus GruntEnemyComponent::Backstep(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* target = FindTarget();
	if (!owner_ || !target) {
		return BahamutAI::BTStatus::Failure;
	}

	// **相手を向いたまま下がる。** 背を向けて逃げると、遠距離型が撃てない時間が生まれてしまう。
	RotateTowardsTarget(params.GetFloat("turnSpeed", 6.0f), context.deltaTime);

	Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	toTarget.y = 0.0f;
	float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
	if (distance > 0.0001f) {
		float speed = params.GetFloat("speed", 2.5f);
		owner_->GetTransform().translation_ -= toTarget * (speed * context.deltaTime / distance);
	}
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus GruntEnemyComponent::Strafe(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* target = FindTarget();
	if (!owner_ || !target) {
		return BahamutAI::BTStatus::Failure;
	}

	RotateTowardsTarget(params.GetFloat("turnSpeed", 6.0f), context.deltaTime);

	Vector3 toTarget = target->GetTransform().translation_ - owner_->GetTransform().translation_;
	toTarget.y = 0.0f;
	float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
	if (distance <= 0.0001f) {
		return BahamutAI::BTStatus::Success;
	}

	// 相手への向きを水平に90度回した向きが横方向。距離を保ったまま円を描く。
	Vector3 forward = toTarget / distance;
	Vector3 right = {forward.z, 0.0f, -forward.x};
	float dir = (params.GetFloat("dir", 1.0f) >= 0.0f) ? 1.0f : -1.0f;
	owner_->GetTransform().translation_ += right * (dir * params.GetFloat("speed", 2.0f) * context.deltaTime);
	return BahamutAI::BTStatus::Success;
}

BahamutAI::BTStatus GruntEnemyComponent::FaceTarget(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	if (!FindTarget()) {
		return BahamutAI::BTStatus::Failure;
	}
	bool aligned = RotateTowardsTarget(params.GetFloat("turnSpeed", 6.0f), context.deltaTime);
	return aligned ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

// ---------------------------------------------------------------------------
// Action: 近距離
// ---------------------------------------------------------------------------

BahamutAI::BTStatus GruntEnemyComponent::SwingRaise(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	if (!owner_) {
		return BahamutAI::BTStatus::Failure;
	}
	// 振りかぶり中はまだ当たらない。ここで判定を切っておくと、
	// 前の攻撃から連続で入ったときに判定が残りっぱなしになる事故を防げる。
	SetWeaponAttack(GetMeleeWeapon(), false);

	float duration = params.GetFloat("duration", 0.45f);
	if (currentPhase_ != "SwingRaise") {
		// **振りかぶった時点で予告する。** 踏み込む先(step)まで含めた円を出すので、
		// 受け側は「後ろへ下がれば当たらない」と判断できる。
		float step = params.GetFloat("step", 2.2f);
		float yaw = owner_->GetTransform().rotation_.y;
		Vector3 impact = owner_->GetTransform().translation_ + Vector3{std::sin(yaw), 0.0f, std::cos(yaw)} * (step * 0.5f);

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Circle;
		notice.element = Threat::Element::Physical;
		notice.origin = impact;
		notice.radius = params.GetFloat("threatRadius", 2.4f);
		notice.hitTime = duration;
		notice.clearTime = duration + params.GetFloat("hitDuration", 0.18f);
		notice.damage = params.GetFloat("threatDamage", 8.0f);
		Threat::Withdraw(threatId_);
		threatId_ = Threat::Announce(notice);
	}

	RotateTowardsTarget(params.GetFloat("turnSpeed", 4.0f), context.deltaTime);

	float progress = 0.0f;
	bool finished = TickPhase("SwingRaise", duration, context.deltaTime, progress);
	return finished ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GruntEnemyComponent::SwingHit(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* weapon = GetMeleeWeapon();
	if (!owner_ || !weapon) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "SwingHit");
	if (starting) {
		SetWeaponAttack(weapon, true);
		// 踏み込みの足元から土埃。雑魚なので控えめ。
		if (owner_) {
			Vector3 feet = owner_->GetTransform().translation_;
			GameFx::Burst(owner_->GetScene(), GameFx::Prefab::kDust, feet, 0.6f);
		}
	}

	RotateTowardsTarget(params.GetFloat("turnSpeed", 0.0f), context.deltaTime);

	// 踏み込みは**今向いている方向**へ。相手位置へ直接寄せると振り中に追尾してしまい、
	// 振りかぶりで作った回避の猶予が無意味になる。
	float step = params.GetFloat("step", 2.0f);
	if (step > 0.0f) {
		float yaw = owner_->GetTransform().rotation_.y;
		Vector3 forward = {std::sin(yaw), 0.0f, std::cos(yaw)};
		owner_->GetTransform().translation_ += forward * (step * context.deltaTime);
	}

	float progress = 0.0f;
	bool finished = TickPhase("SwingHit", params.GetFloat("duration", 0.18f), context.deltaTime, progress);
	if (finished) {
		SetWeaponAttack(weapon, false);
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GruntEnemyComponent::SwingRecover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	SetWeaponAttack(GetMeleeWeapon(), false);

	float progress = 0.0f;
	bool finished = TickPhase("SwingRecover", params.GetFloat("duration", 0.5f), context.deltaTime, progress);
	return finished ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

// ---------------------------------------------------------------------------
// Action: 遠距離
// ---------------------------------------------------------------------------

BahamutAI::BTStatus GruntEnemyComponent::BeamWarn(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* beam = GetBeamObject();
	if (!owner_ || !beam) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "BeamWarn");
	if (starting) {
		beam->SetActive(true);
		// **予兆では当たらない。** 見せるためだけに出す。
		SetWeaponAttack(beam, false);

		// **線として予告する。** 円と違って正解は「横へ抜ける」なので、形を伝えることに意味がある。
		// 照射は0.5秒続き無敵では覆えないため、持続型(sustained)として出す。
		float duration = params.GetFloat("duration", 0.7f);
		float yaw = owner_->GetTransform().rotation_.y;

		Threat::Notice notice;
		notice.source = owner_;
		notice.shape = Threat::Shape::Line;
		notice.element = Threat::Element::Magic;
		notice.origin = owner_->GetTransform().translation_;
		notice.direction = {std::sin(yaw), 0.0f, std::cos(yaw)};
		notice.radius = params.GetFloat("threatWidth", 1.0f);
		notice.length = params.GetFloat("length", 12.0f);
		notice.sustained = true;
		notice.hitTime = duration;
		notice.clearTime = duration + params.GetFloat("fireDuration", 0.5f);
		notice.damage = params.GetFloat("threatDamage", 12.0f);
		Threat::Withdraw(threatId_);
		threatId_ = Threat::Announce(notice);
	}

	// 予兆の間はしっかり狙いを合わせにいく。ここで追いつかれるからこそ、
	// 照射が始まる前に横へ動く判断が要求される。
	RotateTowardsTarget(params.GetFloat("turnSpeed", 3.5f), context.deltaTime);
	UpdateBeam(params.GetFloat("length", 12.0f), params.GetFloat("thickness", 0.06f), params.GetFloat("aimHeight", 1.0f));

	float progress = 0.0f;
	bool finished = TickPhase("BeamWarn", params.GetFloat("duration", 0.7f), context.deltaTime, progress);
	return finished ? BahamutAI::BTStatus::Success : BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GruntEnemyComponent::BeamFire(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* beam = GetBeamObject();
	if (!owner_ || !beam) {
		return BahamutAI::BTStatus::Failure;
	}

	bool starting = (currentPhase_ != "BeamFire");
	if (starting) {
		beam->SetActive(true);
		SetWeaponAttack(beam, true);
	}

	// **照射中はわざと遅く旋回する。** 速いと必ず当たる=避けようがない攻撃になる。
	// 「走り続ければ抜けられるが、止まれば焼かれる」の境目がこの数値。
	RotateTowardsTarget(params.GetFloat("turnSpeed", 0.6f), context.deltaTime);
	UpdateBeam(params.GetFloat("length", 12.0f), params.GetFloat("thickness", 0.28f), params.GetFloat("aimHeight", 1.0f));

	float progress = 0.0f;
	bool finished = TickPhase("BeamFire", params.GetFloat("duration", 0.5f), context.deltaTime, progress);
	if (finished) {
		SetWeaponAttack(beam, false);
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

BahamutAI::BTStatus GruntEnemyComponent::BeamRecover(BahamutAI::AIContext& context, const BahamutAI::NodeParams& params) {
	GameObject* beam = GetBeamObject();
	if (beam) {
		SetWeaponAttack(beam, false);
	}

	float progress = 0.0f;
	bool finished = TickPhase("BeamRecover", params.GetFloat("duration", 0.35f), context.deltaTime, progress);
	if (finished) {
		HideBeam();
		return BahamutAI::BTStatus::Success;
	}
	return BahamutAI::BTStatus::Running;
}

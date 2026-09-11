#include "CharacterMotor.h"
#include "GameAudio.h"

#include <components/RigidbodyComponent.h>
#include "GameFx.h"
#include "PlayerHealth.h"
#include "StaminaComponent.h"

#include <scene/MovementUtil.h>

#include <algorithm>
#include <cmath>

using namespace KujataEngine;

namespace {

// フレーム数指定(60fps基準)を秒へ変換する係数。
constexpr float kFrameSeconds = 1.0f / 60.0f;

// 自身または子孫からAnimatorComponentを探す(モデルは子に分離されている構成のため)。
AnimatorComponent* FindAnimatorRecursive(GameObject* object) {
	if (!object) {
		return nullptr;
	}
	if (AnimatorComponent* animator = object->GetComponent<AnimatorComponent>()) {
		return animator;
	}
	for (GameObject* child : object->GetChildren()) {
		if (AnimatorComponent* animator = FindAnimatorRecursive(child)) {
			return animator;
		}
	}
	return nullptr;
}

} // namespace

void CharacterMotor::OnPlayStart() {
	animator_ = FindAnimatorRecursive(owner_);
	modelObject_ = animator_ ? animator_->GetOwner() : nullptr;
	health_ = GetComponent<PlayerHealth>();
	stamina_ = GetComponent<StaminaComponent>();
	stunTimer_ = 0.0f;
	knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
	dodgeTimer_ = 0.0f;
	dodgeCooldownTimer_ = 0.0f;
	invincibleTimer_ = 0.0f;
}

void CharacterMotor::Update() {
	float deltaTime = Time::GetDeltaTime();

	// アニメーションのルートモーション(攻撃の踏み込み等)をコライダー側の最上位へ反映する。
	TransferModelRootMotion();

	// 無敵時間は状態に関わらず進める(回避が中断されても無敵だけ残らないように)。
	TickInvincibility(deltaTime);

	// 回避クールダウンは常に進める(クールダウン中も移動や攻撃は可能。再回避だけ不可)。
	if (dodgeCooldownTimer_ > 0.0f) {
		dodgeCooldownTimer_ -= deltaTime;
		if (dodgeCooldownTimer_ < 0.0f) {
			dodgeCooldownTimer_ = 0.0f;
		}
	}

	// 回避中: 前転しながら向いている方向へ前進。移動指示は受け付けない。
	if (dodgeTimer_ > 0.0f) {
		UpdateDodge(deltaTime);
		return;
	}

	// 硬直中は移動指示を受け付けず、ノックバックで滑るだけ。
	if (stunTimer_ > 0.0f) {
		UpdateKnockback(deltaTime);
	}
}

void CharacterMotor::MoveCameraRelative(const Vector3& input) {
	if (!owner_ || IsActionLocked()) {
		return;
	}

	float deltaTime = Time::GetDeltaTime();
	float moveSpeed = speed_ * moveSpeedScale_;

	// 注目中でなければ: カメラ基準の方向へ移動し、移動方向へTurn Speedでスムーズに旋回する(エンジン標準関数)。
	if (!facingTarget_) {
		MovementUtil::MoveAndFace(*owner_, input, moveSpeed, turnSpeed_ * turnSpeedScale_, deltaTime);
		return;
	}

	// 注目中: 入力方向へ移動しつつ、体は対象の方を向き続ける(ストレイフ)。
	WorldTransform& transform = owner_->GetTransform();
	float inputLength = std::sqrt(input.x * input.x + input.z * input.z);
	if (inputLength >= 0.01f) {
		float magnitude = std::min(inputLength, 1.0f);
		Vector3 normalizedInput = {input.x / inputLength, 0.0f, input.z / inputLength};
		Vector3 moveDirection = MovementUtil::ToCameraRelative(normalizedInput, MovementUtil::GetCameraYaw(*owner_));
		transform.translation_ += moveDirection * (moveSpeed * magnitude * deltaTime);
	}

	Vector3 toTarget = facingTarget_->GetTransform().translation_ - transform.translation_;
	toTarget.y = 0.0f;
	if (Length(toTarget) > 0.0001f) {
		MovementUtil::FaceDirection(transform, toTarget, turnSpeed_ * turnSpeedScale_, deltaTime);
	}
}

bool CharacterMotor::TryDodgeCameraRelative(const Vector3& input) {
	if (!owner_) {
		return false;
	}
	float inputLength = std::sqrt(input.x * input.x + input.z * input.z);
	if (inputLength < 0.01f) {
		return TryDodge();
	}
	// 回避できない条件はTryDodgeと同じものをここでも見る。
	// 先に向きだけ変えてしまうと、スタミナ切れで回避が出なかったときに
	// 「その場でくるっと向きだけ変わる」不自然な挙動になるため。
	if (IsActionLocked() || IsDodgeCooldown()) {
		return false;
	}
	if (stamina_ && !stamina_->CanUse()) {
		return false;
	}

	// 入力方向へ即座に向き直してから転がる(回避中はUpdateDodgeが前方へ進める)。
	Vector3 normalizedInput = {input.x / inputLength, 0.0f, input.z / inputLength};
	Vector3 moveDirection = MovementUtil::ToCameraRelative(normalizedInput, MovementUtil::GetCameraYaw(*owner_));
	// 十分大きな旋回速度で1フレームに向き切る。
	MovementUtil::FaceDirection(owner_->GetTransform(), moveDirection, 1000.0f, 1.0f);
	return TryDodge();
}

void CharacterMotor::MoveWorld(const Vector3& direction, float speedScale) {
	if (!owner_ || IsActionLocked()) {
		return;
	}

	Vector3 horizontal = {direction.x, 0.0f, direction.z};
	float length = Length(horizontal);
	if (length <= 0.0001f) {
		return;
	}
	horizontal = horizontal / length;

	float deltaTime = Time::GetDeltaTime();
	WorldTransform& transform = owner_->GetTransform();
	MovementUtil::FaceDirection(transform, horizontal, turnSpeed_, deltaTime);
	transform.translation_ += horizontal * (speed_ * speedScale * deltaTime);
}

void CharacterMotor::FaceWorld(const Vector3& direction) {
	if (!owner_ || IsActionLocked()) {
		return;
	}

	Vector3 horizontal = {direction.x, 0.0f, direction.z};
	if (Length(horizontal) <= 0.0001f) {
		return;
	}
	MovementUtil::FaceDirection(owner_->GetTransform(), horizontal, turnSpeed_, Time::GetDeltaTime());
}

float CharacterMotor::GetDodgeDistance() const {
	return dodgeSpeed_ * static_cast<float>(dodgeDurationFrames_) * kFrameSeconds;
}

bool CharacterMotor::CanDodge() const {
	if (IsActionLocked() || IsDodgeCooldown()) {
		return false;
	}
	return !stamina_ || stamina_->CanUse();
}

bool CharacterMotor::TryDodgeWorld(const Vector3& direction) {
	if (!owner_) {
		return false;
	}
	Vector3 horizontal = {direction.x, 0.0f, direction.z};
	float length = std::sqrt(horizontal.x * horizontal.x + horizontal.z * horizontal.z);
	if (length < 0.0001f) {
		return TryDodge();
	}

	// TryDodgeCameraRelativeと同じ順序。**向きを変える前に可否を見る**のが要点で、
	// 先に回してしまうとスタミナ切れのときに「その場で向きだけ変わる」挙動になる。
	if (!CanDodge()) {
		return false;
	}

	horizontal = horizontal / length;
	// 十分大きな旋回速度で1フレームに向き切る(回避中はUpdateDodgeが前方へ進める)。
	MovementUtil::FaceDirection(owner_->GetTransform(), horizontal, 1000.0f, 1.0f);
	return TryDodge();
}

bool CharacterMotor::TryDodge() {
	if (IsActionLocked() || IsDodgeCooldown()) {
		return false;
	}

	// スタミナが尽きていたら回避できない(残っていれば出せて、消費で0に張り付く)。
	if (stamina_ && !stamina_->CanUse()) {
		return false;
	}
	if (stamina_) {
		stamina_->ConsumePercent(dodgeStaminaCost_);
	}

	dodgeTimer_ = static_cast<float>(dodgeDurationFrames_) * kFrameSeconds;
	invincibleTimer_ = static_cast<float>(invincibleFrames_) * kFrameSeconds;

	GameAudio::PlaySe(GameAudio::Se::Dodge);

	// 踏み切りの足元に土埃。**入力に対して画面が反応する**ので、回避の手応えが上がる。
	if (owner_) {
		GameFx::Burst(owner_->GetScene(), GameFx::Prefab::kDust, owner_->GetTransform().translation_, 0.45f);
	}

	if (health_ && invincibleTimer_ > 0.0f) {
		health_->SetInvincible(true);
	}

	// 前転アニメーション(モデルのみ回転。コライダーは直立のまま)。
	if (animator_ && !dodgeClipName_.empty()) {
		animator_->PlayByName(dodgeClipName_.c_str());
	}
	return true;
}

void CharacterMotor::BeginActionLock(float seconds) {
	if (seconds <= 0.0f) {
		return;
	}
	// 回避中に割り込んだら回避は打ち切る(専用モーションと二重に動かない)。
	if (dodgeTimer_ > 0.0f) {
		dodgeTimer_ = 0.0f;
		dodgeCooldownTimer_ = static_cast<float>(dodgeCooldownFrames_) * kFrameSeconds;
	}
	// 硬直だけを立てる。ノックバック速度は入れないのでその場に留まる。
	knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
	if (seconds > stunTimer_) {
		stunTimer_ = seconds;
	}
}

void CharacterMotor::UpdateDodge(float deltaTime) {
	dodgeTimer_ -= deltaTime;

	// 向いている方向(ルートの前方)へ前進する。
	if (owner_) {
		WorldTransform& transform = owner_->GetTransform();
		Vector3 forward = transform.GetRotationQuaternion().RotateVector({0.0f, 0.0f, 1.0f});
		forward.y = 0.0f;
		float length = Length(forward);
		if (length > 0.0001f) {
			transform.translation_ += forward * (dodgeSpeed_ * deltaTime / length);
		}
	}

	if (dodgeTimer_ <= 0.0f) {
		dodgeTimer_ = 0.0f;
		dodgeCooldownTimer_ = static_cast<float>(dodgeCooldownFrames_) * kFrameSeconds;
	}
}

void CharacterMotor::TickInvincibility(float deltaTime) {
	if (invincibleTimer_ <= 0.0f) {
		return;
	}
	invincibleTimer_ -= deltaTime;
	if (invincibleTimer_ <= 0.0f) {
		invincibleTimer_ = 0.0f;
		if (health_) {
			health_->SetInvincible(false);
		}
	}
}

void CharacterMotor::StopAllMotion() {
	knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
	dodgeTimer_ = 0.0f;
	stunTimer_ = 0.0f;
	// Rigidbodyの速度も落とす。ここを残すと、押し出しの応答で溜まった速度で滑り続ける。
	if (owner_) {
		if (KujataEngine::RigidbodyComponent* rigidbody = owner_->GetComponent<KujataEngine::RigidbodyComponent>()) {
			rigidbody->SetVelocity({0.0f, 0.0f, 0.0f});
		}
	}
}

void CharacterMotor::TransferModelRootMotion() {
	// モデルが自分自身(旧構成: Animatorが最上位に付いている)の場合は何もしない。
	if (!owner_ || !modelObject_ || modelObject_ == owner_) {
		return;
	}

	WorldTransform& modelTransform = modelObject_->GetTransform();
	// 水平(x/z)のみ移す。yはボビング等の見た目専用アニメーションのため残す。
	Vector3 localOffset = {modelTransform.translation_.x, 0.0f, modelTransform.translation_.z};
	if (localOffset.x * localOffset.x + localOffset.z * localOffset.z <= 1.0e-10f) {
		return;
	}

	// モデルのローカルオフセットを最上位のスケール・回転でワールドの移動量へ変換して移動する。
	// (モデルのワールド位置は変えず、親子間で移動量を付け替えるだけ)
	WorldTransform& rootTransform = owner_->GetTransform();
	Vector3 scaledOffset = {
	    localOffset.x * rootTransform.scale_.x,
	    0.0f,
	    localOffset.z * rootTransform.scale_.z,
	};
	rootTransform.translation_ += rootTransform.GetRotationQuaternion().RotateVector(scaledOffset);
	modelTransform.translation_.x = 0.0f;
	modelTransform.translation_.z = 0.0f;
}

void CharacterMotor::ApplyKnockback(const Vector3& velocity, float stunDuration) {
	// 回避中(無敵切れ後)に被弾したら回避を中断してクールダウンへ移行する。
	if (dodgeTimer_ > 0.0f) {
		dodgeTimer_ = 0.0f;
		dodgeCooldownTimer_ = static_cast<float>(dodgeCooldownFrames_) * kFrameSeconds;
	}

	knockbackVelocity_ = velocity;
	if (stunDuration > stunTimer_) {
		stunTimer_ = stunDuration;
	}

	// のけぞりアニメーション。localRotationチャンネルなので今向いている方向を基準に後ろへ傾く。
	if (animator_ && !recoilClipName_.empty()) {
		animator_->PlayByName(recoilClipName_.c_str());
	}
}

void CharacterMotor::UpdateKnockback(float deltaTime) {
	stunTimer_ -= deltaTime;
	if (stunTimer_ <= 0.0f) {
		stunTimer_ = 0.0f;
		knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
		return;
	}

	if (!owner_) {
		return;
	}

	// 初速から指数減衰しながら滑る(フレームレート非依存)。
	owner_->GetTransform().translation_ += knockbackVelocity_ * deltaTime;
	float damping = std::exp(-knockbackDamping_ * deltaTime);
	knockbackVelocity_ = knockbackVelocity_ * damping;
}

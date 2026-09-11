#include "Player.h"
#include "BossIntroCutscene.h"
#include "CharacterMotor.h"
#include "CriticalStrikeComponent.h"
#include "GameInput.h"
#include "IAbilitySet.h"
#include "IGuard.h"
#include "PlayerHealth.h"

using namespace KujataEngine;

void Player::OnPlayStart() {
	motor_ = GetComponent<CharacterMotor>();
	abilitySet_ = GetComponent<IAbilitySet>();
	guard_ = GetComponent<IGuard>();
	health_ = GetComponent<PlayerHealth>();
	critical_ = GetComponent<CriticalStrikeComponent>();
	wasAttackPressed_ = false;
	attackHoldTime_ = 0.0f;
	attackConsumed_ = false;
}

bool Player::IsControlledObject(GameObject* object) {
	if (!object) {
		return false;
	}
	const Player* brain = object->GetComponent<Player>();
	return brain && brain->IsEnabled();
}

void Player::Update() {
	if (!motor_) {
		return;
	}

	// **倒れている間は一切の入力を受け付けない。**
	// 自力で起き上がるまでの十秒間まったく動けない、というのが死亡状態の重みそのもの。
	if (health_ && health_->IsDead()) {
		return;
	}

	// **開幕演出が終わるまでは入力を受けない。** AI側と同じ理由で、
	// 有効/無効の切り替えだけでは更新順によって1回すり抜ける。
	if (owner_ && BossIntroCutscene::IsSceneIntroPlaying(owner_->GetScene())) {
		wasAttackPressed_ = GameInput::IsAttackHeld();
		attackConsumed_ = true;
		if (guard_) {
			guard_->SetGuardInput(false);
		}
		return;
	}

	// **致命の演出中は入力を一切受けない。**
	// 行動ロックの秒数合わせだけに頼ると、拍の足し忘れやヒットストップで時間の数え方が
	// ずれた瞬間に「カメラは寄ったままキャラだけ動く」が復活する。
	// 演出そのものに聞くのが確実で、意図もそのまま読める。
	if (critical_ && critical_->IsExecuting()) {
		wasAttackPressed_ = GameInput::IsAttackHeld();
		attackConsumed_ = true;
		if (guard_) {
			guard_->SetGuardInput(false);
		}
		return;
	}

	float deltaTime = Time::GetDeltaTime();
	bool isAttackPressed = GameInput::IsAttackHeld();
	bool isGuardPressed = GameInput::IsGuardHeld();

	// 行動不能(被弾硬直・回避中)なら入力を捨てる。
	if (motor_->IsActionLocked()) {
		wasAttackPressed_ = isAttackPressed;
		attackConsumed_ = true;
		if (guard_) {
			guard_->SetGuardInput(false);
		}
		return;
	}

	// --- ガード(押している間) ---
	bool guarding = false;
	if (guard_) {
		guard_->SetGuardInput(isGuardPressed);
		guarding = guard_->IsGuarding();
	}

	// --- 攻撃(短押し=通常 / 長押し=溜め / モーション中の押下=即先行入力) ---
	// **致命の一撃も同じボタン。** プロンプトが出ている相手が射程内に居れば、押した瞬間に致命へ回す。
	// 通常攻撃は「離した瞬間」、溜めは「押しっぱなし」で出るので、ここで attackConsumed_ を立てておけば
	// 1回の押下から通常攻撃・溜め・致命が二重に発火することはない。
	// 入力を読むのはここ1か所だけで、CriticalStrikeComponent側は入力を見ない(二重処理の防止)。
	if (isAttackPressed && !wasAttackPressed_) {
		attackHoldTime_ = 0.0f;
		attackConsumed_ = false;
		if (critical_ && critical_->GetPromptTarget() && critical_->TryExecute(critical_->GetPromptTarget())) {
			attackConsumed_ = true;
			wasAttackPressed_ = isAttackPressed;
			if (guard_) {
				guard_->SetGuardInput(false);
			}
			return;
		}
		if (abilitySet_ && abilitySet_->IsBusy()) {
			abilitySet_->TryUse(0);
			attackConsumed_ = true;
		}
	}
	if (isAttackPressed) {
		attackHoldTime_ += deltaTime;
		if (!attackConsumed_ && attackHoldTime_ >= chargeHoldSeconds_ && abilitySet_ && !guarding) {
			abilitySet_->TryUse(1);
			attackConsumed_ = true;
		}
	} else if (wasAttackPressed_ && !attackConsumed_ && abilitySet_ && !guarding) {
		// 短押しで離した: 通常攻撃。
		abilitySet_->TryUse(0);
	}
	wasAttackPressed_ = isAttackPressed;

	Vector3 moveInput = GameInput::GetMoveInput();

	// 回避入力。技のモーション中でも回避でキャンセルできる(フロム式)。
	// Z注目中は入力方向へ転がる(非注目時は向いている方向へ)。
	if (GameInput::IsDodgeTriggered()) {
		bool dodged = motor_->GetFacingTarget() ? motor_->TryDodgeCameraRelative(moveInput) : motor_->TryDodge();
		if (dodged) {
			if (guard_) {
				guard_->SetGuardInput(false);
			}
			return;
		}
	}

	// 技のモーション中(振り・詠唱・隙)は足を止める。
	if (abilitySet_ && abilitySet_->IsBusy()) {
		motor_->SetMoveSpeedScale(attackMoveScale_);
		motor_->SetTurnSpeedScale(attackTurnScale_);
		motor_->MoveCameraRelative(moveInput);
		return;
	}

	// ガード中は鈍足。
	motor_->SetMoveSpeedScale(guarding ? 0.5f : 1.0f);
	motor_->SetTurnSpeedScale(1.0f);
	motor_->MoveCameraRelative(moveInput);
}

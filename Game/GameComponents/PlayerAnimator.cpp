#include "PlayerAnimator.h"
#include "CharacterMotor.h"

using namespace KujataEngine;

void PlayerAnimator::OnPlayStart() {
	wasPressed_ = false;
	animator_ = GetComponent<AnimatorComponent>();

	// CharacterMotorはコリジョンとともに最上位へ、本Componentはモデル(子)側に分離されているため、
	// 自身に無ければ親を遡って探す。
	motor_ = GetComponentInParent<CharacterMotor>();
}

void PlayerAnimator::Update() {
	if (!animator_) {
		return;
	}

	// 行動不能中(被弾のけぞり・回避中)は攻撃入力を受け付けない。
	if (motor_ && motor_->IsActionLocked()) {
		wasPressed_ = false;
		return;
	}

	// R2(右トリガー)は0〜1のアナログ値なので、しきい値でボタン化してエッジ検出する。
	float rightTrigger = Input::GetRightTrigger();
	bool isPressed = rightTrigger >= triggerThreshold_;

	if (isPressed && !wasPressed_) {
		if (!animator_->IsPlaying()) {
			// 押した瞬間に再生。Clip Name指定があればそのクリップへ切り替える。
			if (clipName_.empty()) {
				animator_->Play();
			}
			else {
				if (!animator_->PlayByName(clipName_)) {
					// 名前が見つからない場合は選択中クリップを再生する。
					animator_->Play();
				}
			}
		}
	}

	if (stopOnRelease_ && !isPressed && wasPressed_) {
		animator_->Stop();
	}

	wasPressed_ = isPressed;
}

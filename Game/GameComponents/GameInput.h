#pragma once
#include <KujataEngine.h>

/// <summary>
/// プレイヤー操作のボタン割り当てを一か所にまとめる(Player / LockOn / PartyManager が共有する)。
/// パッドはXInput、キーボードはDirectInputのキーコード。変えたいときはここだけ触る。
///
///   移動      : 左スティック / WASD
///   カメラ    : 右スティック / 矢印キー(OrbitCameraComponent側)
///   攻撃      : R2(短押し=通常, 長押し=溜め) / K
///   致命      : R2 / K。致命プロンプトが出ている間だけ、押した瞬間に通常攻撃より優先して出る
///   回避      : A / Space
///   ガード    : L2(押している間) / J
///   Z注目     : R3(トグル) / Q。注目中に右スティックを左右へ倒した瞬間で対象切替
///   キャラ切替: 十字キー下 / Tab
/// </summary>
namespace GameInput {

// --- パッド ---
inline constexpr WORD kDodgeButton = XINPUT_GAMEPAD_A;
inline constexpr WORD kLockOnButton = XINPUT_GAMEPAD_RIGHT_THUMB;
inline constexpr WORD kSwapCharacterButton = XINPUT_GAMEPAD_DPAD_DOWN;
// トリガーは0〜1のアナログ値。この値以上で「押した」とみなす。
inline constexpr float kTriggerThreshold = 0.5f;

// --- キーボード ---
inline constexpr unsigned char kAttackKey = DIK_K;
inline constexpr unsigned char kDodgeKey = DIK_SPACE;
inline constexpr unsigned char kGuardKey = DIK_J;
inline constexpr unsigned char kLockOnKey = DIK_Q;
inline constexpr unsigned char kSwapCharacterKey = DIK_TAB;

/// <summary>
/// 攻撃ボタン(R2/K)が押されているか。
///
/// **致命の一撃もこのボタンで出す。** 押し始めの1フレームで致命が成立すればそちらを優先し、
/// その押下では通常攻撃も溜めも出さない、という優先順位は Player::Update が持っている。
/// 致命は「押した瞬間」、通常攻撃は「離した瞬間」の判定なので、1回の押下で二重に発火しない。
/// </summary>
inline bool IsAttackHeld() {
	return KujataEngine::Input::GetRightTrigger() >= kTriggerThreshold || KujataEngine::Input::GetKey(kAttackKey);
}

/// <summary>ガードボタン(L2/J)が押されているか。</summary>
inline bool IsGuardHeld() {
	return KujataEngine::Input::GetLeftTrigger() >= kTriggerThreshold || KujataEngine::Input::GetKey(kGuardKey);
}

/// <summary>回避が押された瞬間か。</summary>
inline bool IsDodgeTriggered() {
	return KujataEngine::Input::GetControllerButtonTrigger(kDodgeButton) || KujataEngine::Input::GetKeyTrigger(kDodgeKey);
}

/// <summary>Z注目が押された瞬間か。</summary>
inline bool IsLockOnTriggered() {
	return KujataEngine::Input::GetControllerButtonTrigger(kLockOnButton) || KujataEngine::Input::GetKeyTrigger(kLockOnKey);
}

/// <summary>キャラ切替が押された瞬間か。</summary>
inline bool IsSwapCharacterTriggered() {
	return KujataEngine::Input::GetControllerButtonTrigger(kSwapCharacterButton) || KujataEngine::Input::GetKeyTrigger(kSwapCharacterKey);
}

/// <summary>移動入力(左スティック+WASD合成、カメラ基準のx=横/z=前後)。</summary>
inline KujataEngine::Vector3 GetMoveInput() {
	KujataEngine::Vector2 stick = KujataEngine::Input::GetLeftStick();
	KujataEngine::Vector3 input = {stick.x, 0.0f, stick.y};
	if (KujataEngine::Input::GetKey(DIK_W)) {
		input.z += 1.0f;
	}
	if (KujataEngine::Input::GetKey(DIK_S)) {
		input.z -= 1.0f;
	}
	if (KujataEngine::Input::GetKey(DIK_D)) {
		input.x += 1.0f;
	}
	if (KujataEngine::Input::GetKey(DIK_A)) {
		input.x -= 1.0f;
	}
	return input;
}

} // namespace GameInput

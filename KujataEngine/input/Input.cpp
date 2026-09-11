#include "Input.h"
#include "../base/WinApp.h"
#include <cassert>
#include <cstring>

namespace KujataEngine {

IDirectInput8* Input::directInput_ = nullptr;
IDirectInputDevice8* Input::keyboard_ = nullptr;
IDirectInputDevice8* Input::mouse_ = nullptr;

BYTE Input::key_[256] = {};
BYTE Input::preKey_[256] = {};

DIMOUSESTATE2 Input::mouseState_ = {};
DIMOUSESTATE2 Input::preMouseState_ = {};

XINPUT_STATE Input::controllerState_[XUSER_MAX_COUNT] = {};
XINPUT_STATE Input::preControllerState_[XUSER_MAX_COUNT] = {};
bool Input::isControllerConnected_[XUSER_MAX_COUNT] = {};

HWND Input::hwnd_;

Vector2 Input::mouseClientPos_ = {};
Vector2 Input::mousePreClientPos_ = {};
Input::InputDeviceType Input::currentInputDeviceType_ = Input::InputDeviceType::kKeyboardMouse;

// --- キーボード ---
bool Input::GetKey(unsigned char keycode) { return key_[keycode]; }
bool Input::GetPreKey(unsigned char keycode) { return preKey_[keycode]; }
bool Input::GetKeyTrigger(unsigned char keycode) { return key_[keycode] && !preKey_[keycode]; }
bool Input::GetKeyRelease(unsigned char keycode) { return !key_[keycode] && preKey_[keycode]; }

// --- マウスボタン ---
bool Input::GetClick(int num) { return mouseState_.rgbButtons[num] != 0; }
bool Input::GetPreClick(int num) { return preMouseState_.rgbButtons[num] != 0; }
bool Input::GetClickTrigger(int num) { return mouseState_.rgbButtons[num] && !preMouseState_.rgbButtons[num]; }
bool Input::GetClickRelease(int num) { return !mouseState_.rgbButtons[num] && preMouseState_.rgbButtons[num]; }

// --- 入力デバイス種別 ---
Input::InputDeviceType Input::GetCurrentInputDeviceType() { return currentInputDeviceType_; }
bool Input::IsKeyboardMouseInput() { return currentInputDeviceType_ == InputDeviceType::kKeyboardMouse; }
bool Input::IsControllerInput() { return currentInputDeviceType_ == InputDeviceType::kController; }

namespace {

// XInputで扱えるコントローラー番号かを確認する。
bool IsValidControllerNo(int padNo) {
	return 0 <= padNo && padNo < XUSER_MAX_COUNT;
}

// スティック軸の生値を-1.0f～1.0fの範囲に変換する。
float NormalizeStickAxisValue(SHORT value) {
	if (value < 0) {
		return static_cast<float>(value) / 32768.0f;
	}

	return static_cast<float>(value) / 32767.0f;
}

// スティック入力を円形デッドゾーンで正規化する。
Vector2 NormalizeStickVector(SHORT x, SHORT y, SHORT deadZone) {
	Vector2 value = {
		NormalizeStickAxisValue(x),
		NormalizeStickAxisValue(y),
	};

	const float length = Vector2::Length(value);
	const float deadZoneRatio = static_cast<float>(deadZone) / 32767.0f;

	if (length <= deadZoneRatio) {
		return Vector2{0.0f, 0.0f};
	}

	Vector2 direction = {
		value.x / length,
		value.y / length,
	};

	float normalizedLength = (length - deadZoneRatio) / (1.0f - deadZoneRatio);
	if (normalizedLength > 1.0f) {
		normalizedLength = 1.0f;
	}

	return Vector2{
		direction.x * normalizedLength,
		direction.y * normalizedLength,
	};
}

/// トリガーの生値を0.0f～1.0fの範囲に変換する。
float NormalizeTriggerValue(BYTE value) {
	if (value < XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
		return 0.0f;
	}

	return static_cast<float>(value) / 255.0f;
}

}

void Input::Initialize() {
	HRESULT hr;
	WinApp* winApp = WinApp::GetInstance();

	hwnd_ = winApp->GetHwnd();

	// DirectInputを初期化する。
	hr = DirectInput8Create(winApp->GetHInstance(), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput_, nullptr);
	assert(SUCCEEDED(hr));

	// キーボードデバイスを生成する。
	hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, NULL);
	assert(SUCCEEDED(hr));

	// キーボード入力データの形式 。
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));

	// キーボードの協調レベル 。
	hr = keyboard_->SetCooperativeLevel(hwnd_, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	// マウスデバイスを生成する。
	hr = directInput_->CreateDevice(GUID_SysMouse, &mouse_, NULL);
	assert(SUCCEEDED(hr));

	// マウス入力データの形式 。
	hr = mouse_->SetDataFormat(&c_dfDIMouse2);

	// マウスの協調レベル 。
	mouse_->SetCooperativeLevel(hwnd_, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
}

void Input::Update() {
	// キーボード入力の取得を開始する。
	keyboard_->Acquire();

	// 前フレームのキーボード入力を保存してから、現在フレームの入力を取得する。
	memcpy(preKey_, key_, 256);
	keyboard_->GetDeviceState(sizeof(key_), key_);

	// マウス入力の取得を開始する。
	mouse_->Acquire();

	// 前フレームのマウス入力を保存してから、現在フレームの入力を取得する。
	preMouseState_ = mouseState_;
	mouse_->GetDeviceState(sizeof(mouseState_), &mouseState_);

	// マウス座標の更新
	mousePreClientPos_ = mouseClientPos_;
	mouseClientPos_ = CalcMouseClientPos();

	// コントローラー入力を更新する。
	// XInputは最大4台まで扱えるため、全てのスロットを毎フレーム確認する。
	for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
		preControllerState_[i] = controllerState_[i];

		// XInputGetStateは接続中のコントローラーだけERROR_SUCCESSを返す。
		if (XInputGetState(i, &controllerState_[i]) == ERROR_SUCCESS) {
			isControllerConnected_[i] = true;
		} else {
			// 未接続の場合は状態を空にして、前回の入力が残らないようにする。
			isControllerConnected_[i] = false;
			controllerState_[i] = {};
		}
	}

	UpdateInputDeviceType();
}

void Input::UpdateInputDeviceType() {
	if (IsKeyboardMouseInputDetected()) {
		currentInputDeviceType_ = InputDeviceType::kKeyboardMouse;
	}

	for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
		if (IsControllerInputDetected(i)) {
			currentInputDeviceType_ = InputDeviceType::kController;
			break;
		}
	}
}

bool Input::IsKeyboardMouseInputDetected() {
	for (int i = 0; i < 256; ++i) {
		if (key_[i] != 0 && preKey_[i] == 0) {
			return true;
		}
	}

	if (mouseState_.lX != 0 || mouseState_.lY != 0 || mouseState_.lZ != 0) {
		return true;
	}

	const int kMouseButtonCount = sizeof(mouseState_.rgbButtons) / sizeof(mouseState_.rgbButtons[0]);
	for (int i = 0; i < kMouseButtonCount; ++i) {
		if (mouseState_.rgbButtons[i] != 0 && preMouseState_.rgbButtons[i] == 0) {
			return true;
		}
	}

	return false;
}

bool Input::IsControllerInputDetected(int padNo) {
	if (!IsControllerConnected(padNo)) {
		return false;
	}

	const XINPUT_GAMEPAD& gamepad = controllerState_[padNo].Gamepad;
	const XINPUT_GAMEPAD& preGamepad = preControllerState_[padNo].Gamepad;

	const WORD pressedButtons = static_cast<WORD>(gamepad.wButtons & ~preGamepad.wButtons);
	if (pressedButtons != 0) {
		return true;
	}

	const Vector2 leftStick = NormalizeStickVector(gamepad.sThumbLX, gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
	const Vector2 preLeftStick = NormalizeStickVector(preGamepad.sThumbLX, preGamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
	if (Vector2::Length(leftStick) > 0.0f && Vector2::Length(preLeftStick) == 0.0f) {
		return true;
	}

	const Vector2 rightStick = NormalizeStickVector(gamepad.sThumbRX, gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
	const Vector2 preRightStick = NormalizeStickVector(preGamepad.sThumbRX, preGamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
	if (Vector2::Length(rightStick) > 0.0f && Vector2::Length(preRightStick) == 0.0f) {
		return true;
	}

	const float leftTrigger = NormalizeTriggerValue(gamepad.bLeftTrigger);
	const float preLeftTrigger = NormalizeTriggerValue(preGamepad.bLeftTrigger);
	if (leftTrigger > 0.0f && preLeftTrigger == 0.0f) {
		return true;
	}

	const float rightTrigger = NormalizeTriggerValue(gamepad.bRightTrigger);
	const float preRightTrigger = NormalizeTriggerValue(preGamepad.bRightTrigger);
	if (rightTrigger > 0.0f && preRightTrigger == 0.0f) {
		return true;
	}

	return false;
}

Vector2 Input::GetMouseClientPos() {
	return mouseClientPos_;
}

Vector2 Input::GetMousePreClientPos() {
	return mousePreClientPos_;
}

bool Input::IsControllerConnected(int padNo) {
	if (!IsValidControllerNo(padNo)) {
		return false;
	}

	return isControllerConnected_[padNo];
}

bool Input::GetControllerButton(WORD button, int padNo) {
	if (!IsControllerConnected(padNo)) {
		return false;
	}

	return (controllerState_[padNo].Gamepad.wButtons & button) != 0;
}

bool Input::GetPreControllerButton(WORD button, int padNo) {
	if (!IsValidControllerNo(padNo)) {
		return false;
	}

	return (preControllerState_[padNo].Gamepad.wButtons & button) != 0;
}

bool Input::GetControllerButtonTrigger(WORD button, int padNo) {
	return GetControllerButton(button, padNo) && !GetPreControllerButton(button, padNo);
}

bool Input::GetControllerButtonRelease(WORD button, int padNo) {
	return !GetControllerButton(button, padNo) && GetPreControllerButton(button, padNo);
}

Vector2 Input::GetLeftStick(int padNo) {
	if (!IsControllerConnected(padNo)) {
		return Vector2{0.0f, 0.0f};
	}

	const XINPUT_GAMEPAD& gamepad = controllerState_[padNo].Gamepad;
	return NormalizeStickVector(gamepad.sThumbLX, gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
}

Vector2 Input::GetRightStick(int padNo) {
	if (!IsControllerConnected(padNo)) {
		return Vector2{0.0f, 0.0f};
	}

	const XINPUT_GAMEPAD& gamepad = controllerState_[padNo].Gamepad;
	return NormalizeStickVector(gamepad.sThumbRX, gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
}

float Input::GetLeftTrigger(int padNo) {
	if (!IsControllerConnected(padNo)) {
		return 0.0f;
	}

	return NormalizeTriggerValue(controllerState_[padNo].Gamepad.bLeftTrigger);
}

float Input::GetRightTrigger(int padNo) {
	if (!IsControllerConnected(padNo)) {
		return 0.0f;
	}

	return NormalizeTriggerValue(controllerState_[padNo].Gamepad.bRightTrigger);
}

Vector2 Input::CalcMouseClientPos() {
	POINT mousePoint;

	// マウスカーソルのスクリーン座標を取得する。
	GetCursorPos(&mousePoint);

	// スクリーン座標をウィンドウのクライアント座標へ変換する。
	ScreenToClient(hwnd_, &mousePoint);

	return Vector2{ static_cast<float>(mousePoint.x), static_cast<float>(mousePoint.y) };
}

} // namespace KujataEngine

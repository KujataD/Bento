#pragma once
#include "../math/Vector2.h"
#include "../runtime/KujataApi.h"
#include <Windows.h>

// DirectInputのバージョン指定
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <Xinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib")

namespace KujataEngine {

// キーボード/マウス/コントローラー入力。exeが所有し、GameModuleからも公開APIを呼べるよう
// 各メソッドを KUJATA_API で公開する(DirectInput/XInputの状態は静的privateに隠蔽)。
class Input {
public:
	enum class InputDeviceType {
		kKeyboardMouse,
		kController,
	};

	static KUJATA_API void Initialize();
	static KUJATA_API void Update();

	// --- キーボード ---
	static KUJATA_API bool GetKey(unsigned char keycode);
	static KUJATA_API bool GetPreKey(unsigned char keycode);
	static KUJATA_API bool GetKeyTrigger(unsigned char keycode);
	static KUJATA_API bool GetKeyRelease(unsigned char keycode);

	// --- マウスボタン ---
	static KUJATA_API bool GetClick(int num);
	static KUJATA_API bool GetPreClick(int num);
	static KUJATA_API bool GetClickTrigger(int num);
	static KUJATA_API bool GetClickRelease(int num);

	// --- 使用中の入力デバイス種別 ---
	static KUJATA_API InputDeviceType GetCurrentInputDeviceType();
	static KUJATA_API bool IsKeyboardMouseInput();
	static KUJATA_API bool IsControllerInput();

	// --- マウス座標(クライアント) ---
	static KUJATA_API Vector2 GetMouseClientPos();
	static KUJATA_API Vector2 GetMousePreClientPos();

	// --- コントローラー ---
	// コントローラーが接続されているかを取得する。
	static KUJATA_API bool IsControllerConnected(int padNo = 0);

	// コントローラーのボタン入力を取得する。
	static KUJATA_API bool GetControllerButton(WORD button, int padNo = 0);
	static KUJATA_API bool GetPreControllerButton(WORD button, int padNo = 0);
	static KUJATA_API bool GetControllerButtonTrigger(WORD button, int padNo = 0);
	static KUJATA_API bool GetControllerButtonRelease(WORD button, int padNo = 0);

	// 左右スティックの入力を取得する。
	static KUJATA_API Vector2 GetLeftStick(int padNo = 0);
	static KUJATA_API Vector2 GetRightStick(int padNo = 0);

	// 左右トリガーの入力を取得する。
	static KUJATA_API float GetLeftTrigger(int padNo = 0);
	static KUJATA_API float GetRightTrigger(int padNo = 0);

private:
	static KUJATA_API Vector2 CalcMouseClientPos();
	static KUJATA_API void UpdateInputDeviceType();
	static KUJATA_API bool IsKeyboardMouseInputDetected();
	static KUJATA_API bool IsControllerInputDetected(int padNo);

private:
	static HWND hwnd_;

	static IDirectInput8* directInput_;
	static IDirectInputDevice8* keyboard_;
	static IDirectInputDevice8* mouse_;

	// キーボード入力状態
	static BYTE key_[256];
	static BYTE preKey_[256];

	// マウス入力状態
	static DIMOUSESTATE2 mouseState_;
	static DIMOUSESTATE2 preMouseState_;

	// 最大4台分のコントローラー入力状態を保持する。
	static XINPUT_STATE controllerState_[XUSER_MAX_COUNT];
	static XINPUT_STATE preControllerState_[XUSER_MAX_COUNT];
	static bool isControllerConnected_[XUSER_MAX_COUNT];

	static Vector2 mouseClientPos_;
	static Vector2 mousePreClientPos_;
	static InputDeviceType currentInputDeviceType_;
};

} // namespace KujataEngine

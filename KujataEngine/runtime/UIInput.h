#pragma once

#include "KujataApi.h"

namespace KujataEngine {

class GameObject;

/// <summary>
/// UIのポインタ入力状態。座標は「Game RT(ターゲット)ピクセル空間」(左上原点)。
/// エディタではGameViewWindowが、ランタイムでは主ループがセットする。
/// </summary>
struct UIPointerState {
	float x = 0.0f;
	float y = 0.0f;
	bool inside = false;   // ポインタがビュー内にあるか
	bool pressed = false;  // このフレームで押された(トリガ)
	bool held = false;     // 押下中
	bool released = false; // このフレームで離された
};

KUJATA_API void SetUIPointer(const UIPointerState& state);
KUJATA_API const UIPointerState& GetUIPointer();

/// <summary>
/// UIの操作モード。ポインタとフォーカスが同時にハイライトを奪い合わないよう、
/// どちらか一方だけがボタンの見た目とクリックを担当する。
///   Pointer : マウス。ポインタが動く/クリックされると切り替わる。
///   Focus   : ゲームパッド/キーボード。方向入力・決定・キャンセルで切り替わる。
/// </summary>
enum class UIInputMode {
	Pointer,
	Focus,
};

KUJATA_API UIInputMode GetUIInputMode();
KUJATA_API void SetUIInputMode(UIInputMode mode);

/// <summary>
/// 現在フォーカスしているUI要素(ButtonComponentを持つGameObject)。未選択ならnullptr。
/// シーン破棄でぶら下がりポインタになるため、UINavigationSystemが毎フレーム
/// 「今フレームの候補に含まれているか」を検証してから参照する。
/// メニューを開いた直後など、ゲーム側から選択位置を移したいときはSetUISelectedを使う。
/// </summary>
KUJATA_API void SetUISelected(GameObject* selected);
KUJATA_API GameObject* GetUISelected();

} // namespace KujataEngine

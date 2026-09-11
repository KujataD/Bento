#pragma once

#include "../runtime/KujataApi.h"
#include "../math/Vector4.h"
#include "../scene/Component.h"
#include "../scene/ObjectRef.h"
#include "../scene/UnityAction.h"
#include <array>
#include <string>

namespace KujataEngine {

/// <summary>
/// UI要素間のフォーカス移動方向(ゲームパッド/キーボードのナビゲーション)。
/// 画面座標系(Y下向き)での方向であり、Upは画面の上方向を指す。
/// </summary>
enum class UINavDirection {
	Up,
	Down,
	Left,
	Right,
};

/// <summary>
/// UIのボタン(UnityのButton相当)。同じGameObjectのImageをtarget graphicとし、
/// ホバー/押下で色を変え、クリック時に名前付きイベント(UIEventBus)を発火する。
///
/// マウスポインタでの操作はUIEventSystem、ゲームパッド/キーボードでのフォーカス移動は
/// UINavigationSystemが担当する(どちらもApplyVisualState/FireOnClickを呼ぶ)。
/// </summary>
class KUJATA_API ButtonComponent : public Component {
public:
	enum class VisualState {
		Normal,
		Highlighted,
		Pressed,
		Disabled,
	};

	/// <summary>
	/// フォーカス移動の解決方法(UnityのSelectable.Navigation.Mode相当)。
	///   None      : ナビゲーション対象にしない(マウス専用のボタン)
	///   Automatic : 矩形の位置関係から自動で隣を探す(通常はこれ)
	///   Explicit  : 上下左右の移動先を明示指定する
	/// </summary>
	enum class NavigationMode {
		None,
		Automatic,
		Explicit,
	};

	const char* GetTypeName() const override { return "Button"; }
	bool AllowMultiple() const override { return false; }

	void DrawInspector() override;
	void WriteJson(nlohmann::json& json) const override;
	void ReadJson(const nlohmann::json& json) override;
	void OnAfterReadJson() override;
	void ResolveReferences(IObjectResolver& resolver) override;

	bool IsInteractable() const { return interactable_; }
	void SetInteractable(bool interactable) { interactable_ = interactable; }

	NavigationMode GetNavigationMode() const { return navigationMode_; }

	/// <summary>
	/// Explicitモードでの移動先GameObject(未設定ならnullptr)。
	/// Automatic/Noneのときも設定値をそのまま返すので、呼び出し側でモードを見ること。
	/// </summary>
	GameObject* GetExplicitNeighbor(UINavDirection direction) const;

	/// <summary>現在の見た目状態に応じてtarget graphic(Image)の色を設定する。</summary>
	void ApplyVisualState(VisualState state);

	/// <summary>クリック確定時。onClickイベントを発火する。</summary>
	void FireOnClick();

private:
	void SyncEventBuffer();

	Vector4 normalColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
	Vector4 highlightedColor_ = {0.90f, 0.90f, 0.90f, 1.0f};
	Vector4 pressedColor_ = {0.70f, 0.70f, 0.70f, 1.0f};
	Vector4 disabledColor_ = {0.50f, 0.50f, 0.50f, 0.6f};
	bool interactable_ = true;
	std::string onClickEvent_;      // 旧来の名前付きイベント(UIEventBus)。後方互換のため残す。
	UnityAction onClick_;           // UnityのButton.onClick相当(対象+Component+メソッドの永続呼び出し)。

	// --- ゲームパッド/キーボードでのフォーカス移動 ---
	// 既定をAutomaticにしているので、既存のシーンJSON(このキーを持たない)もそのままパッドで操作できる。
	NavigationMode navigationMode_ = NavigationMode::Automatic;
	ObjectRef selectOnUp_;
	ObjectRef selectOnDown_;
	ObjectRef selectOnLeft_;
	ObjectRef selectOnRight_;

	std::array<char, 128> eventBuffer_{};
	bool eventBufferSynced_ = false;
};

} // namespace KujataEngine

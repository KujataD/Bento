#pragma once

#include "../runtime/KujataApi.h"
#include "../math/Vector2.h"
#include "../scene/Component.h"
#include "../scene/ObjectRef.h"
#include "../scene/UnityAction.h"

namespace KujataEngine {

/// <summary>
/// UIルート。UnityのCanvasに相当し、Render Modeで2つの使い方を切り替える。
///
///   - Screen Space - Overlay: 画面に貼り付くUI(HUD/メニュー)。
///     Canvas Scaler(基準解像度・Scale With Screen Size)で解像度に追従する。
///   - World Space: ワールドに置くUI(看板、キャラの上のHPバー、操作パネル等)。
///     GameObjectのTransformで配置され、カメラに映り、3Dオブジェクトに遮蔽される。
///
/// どちらの場合も配下のUI要素は「キャンバス単位」でレイアウトされる。
/// Overlayでは描画時にscaleFactorで実ピクセルへ拡縮され、
/// World Spaceではキャンバス単位がそのままローカル座標になり、Transformのscaleでworld単位へ変換される
/// (1280x720のキャンバスをscale 0.01で置くと12.8x7.2 world単位。Unityと同じ考え方)。
///
/// ゲームパッド/キーボードのフォーカス操作(UINavigationSystem)では、
/// このCanvasが「First Selected(最初に選ぶボタン)」と「On Cancel(Bボタンの戻り先)」を持つ。
/// </summary>
class KUJATA_API CanvasComponent : public Component {
public:
	enum class RenderMode {
		ScreenSpaceOverlay,
		WorldSpace,
	};

	struct Layout {
		float scaleFactor = 1.0f;
		float canvasWidth = 0.0f;  // キャンバス単位での横幅
		float canvasHeight = 0.0f; // キャンバス単位での縦幅
	};

	const char* GetTypeName() const override { return "Canvas"; }
	bool AllowMultiple() const override { return false; }

	void DrawInspector() override;
	void WriteJson(nlohmann::json& json) const override;
	void ReadJson(const nlohmann::json& json) override;
	void ResolveReferences(IObjectResolver& resolver) override;

	/// <summary>
	/// キャンバス単位のサイズとscaleFactorを算出する。
	/// Overlayは対象RT解像度から、World Spaceは明示指定サイズ(scaleFactorは常に1)。
	/// </summary>
	Layout GetLayout(float targetWidth, float targetHeight) const;

	RenderMode GetRenderMode() const { return renderMode_; }
	void SetRenderMode(RenderMode renderMode) { renderMode_ = renderMode; }
	bool IsWorldSpace() const { return renderMode_ == RenderMode::WorldSpace; }

	/// <summary>World Space時のキャンバスサイズ(キャンバス単位)。</summary>
	const Vector2& GetWorldCanvasSize() const { return worldCanvasSize_; }
	void SetWorldCanvasSize(const Vector2& size) { worldCanvasSize_ = size; }

	int GetSortOrder() const { return sortOrder_; }
	void SetSortOrder(int sortOrder) { sortOrder_ = sortOrder; }

	/// <summary>このCanvasがフォーカスを得たとき最初に選ぶGameObject(未設定ならnullptr)。</summary>
	GameObject* GetFirstSelected() const { return firstSelected_.value; }

	/// <summary>キャンセル(Bボタン/Esc)が押されたときの呼び出し先。</summary>
	const UnityAction& GetOnCancel() const { return onCancel_; }

	/// <summary>キャンセルの呼び出し先が1件でも設定されているか。</summary>
	bool HasOnCancel() const { return !onCancel_.calls.empty(); }

private:
	RenderMode renderMode_ = RenderMode::ScreenSpaceOverlay;

	// --- Screen Space - Overlay 用 ---
	Vector2 referenceResolution_ = {1280.0f, 720.0f};
	float matchWidthHeight_ = 0.5f; // 0=幅基準, 1=高さ基準
	bool scaleWithScreenSize_ = true;

	// --- World Space 用 ---
	Vector2 worldCanvasSize_ = {1280.0f, 720.0f};

	int sortOrder_ = 0;

	// --- ゲームパッド/キーボードのフォーカス操作 ---
	ObjectRef firstSelected_;
	UnityAction onCancel_;
};

} // namespace KujataEngine

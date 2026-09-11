#pragma once

#include "../../externals/imgui/imgui.h"
#include "../2d/UIRect.h"
#include <cstdint>
#include <filesystem>

namespace KujataEngine {

// Gameウィンドウ（RenderTarget表示 + Transformギズモ + オブジェクト選択）を描画する。
class SceneViewWindow {
public:
	void Draw(bool* pOpen = nullptr);

private:
	enum class TransformGizmoOperation {
		Translate,
		Rotate,
		Scale,
	};

	// UI編集ビューでのマウス操作の種類(3x3ハンドル + 本体ドラッグ)。
	enum class UIEditDrag {
		None,
		Move,     // 本体ドラッグで移動
		ResizeTL, // 四隅・四辺(反対側を固定してリサイズ)
		ResizeT,
		ResizeTR,
		ResizeR,
		ResizeBR,
		ResizeB,
		ResizeBL,
		ResizeL,
	};

	void LoadGizmoIcons();
	void DrawGizmoToolbar();
	bool DrawGizmoModeButton(const char* id, const char* fallbackLabel, uint32_t textureIndex, TransformGizmoOperation operation, const char* tooltip);
	void DrawTransformGizmo(const ImVec2& imagePosition, const ImVec2& imageSize);
	void HandleGameWindowObjectSelection(const ImVec2& imagePosition, const ImVec2& imageSize);

	// UI編集ビュー本体。imagePosition/imageSizeはScene RT画像の表示位置・サイズ(スクリーン座標)。
	void DrawUIEditView(const ImVec2& imagePosition, const ImVec2& imageSize);

	TransformGizmoOperation gizmoOperation_ = TransformGizmoOperation::Translate;
	bool transformGizmoUsing_ = false;
	bool gizmoIconsLoaded_ = false;
	uint32_t gizmoTranslateIconIndex_ = 0;
	uint32_t gizmoRotateIconIndex_ = 0;
	uint32_t gizmoScaleIconIndex_ = 0;

	// UI編集ビュー(スライドツール風にマウスで移動/リサイズ/整列)。
	bool uiEditMode_ = false;
	UIEditDrag uiDrag_ = UIEditDrag::None;
	bool uiDragUndoCaptured_ = false;
	UIRect uiDragStartPixelRect_{}; // ドラッグ開始時の選択要素の矩形(RTピクセル空間)
	ImVec2 uiDragStartMousePixel_{}; // ドラッグ開始時のマウス(RTピクセル空間)
};

} // namespace KujataEngine

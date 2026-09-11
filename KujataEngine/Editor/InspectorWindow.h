#pragma once

namespace KujataEngine {

class ProjectWindow;

// Inspectorウィンドウ（選択GameObjectの名前/Tag/Layer/Active編集、Prefab操作、
// Component一覧・追加・削除、Materialアセット選択時の編集UI）を描画する。
class InspectorWindow {
public:
	// projectWindow はMaterialアセット編集UIとPrefab反映後のRefreshに使う。
	void Draw(ProjectWindow& projectWindow, bool* pOpen = nullptr);

private:
	// Inspector編集中に1操作を1つのUndoへまとめるためのフラグ。
	bool inspectorEditing_ = false;
};

} // namespace KujataEngine

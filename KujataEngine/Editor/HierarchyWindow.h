#pragma once

namespace KujataEngine {

class Scene;
class GameObject;

// Hierarchyウィンドウ（Scene内のGameObjectツリー表示・生成・D&D・削除）を描画する。
class HierarchyWindow {
public:
	void Draw(bool* pOpen = nullptr);

private:
	void DrawObject(Scene& scene, GameObject* gameObject, GameObject* selectedObject, bool& selectedObjectExists);

	// 開閉矢印のクリック時に、マウスリリースでの選択を1回だけ抑制するフラグ。
	bool suppressSelectOnRelease_ = false;
};

} // namespace KujataEngine

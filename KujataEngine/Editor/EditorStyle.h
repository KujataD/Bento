#pragma once

#include "../../externals/imgui/imgui.h"

namespace KujataEngine {

// KujataEditor 向けの ImGui ダークテーマを適用する。
class EditorStyle {
public:
	static void Apply();

	// プレハブ(Hierarchyのインスタンス名・Projectのプレハブファイル・Inspectorの表示)の文字色。原色の緑。
	static ImVec4 PrefabTextColor() { return ImVec4(0.0f, 1.0f, 0.0f, 1.0f); }
};

} // namespace KujataEngine

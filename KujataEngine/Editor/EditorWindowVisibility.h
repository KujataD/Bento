#pragma once

namespace KujataEngine {

/// <summary>
/// Windowメニューでトグルする各エディタウィンドウの表示状態。
/// 各ウィンドウのBeginにp_openとして渡し、閉じるボタン[x]とも連動させる。
/// imgui.iniの [KujataEditor][Windows] に保存され、次回起動時に復元される(ImGuiManager参照)。
/// </summary>
struct EditorWindowVisibility {
	bool scene = true;
	bool game = true;
	bool hierarchy = true;
	bool inspector = true;
	bool project = true;
	bool console = true;
	bool performance = true;
	bool animation = true;
	bool scenes = false;
	bool rendering = false;
	bool inputActions = false;

	bool operator==(const EditorWindowVisibility&) const = default;
};

} // namespace KujataEngine

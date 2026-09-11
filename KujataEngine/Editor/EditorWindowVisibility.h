#pragma once

namespace KujataEngine {

/// <summary>
/// Windowメニューでトグルする各エディタウィンドウの表示状態。
/// 各ウィンドウのBeginにp_openとして渡し、閉じるボタン[x]とも連動させる。
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
};

} // namespace KujataEngine

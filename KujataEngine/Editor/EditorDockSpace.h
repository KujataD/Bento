#pragma once

#include <functional>

namespace KujataEngine {

// Unity風のDockSpaceとメニューバーを描画し、初期Dockレイアウトを構築する。
class EditorDockSpace {
public:
	// メニューバーの中身はdrawMenuBarContent、その下の独立したツールバー帯の中身は
	// drawToolbarContentで注入する(状態はImGuiManager側が持つ)。
	void Draw(const std::function<void()>& drawMenuBarContent, const std::function<void()>& drawToolbarContent);

	// 次のDrawで初期Dockレイアウトを組み直させる(WindowメニューのReset Layout)。
	void ResetLayout() { layoutResetRequested_ = true; }

private:
	// ImGuiID(=unsigned int)。ヘッダにimguiを持ち込まないため素の型で受ける。
	void SetupInitialLayout(unsigned int dockspaceId);

	// imgui.iniに保存済みの配置があるかを、起動後の最初のDrawで1回だけ調べたか。
	bool savedLayoutChecked_ = false;
	// 次のDrawで初期配置を組み直すか。保存済みの配置が無いときとReset Layoutのときだけtrueになる。
	bool layoutResetRequested_ = false;
};

} // namespace KujataEngine

#pragma once

#include <functional>

namespace KujataEngine {

// Unity風のDockSpaceとメニューバーを描画し、初期Dockレイアウトを構築する。
class EditorDockSpace {
public:
	// メニューバーの中身はdrawMenuBarContent、その下の独立したツールバー帯の中身は
	// drawToolbarContentで注入する(状態はImGuiManager側が持つ)。
	void Draw(const std::function<void()>& drawMenuBarContent, const std::function<void()>& drawToolbarContent);

	// 次のDrawで初期Dockレイアウトを組み直させる（再初期化時に使う）。
	void ResetLayout() { dockLayoutInitialized_ = false; }

private:
	// ImGuiID(=unsigned int)。ヘッダにimguiを持ち込まないため素の型で受ける。
	void SetupInitialLayout(unsigned int dockspaceId);

	// DockBuilderによる初期配置は1回だけ行う。毎フレーム実行するとユーザーが動かしたDock配置を上書きしてしまう。
	bool dockLayoutInitialized_ = false;
};

} // namespace KujataEngine

#pragma once

#include <string>

namespace KujataEngine {

// Unity の Project Settings に当たるウィンドウ(Edit → Project Settings)。
// 左の一覧でページを選び、右にその設定を出す。
class ProjectSettingsWindow {
public:
	enum class Page {
		InputActions,
		Scenes,
		Rendering,
	};

	void Draw(bool* pOpen = nullptr);

	/// <summary>表示するページを変える(次に描くときに前面へ出す)。</summary>
	void SetPage(Page page);

	/// <summary>"InputActions" / "Scenes" / "Rendering" からページを得る(CUI の window.show 用)。</summary>
	static bool TryParsePage(const std::string& name, Page& outPage);

	static const char* GetWindowTitle() { return "Project Settings"; }

private:
	void DrawInputActionsPage();
	void DrawScenesPage();
	void DrawRenderingPage();

	Page page_ = Page::InputActions;
	// 名前を入力中のアクション(入力を終えたときに名前を変える)。
	std::string renameTarget_;
	char renameBuffer_[64] = "";
	char newActionName_[64] = "";
	char newSceneName_[128] = "";
};

} // namespace KujataEngine

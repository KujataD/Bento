#include "EditorConsole.h"

#include "../../externals/imgui/imgui.h"
#include "EditorApplication.h"
#include "EditorUndoManager.h"

namespace KujataEngine {

EditorConsole* EditorConsole::GetInstance() {
	static EditorConsole instance;
	return &instance;
}

void EditorConsole::AddLog(const std::string& message) {
	logs_.push_back(message);
	// 無制限にログを溜めるとメモリと描画負荷が増えるため、簡易Consoleでは古いログから捨てる。
	if (logs_.size() > 128) {
		logs_.erase(logs_.begin());
	}
}

void EditorConsole::ClearLogs() { logs_.clear(); }

void EditorConsole::Draw(bool* pOpen) {
#ifdef USE_IMGUI
	ImGui::Begin("Console", pOpen);
	// ログとは別に、現在モードを常に先頭へ表示する。
	if (EditorApplication::GetInstance()->IsPlaying()) {
		ImGui::TextUnformatted("Editor Mode: Play");
	} else {
		ImGui::TextUnformatted("Editor Mode: Edit");
	}
	int undoMaxCount = static_cast<int>(EditorUndoManager::GetInstance()->GetMaxUndoCount());
	ImGui::SetNextItemWidth(96.0f);
	if (ImGui::DragInt("Undo Max", &undoMaxCount, 1.0f, 1, 200)) {
		if (undoMaxCount < 1) {
			undoMaxCount = 1;
		}
		EditorUndoManager::GetInstance()->SetMaxUndoCount(static_cast<size_t>(undoMaxCount));
	}
	ImGui::Separator();
	for (const std::string& log : logs_) {
		ImGui::TextUnformatted(log.c_str());
	}
	// Consoleが末尾までスクロールされている場合は、新しいログへ追従する。
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
		ImGui::SetScrollHereY(1.0f);
	}
	ImGui::End();
#else
	(void)pOpen;
#endif // USE_IMGUI
}

} // namespace KujataEngine

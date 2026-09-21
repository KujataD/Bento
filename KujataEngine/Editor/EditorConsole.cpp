#include "EditorConsole.h"

#include "../../externals/imgui/imgui.h"
#include "EditorApplication.h"
#include "EditorCommandServer.h"
#include "EditorUndoManager.h"
#include <cstring>
#include <sstream>

namespace KujataEngine {

namespace {

// 古いログから捨てる上限。scene.list などCUIの結果も並ぶので、少し多めに持つ。
constexpr size_t kMaxLogCount = 1000;

// CUIの返事を、人間が読む形でConsoleへ出す(kujata CLIと同じ text を使う)。
void PrintResponse(const nlohmann::json& response) {
	std::istringstream lines(response.value("text", std::string()));
	for (std::string line; std::getline(lines, line);) {
		EditorConsole::GetInstance()->AddLog("  " + line);
	}
}

} // namespace

EditorConsole* EditorConsole::GetInstance() {
	static EditorConsole instance;
	return &instance;
}

void EditorConsole::AddLog(const std::string& message) {
	logs_.push_back(message);
	if (capturing_) {
		captured_.push_back(message);
	}
	// 無制限にログを溜めるとメモリと描画負荷が増えるため、簡易Consoleでは古いログから捨てる。
	if (logs_.size() > kMaxLogCount) {
		logs_.erase(logs_.begin());
	}
	scrollToBottom_ = true;
}

void EditorConsole::ClearLogs() { logs_.clear(); }

void EditorConsole::BeginCapture() {
	capturing_ = true;
	captured_.clear();
}

std::vector<std::string> EditorConsole::EndCapture() {
	capturing_ = false;
	return std::move(captured_);
}

void EditorConsole::SubmitCommand(const std::string& line) {
	// Consoleから打ったコマンドも、kujata CLIと同じ受付の列を通す(同じ順番・同じ処理)。
	EditorCommandServer::GetInstance().Submit(line, "Console", PrintResponse);
	if (history_.empty() || history_.back() != line) {
		history_.push_back(line);
	}
	historyPosition_ = -1;
}

int EditorConsole::InputCallback(ImGuiInputTextCallbackData* data) {
#ifdef USE_IMGUI
	EditorConsole* console = static_cast<EditorConsole*>(data->UserData);
	if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || console->history_.empty()) {
		return 0;
	}

	const int previous = console->historyPosition_;
	if (data->EventKey == ImGuiKey_UpArrow) {
		if (console->historyPosition_ == -1) {
			console->historyPosition_ = static_cast<int>(console->history_.size()) - 1;
		} else if (console->historyPosition_ > 0) {
			--console->historyPosition_;
		}
	} else if (data->EventKey == ImGuiKey_DownArrow) {
		if (console->historyPosition_ != -1 && ++console->historyPosition_ >= static_cast<int>(console->history_.size())) {
			console->historyPosition_ = -1;
		}
	}

	if (previous != console->historyPosition_) {
		const std::string& text = console->historyPosition_ >= 0 ? console->history_[console->historyPosition_] : std::string();
		data->DeleteChars(0, data->BufTextLen);
		data->InsertChars(0, text.c_str());
	}
#else
	(void)data;
#endif // USE_IMGUI
	return 0;
}

void EditorConsole::DrawCommandInput() {
#ifdef USE_IMGUI
	ImGui::Separator();
	ImGui::TextUnformatted(">");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	if (focusInput_) {
		ImGui::SetKeyboardFocusHere();
		focusInput_ = false;
	}
	const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
	if (ImGui::InputTextWithHint("##ConsoleCommand", "コマンドを入力(help で一覧)", inputBuffer_, sizeof(inputBuffer_), flags, InputCallback, this)) {
		std::string line = inputBuffer_;
		if (line.find_first_not_of(" \t") != std::string::npos) {
			SubmitCommand(line);
		}
		inputBuffer_[0] = '\0';
		// Enterで入力欄からフォーカスが外れるので、続けて打てるよう次のフレームで戻す。
		focusInput_ = true;
	}
#endif // USE_IMGUI
}

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

	// ログは入力欄の上の領域だけでスクロールさせる(入力欄は常に下に見えているように)。
	const float inputHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	ImGui::BeginChild("##ConsoleLogs", ImVec2(0.0f, -inputHeight), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
	const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
	for (const std::string& log : logs_) {
		ImGui::TextUnformatted(log.c_str());
	}
	// 末尾までスクロールされている場合は、新しいログへ追従する。
	if (scrollToBottom_ && wasAtBottom) {
		ImGui::SetScrollHereY(1.0f);
	}
	scrollToBottom_ = false;
	ImGui::EndChild();

	DrawCommandInput();
	ImGui::End();
#else
	(void)pOpen;
#endif // USE_IMGUI
}

} // namespace KujataEngine

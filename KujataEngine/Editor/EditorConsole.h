#pragma once

#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace KujataEngine {

// Editorの簡易Consoleウィンドウと、そのログバッファを管理するシングルトン。
// 下部の入力欄からエディタのCUIコマンド(help で一覧)を打てる。kujata CLIと同じコマンド。
class EditorConsole {
public:
	static EditorConsole* GetInstance();

	void AddLog(const std::string& message);
	void ClearLogs();
	void Draw(bool* pOpen = nullptr);

	const std::vector<std::string>& GetLogs() const { return logs_; }

	/// <summary>
	/// ここからEndCaptureまでに出たログを別に集める。CUIが「そのコマンドの実行中に出たログ」を返すために使う。
	/// </summary>
	void BeginCapture();
	std::vector<std::string> EndCapture();

private:
	EditorConsole() = default;
	~EditorConsole() = default;
	EditorConsole(const EditorConsole&) = delete;
	EditorConsole& operator=(const EditorConsole&) = delete;

	void DrawCommandInput();
	void SubmitCommand(const std::string& line);
	// 入力欄の↑↓で履歴をたどるためのImGuiコールバック。
	static int InputCallback(ImGuiInputTextCallbackData* data);

	// Consoleウィンドウに表示する簡易ログ。最低限の状態確認用としてメモリ上に保持する。
	std::vector<std::string> logs_;

	bool capturing_ = false;
	std::vector<std::string> captured_;

	// 入力欄の中身と、↑↓でたどる入力履歴。
	char inputBuffer_[512] = {};
	std::vector<std::string> history_;
	int historyPosition_ = -1;
	bool scrollToBottom_ = false;
	bool focusInput_ = false;
};

} // namespace KujataEngine

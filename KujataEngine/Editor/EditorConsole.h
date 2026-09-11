#pragma once

#include <string>
#include <vector>

namespace KujataEngine {

// Editorの簡易Consoleウィンドウと、そのログバッファを管理するシングルトン。
class EditorConsole {
public:
	static EditorConsole* GetInstance();

	void AddLog(const std::string& message);
	void ClearLogs();
	void Draw(bool* pOpen = nullptr);

private:
	EditorConsole() = default;
	~EditorConsole() = default;
	EditorConsole(const EditorConsole&) = delete;
	EditorConsole& operator=(const EditorConsole&) = delete;

	// Consoleウィンドウに表示する簡易ログ。最低限の状態確認用としてメモリ上に保持する。
	std::vector<std::string> logs_;
};

} // namespace KujataEngine

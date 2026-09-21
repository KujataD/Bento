#pragma once

#include <filesystem>
#include <string>

namespace KujataEngine {

/// <summary>ログの重さ。Consoleの絞り込みと色分け、log.tail の絞り込みに使う。</summary>
enum class EditorLogLevel {
	Info,
	Warning,
	Error,
};

/// <summary>
/// ログの文面から重さを決める。今のログ出力(AddConsoleLog / Logger::Log)は重さを持たないので、
/// 「失敗」「error」などの言葉で判定する(完全ではない。重さを明示して出す仕組みは今後)。
/// </summary>
EditorLogLevel ClassifyEditorLog(const std::string& message);

/// <summary>"[CUI] ..." のような先頭の [分類] を取り出す(無ければ空)。</summary>
std::string ExtractEditorLogCategory(const std::string& message);

const char* ToEditorLogLevelName(EditorLogLevel level);

/// <summary>名前("info" / "warning" / "error")から重さを得る。読めなければfalse。</summary>
bool ParseEditorLogLevel(const std::string& name, EditorLogLevel& level);

/// <summary>
/// ログを1行1件のJSON(JSON Lines)でファイルにも残す。人間はConsole、AIやスクリプトはこのファイルを読む。
/// 置き場所は <エンジン>/logs/editor_<起動日時>.jsonl(起動したカレントフォルダに関係なく同じ場所)。
/// 1行: {"time": "...", "level": "error", "source": "Console", "category": "CUI", "message": "..."}
/// </summary>
class EditorLog {
public:
	/// <summary>sourceはログの出どころ("Console" = エディタのConsole、"Engine" = Logger::Log)。</summary>
	static void Write(const std::string& source, const std::string& message);

	/// <summary>今回の起動で書いているファイル(まだ1件も書いていなければ空)。</summary>
	static std::filesystem::path GetFilePath();

	/// <summary>エンジン側のLogger::Logも、このファイルへ流れるようにする。起動時に1回呼ぶ。</summary>
	static void HookEngineLogger();
};

} // namespace KujataEngine

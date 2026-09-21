#pragma once

#include "EditorCommand.h"
#include "../../externals/nlohmann/json.hpp"
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace KujataEngine {

/// <summary>
/// エディタのCUIの受付窓口。次の3つの入口から来たコマンドを1件ずつ順にメインスレッドで実行し、
/// 結果(ログとエディタの状態つき)を返す。
///   - EditorのConsoleウィンドウの入力欄(人間)
///   - 名前付きパイプ \\.\pipe\KujataEditor(Tools/kujata.cmd。人間・AI)
///   - 起動引数 --run <ファイル>(1行1コマンドのスクリプト。--exit で終わったら終了)
/// 設計は .claude/editor-automation.md。
/// </summary>
class EditorCommandServer {
public:
	using ResponseCallback = std::function<void(const nlohmann::json& response)>;

	static EditorCommandServer& GetInstance();

	/// <summary>標準コマンドの登録、起動引数の解析、パイプの受付開始。</summary>
	void Initialize();

	/// <summary>毎フレームの頭(EditorApplication::Updateの先頭)で呼ぶ。コマンドはここでだけ実行される。</summary>
	void ProcessFrame();

	/// <summary>パイプの受付を止める。</summary>
	void Finalize();

	/// <summary>
	/// コマンドを受付の列に積む。どのスレッドから呼んでもよい。
	/// callbackはメインスレッドで、コマンドの返事ができたときに呼ばれる。
	/// </summary>
	void Submit(const std::string& line, const std::string& source, ResponseCallback callback);

	/// <summary>今のエディタの状態(モード・シーン・選択・Undoの先頭・フレーム番号)。</summary>
	nlohmann::json BuildState() const;

	/// <summary>--run のスクリプトに失敗したコマンドがあれば1。WinMainの戻り値に使う。</summary>
	int GetExitCode() const { return exitCode_; }

	static constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\KujataEditor";

private:
	EditorCommandServer() = default;

	struct Request {
		std::string line;
		std::string source;
		ResponseCallback callback;
	};

	void ParseStartupArguments();
	void LoadScript(const std::filesystem::path& scriptPath);
	void Respond(Request& request, const EditorCommandResult& result);
	void PipeThreadMain();
	// 受付スレッドからConsoleへログを出したいときに使う。実際の出力は次のProcessFrame。
	void PostLog(const std::string& message);

	std::mutex queueMutex_;
	std::deque<Request> queue_;
	std::vector<std::string> pendingLogs_;

	// wait中のコマンド。waitが終わるまで次のコマンドは実行しない(順番を保つため)。
	bool hasActiveRequest_ = false;
	Request activeRequest_;
	EditorCommandResult activeResult_;
	int activeWaitFrames_ = 0;

	uint64_t frame_ = 0;

	// --run / --exit
	std::filesystem::path scriptOutputPath_;
	std::ofstream scriptOutput_;
	size_t pendingScriptCommands_ = 0;
	bool exitWhenScriptDone_ = false;
	int exitCode_ = 0;

	std::atomic<bool> running_ = false;
	std::thread pipeThread_;
};

} // namespace KujataEngine

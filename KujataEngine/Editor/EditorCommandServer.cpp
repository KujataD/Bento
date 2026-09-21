#include "EditorCommandServer.h"

#include "EditorApplication.h"
#include "EditorConsole.h"
#include "EditorSelection.h"
#include "EditorUndoManager.h"
#include "Commands/EditorCommandUtil.h"
#include "../runtime/AppControl.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <future>
#include <memory>
#include <string_view>

namespace KujataEngine {

namespace {

const char* ToModeName(EditorMode mode) {
	switch (mode) {
	case EditorMode::Play:
		return "Play";
	case EditorMode::PrefabEdit:
		return "PrefabEdit";
	case EditorMode::Edit:
	default:
		return "Edit";
	}
}

std::string Utf16ToUtf8(const std::wstring& text) {
	if (text.empty()) {
		return {};
	}
	int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string result(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
	return result;
}

bool WriteAll(HANDLE pipe, const std::string& text) {
	size_t written = 0;
	while (written < text.size()) {
		DWORD chunk = 0;
		if (!WriteFile(pipe, text.data() + written, static_cast<DWORD>(text.size() - written), &chunk, nullptr)) {
			return false;
		}
		written += chunk;
	}
	return true;
}

} // namespace

EditorCommandServer& EditorCommandServer::GetInstance() {
	static EditorCommandServer instance;
	return instance;
}

void EditorCommandServer::Initialize() {
	RegisterBuiltinEditorCommands();
	ParseStartupArguments();

	running_ = true;
	pipeThread_ = std::thread([this]() { PipeThreadMain(); });
}

void EditorCommandServer::ParseStartupArguments() {
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) {
		return;
	}

	std::filesystem::path scriptPath;
	for (int i = 1; i < argc; ++i) {
		std::wstring_view argument(argv[i]);
		if (argument == L"--run" && i + 1 < argc) {
			scriptPath = argv[++i];
		} else if (argument == L"--run-output" && i + 1 < argc) {
			scriptOutputPath_ = argv[++i];
		} else if (argument == L"--exit") {
			exitWhenScriptDone_ = true;
		}
	}
	LocalFree(argv);

	if (!scriptPath.empty()) {
		LoadScript(scriptPath);
	}
}

void EditorCommandServer::LoadScript(const std::filesystem::path& scriptPath) {
	std::ifstream input(scriptPath);
	if (!input) {
		EditorConsole::GetInstance()->AddLog("[CUI] --run のファイルを開けません: " + scriptPath.string(), EditorLogLevel::Error);
		exitCode_ = 1;
		return;
	}

	// 結果は1行1件のJSONで書き出す(既定はスクリプトの隣の <名前>.result.jsonl)。
	if (scriptOutputPath_.empty()) {
		scriptOutputPath_ = scriptPath;
		scriptOutputPath_ += ".result.jsonl";
	}
	scriptOutput_.open(scriptOutputPath_, std::ios::trunc);

	std::string line;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		// 空行と # で始まる行(コメント)は飛ばす。
		size_t first = line.find_first_not_of(" \t");
		if (first == std::string::npos || line[first] == '#') {
			continue;
		}
		++pendingScriptCommands_;
		Submit(line, "Script", [this](const nlohmann::json& response) {
			if (scriptOutput_) {
				scriptOutput_ << response.dump() << '\n';
				scriptOutput_.flush();
			}
			if (!response.value("ok", false)) {
				exitCode_ = 1;
			}
			--pendingScriptCommands_;
		});
	}
	EditorConsole::GetInstance()->AddLog("[CUI] --run: " + std::to_string(pendingScriptCommands_) + " 件のコマンドを実行します: " + scriptPath.string(), EditorLogLevel::Info);
}

void EditorCommandServer::Submit(const std::string& line, const std::string& source, ResponseCallback callback) {
	std::lock_guard<std::mutex> lock(queueMutex_);
	queue_.push_back(Request{line, source, std::move(callback)});
}

void EditorCommandServer::PostLog(const std::string& message) {
	std::lock_guard<std::mutex> lock(queueMutex_);
	pendingLogs_.push_back(message);
}

void EditorCommandServer::ProcessFrame() {
	++frame_;

	// 受付スレッドからのログはここでConsoleへ出す(Consoleはメインスレッド専用)。
	{
		std::lock_guard<std::mutex> lock(queueMutex_);
		for (const std::string& message : pendingLogs_) {
			EditorConsole::GetInstance()->AddLog(message, EditorLogLevel::Warning);
		}
		pendingLogs_.clear();
	}

	// 返事待ち(poll 中)のコマンドがあれば、それが終わるまで次へは進まない(実行順を保つ)。
	if (hasActiveRequest_) {
		EditorCommandResult finished;
		if (!activeResult_.poll(finished)) {
			return;
		}
		hasActiveRequest_ = false;
		Respond(activeRequest_, finished);
	}

	Request request;
	{
		std::lock_guard<std::mutex> lock(queueMutex_);
		if (queue_.empty()) {
			// --run のスクリプトを全部流し終えたら、--exit のときは終了する。
			if (exitWhenScriptDone_ && pendingScriptCommands_ == 0) {
				exitWhenScriptDone_ = false;
				RequestQuitApplication();
			}
			return;
		}
		request = std::move(queue_.front());
		queue_.pop_front();
	}

	// そのコマンドの実行中(waitなら待っている間も)にConsoleへ出たログを、返事に含める。
	EditorConsole::GetInstance()->BeginCapture();
	EditorConsole::GetInstance()->AddLog("> " + request.line + "  [" + request.source + "]", EditorLogLevel::Info);
	EditorCommandResult result = EditorCommandRegistry::GetInstance().Execute(request.line);

	if (result.ok && result.poll) {
		hasActiveRequest_ = true;
		activeRequest_ = std::move(request);
		activeResult_ = std::move(result);
		return;
	}
	Respond(request, result);
}

void EditorCommandServer::Respond(Request& request, const EditorCommandResult& result) {
	std::vector<std::string> logs = EditorConsole::GetInstance()->EndCapture();
	// 先頭は自分で出した "> コマンド" の行なので、返事からは外す。
	if (!logs.empty()) {
		logs.erase(logs.begin());
	}

	nlohmann::json response;
	response["ok"] = result.ok;
	response["command"] = request.line;
	response["result"] = result.result;
	response["error"] = result.error;
	// 人間向けに整形した結果(kujata CLIとConsoleが表示する)。日本語をそのまま読めるよう、ここで作っておく。
	if (!result.ok) {
		response["text"] = "エラー: " + result.error;
	} else if (result.result.is_string()) {
		response["text"] = result.result.get<std::string>();
	} else if (result.result.is_null()) {
		response["text"] = "OK";
	} else {
		response["text"] = result.result.dump(2);
	}
	response["logs"] = logs;
	response["state"] = BuildState();

	// kujata CLIや--runから来たコマンドの失敗も、エディタを見ている人に分かるようConsoleへ出す
	// (Consoleから打ったものは返事の表示で出るので重ねない)。
	if (!result.ok && request.source != "Console") {
		EditorConsole::GetInstance()->AddLog("[CUI] 失敗: " + result.error, EditorLogLevel::Error);
	}

	if (request.callback) {
		request.callback(response);
	}
}

nlohmann::json EditorCommandServer::BuildState() const {
	EditorApplication* application = EditorApplication::GetInstance();
	Scene* scene = application->GetCurrentScene();
	GameObject* selected = EditorSelection::GetInstance()->GetSelectedGameObject();

	nlohmann::json state;
	state["mode"] = ToModeName(application->GetEditorMode());
	state["scene"] = scene ? scene->GetSceneName() : "";
	state["selection"] = selected ? EditorCommandUtil::MakeObjectPath(selected) : "";
	state["undoTop"] = EditorUndoManager::GetInstance()->GetUndoTopLabel();
	state["frame"] = frame_;
	return state;
}

void EditorCommandServer::PipeThreadMain() {
	while (running_) {
		// ローカルのクライアントだけ受け付ける。1本だけ作り、切断されたら作り直す。
		HANDLE pipe = CreateNamedPipeW(kPipeName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64 * 1024, 64 * 1024, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE) {
			// 別のエディタが先にパイプを持っている等。CUIはそちらが受け付ける。
			PostLog("[CUI] 名前付きパイプを作れませんでした(別のエディタが起動中?): " + Utf16ToUtf8(kPipeName));
			return;
		}

		BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
		if (!running_ || !connected) {
			CloseHandle(pipe);
			continue;
		}

		// 1行1コマンド。返事も1行のJSON。クライアントが切断するまで続ける(対話モード用)。
		std::string buffer;
		char chunk[4096];
		DWORD readSize = 0;
		bool alive = true;
		while (alive && running_ && ReadFile(pipe, chunk, sizeof(chunk), &readSize, nullptr) && readSize > 0) {
			buffer.append(chunk, readSize);
			size_t newline = 0;
			while (alive && (newline = buffer.find('\n')) != std::string::npos) {
				std::string line = buffer.substr(0, newline);
				buffer.erase(0, newline + 1);
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				if (line.empty()) {
					continue;
				}

				auto promise = std::make_shared<std::promise<std::string>>();
				std::future<std::string> future = promise->get_future();
				Submit(line, "Pipe", [promise](const nlohmann::json& response) { promise->set_value(response.dump()); });

				// メインスレッドが実行するまで待つ。エディタの終了時は待つのをやめる。
				while (running_ && future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
				}
				if (!running_) {
					alive = false;
					break;
				}
				alive = WriteAll(pipe, future.get() + "\n");
			}
		}

		FlushFileBuffers(pipe);
		DisconnectNamedPipe(pipe);
		CloseHandle(pipe);
	}
}

void EditorCommandServer::Finalize() {
	if (!running_) {
		return;
	}
	running_ = false;

	if (pipeThread_.joinable()) {
		// ConnectNamedPipe / ReadFile で止まっている受付スレッドを起こす。
		CancelSynchronousIo(pipeThread_.native_handle());
		HANDLE wake = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (wake != INVALID_HANDLE_VALUE) {
			CloseHandle(wake);
		}
		pipeThread_.join();
	}
	if (scriptOutput_.is_open()) {
		scriptOutput_.close();
	}
}

} // namespace KujataEngine

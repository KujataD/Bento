// エディタの CUI のコマンド: 案内・状態・ログ・Play・Undo・保存など、エディタ全体に関わるコマンド。
// 設計と一覧は .claude/editor-automation.md。共通の関数は EditorCommandUtil にある。
#include "EditorCommandUtil.h"
#include "../EditorApplication.h"
#include "../EditorCommandServer.h"
#include "../EditorConsole.h"
#include "../EditorLog.h"
#include "../EditorUndoManager.h"
#include "../SceneJsonExporter.h"
#include "../../base/ProjectPath.h"
#include "../../runtime/AppControl.h"
#include "../../scene/Scene.h"
#include <filesystem>
#include <string>
#include <vector>

namespace KujataEngine {

namespace {

using namespace EditorCommandUtil;
using nlohmann::json;

// wait で待てる上限(60fpsで10分)。打ち間違いでエディタが長時間返事をしなくなるのを防ぐ。
constexpr int kMaxWaitFrames = 36000;

EditorCommandResult CommandHelp(const EditorCommandArgs& args) {
	const auto& commands = EditorCommandRegistry::GetInstance().GetCommands();
	if (args.Count() > 0) {
		auto found = commands.find(args.Get(0));
		if (found == commands.end()) {
			return EditorCommandResult::Failure("不明なコマンドです: " + args.Get(0));
		}
		return EditorCommandResult::Success(found->second.usage + "\n  " + found->second.description);
	}

	std::string text = "コマンド一覧(help <コマンド> で詳しく表示):";
	for (const auto& [name, command] : commands) {
		text += "\n  " + command.usage + "\n      " + command.description;
	}
	return EditorCommandResult::Success(text);
}

EditorCommandResult CommandState(const EditorCommandArgs&) { return EditorCommandResult::Success(EditorCommandServer::GetInstance().BuildState()); }

EditorCommandResult CommandLogTail(const EditorCommandArgs& args) {
	// log.tail [件数] [info|warning|error]。重さを指定すると、それ以上の重さのログだけにする。
	size_t count = 20;
	EditorLogLevel minimumLevel = EditorLogLevel::Info;
	for (size_t index = 0; index < args.Count(); ++index) {
		const std::string& argument = args.Get(index);
		if (ParseEditorLogLevel(argument, minimumLevel)) {
			continue;
		}
		try {
			count = static_cast<size_t>(std::stoul(argument));
		} catch (...) {
			return EditorCommandResult::Failure("log.tail [件数] [info|warning|error] の形で指定してください(例: log.tail 50 error)。");
		}
	}

	std::vector<std::string> matched;
	for (const EditorConsole::Entry& entry : EditorConsole::GetInstance()->GetLogs()) {
		if (entry.level >= minimumLevel) {
			matched.push_back(entry.message);
		}
	}
	size_t begin = matched.size() > count ? matched.size() - count : 0;
	return EditorCommandResult::Success(std::vector<std::string>(matched.begin() + static_cast<std::ptrdiff_t>(begin), matched.end()));
}

EditorCommandResult CommandLogFile(const EditorCommandArgs&) {
	const std::filesystem::path path = EditorLog::GetFilePath();
	if (path.empty()) {
		return EditorCommandResult::Failure("ログファイルはまだ作られていません。");
	}
	return EditorCommandResult::Success(path.string());
}

EditorCommandResult CommandUndo(const EditorCommandArgs&) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	const std::string label = EditorUndoManager::GetInstance()->GetUndoTopLabel();
	if (!EditorUndoManager::GetInstance()->Undo(*scene)) {
		return EditorCommandResult::Failure("戻せる操作がありません。");
	}
	return EditorCommandResult::Success("戻しました: " + label);
}

EditorCommandResult CommandRedo(const EditorCommandArgs&) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	if (!EditorUndoManager::GetInstance()->Redo(*scene)) {
		return EditorCommandResult::Failure("やり直せる操作がありません。");
	}
	return EditorCommandResult::Success();
}

EditorCommandResult CommandPlayStart(const EditorCommandArgs&) {
	EditorApplication* application = EditorApplication::GetInstance();
	application->Start();
	if (!application->IsPlaying()) {
		return EditorCommandResult::Failure("Play に入れませんでした(Prefab 編集中は Play できません)。");
	}
	return EditorCommandResult::Success();
}

EditorCommandResult CommandPlayStop(const EditorCommandArgs&) {
	EditorApplication* application = EditorApplication::GetInstance();
	if (!application->IsPlaying()) {
		return EditorCommandResult::Failure("Play 中ではありません。");
	}
	application->Stop();
	return EditorCommandResult::Success();
}

EditorCommandResult CommandWait(const EditorCommandArgs& args) {
	int frames = 0;
	try {
		frames = std::stoi(args.Get(0));
	} catch (...) {
		return EditorCommandResult::Failure("フレーム数を指定してください(例: wait 60)。");
	}
	if (frames < 1 || frames > kMaxWaitFrames) {
		return EditorCommandResult::Failure("フレーム数は 1 〜 " + std::to_string(kMaxWaitFrames) + " で指定してください。");
	}
	// 次のフレームから数えて frames 回目の頭で返事をする(待っている間のログも返事に入る)。
	EditorCommandResult pending = EditorCommandResult::Success();
	pending.poll = [remaining = frames, frames](EditorCommandResult& out) mutable {
		if (--remaining > 0) {
			return false;
		}
		out = EditorCommandResult::Success(json{{"frames", frames}});
		return true;
	};
	return pending;
}

EditorCommandResult CommandSceneSave(const EditorCommandArgs&) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	if (EditorApplication::GetInstance()->IsPlaying()) {
		return EditorCommandResult::Failure("Play 中は保存できません(play.stop してから保存してください)。");
	}
	SceneJsonExporter::ExportResult exportResult = SceneJsonExporter::ExportScene(*scene, GetProjectDataRoot());
	if (!exportResult.succeeded) {
		return EditorCommandResult::Failure("保存に失敗しました: " + exportResult.message);
	}
	EditorConsole::GetInstance()->AddLog("[Editor] Scene JSON exported: " + exportResult.outputDirectory.string());
	return EditorCommandResult::Success(exportResult.outputDirectory.string());
}

EditorCommandResult CommandModuleReload(const EditorCommandArgs&) {
	if (!EditorApplication::GetInstance()->ReloadGameModule()) {
		return EditorCommandResult::Failure("GameModule の差し替えに失敗しました(logs を確認してください)。");
	}
	return EditorCommandResult::Success();
}

EditorCommandResult CommandQuit(const EditorCommandArgs&) {
	RequestQuitApplication();
	return EditorCommandResult::Success("エディタを終了します。");
}

} // namespace

void RegisterCoreCommands(EditorCommandRegistry& registry) {
	registry.Register("help", "help [コマンド]", "コマンドの一覧、または指定したコマンドの使い方を表示する", CommandHelp);
	registry.Register("state", "state", "エディタの状態(モード・シーン・選択・Undoの先頭・フレーム番号)を表示する", CommandState);
	registry.Register("log.tail", "log.tail [件数] [info|warning|error]", "Console の最近のログを表示する(既定 20 件)。重さを付けるとそれ以上のものだけ", CommandLogTail);
	registry.Register("log.file", "log.file", "今回の起動のログファイル(JSON Lines)の場所を表示する", CommandLogFile);
	registry.Register("undo", "undo", "直前の操作を戻す", CommandUndo);
	registry.Register("redo", "redo", "戻した操作をやり直す", CommandRedo);
	registry.Register("play.start", "play.start", "Play に入る", CommandPlayStart);
	registry.Register("play.stop", "play.stop", "Play をやめて Edit に戻る(Play 前の状態に戻る)", CommandPlayStop);
	registry.Register("wait", "wait <フレーム数>", "指定フレーム進むのを待ってから返す。待っている間のログも返す", CommandWait);
	registry.Register("scene.save", "scene.save", "シーンを保存する(Ctrl+S と同じ)", CommandSceneSave);
	registry.Register("module.reload", "module.reload", "GameModule をビルドし直して差し替える(Reload DLL と同じ)", CommandModuleReload);
	registry.Register("quit", "quit", "エディタを終了する", CommandQuit);
}

} // namespace KujataEngine

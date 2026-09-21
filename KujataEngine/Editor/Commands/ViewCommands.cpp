// エディタの CUI のコマンド: 見た目の確認(スクリーンショット・ウィンドウの表示)。
// 設計と一覧は .claude/editor-automation.md。共通の関数は EditorCommandUtil にある。
#include "EditorCommandUtil.h"
#include "../EditorScreenshot.h"
#include "../ImGuiManager.h"
#include "../../base/ProjectPath.h"
#include <filesystem>
#include <optional>
#include <string>

namespace KujataEngine {

namespace {

using namespace EditorCommandUtil;
using nlohmann::json;

EditorCommandResult CommandViewScreenshot(const EditorCommandArgs& args) {
	const std::string& targetName = args.Get(0);
	EditorScreenshot::Target target = EditorScreenshot::Target::SceneView;
	if (targetName == "scene") {
		target = EditorScreenshot::Target::SceneView;
	} else if (targetName == "game") {
		target = EditorScreenshot::Target::GameView;
	} else if (targetName == "editor") {
		target = EditorScreenshot::Target::Editor;
	} else {
		return EditorCommandResult::Failure("撮る対象を scene / game / editor から指定してください(例: view.screenshot game)。");
	}
	if (target == EditorScreenshot::Target::GameView && !GetScene()) {
		return EditorCommandResult::Failure("シーンがありません。");
	}

	// 既定の保存先: <プロジェクト>/Temp/Screenshots/<対象>_<日時>.png(Temp は git 管理外)
	std::filesystem::path outputPath = args.Count() > 1 ? ResolveOutputPath(args.RestFrom(1))
	                                                    : GetActiveProjectRoot() / "Temp" / "Screenshots" / (targetName + "_" + MakeFileTimestamp() + ".png");
	if (outputPath.extension() != ".png") {
		outputPath += ".png";
	}

	std::string error;
	if (!EditorScreenshot::GetInstance().Request(target, outputPath, error)) {
		return EditorCommandResult::Failure(error);
	}

	// 描画が終わるのを待ってから返事をする(次のフレームの頭で保存される)。
	EditorCommandResult pending = EditorCommandResult::Success();
	pending.poll = [](EditorCommandResult& out) {
		std::string pollError;
		std::optional<nlohmann::json> finished = EditorScreenshot::GetInstance().Poll(pollError);
		if (!finished) {
			return false;
		}
		out = pollError.empty() ? EditorCommandResult::Success(*finished) : EditorCommandResult::Failure(pollError);
		return true;
	};
	return pending;
}

EditorCommandResult CommandWindowShow(const EditorCommandArgs& args) {
	ImGuiManager* imgui = ImGuiManager::GetInstance();
	if (args.Count() == 0) {
		// 引数なし: ウィンドウの一覧と、開いているか。
		json windows = json::object();
		for (const auto& [name, visible] : imgui->GetWindowVisibilities()) {
			windows[name] = visible;
		}
		return EditorCommandResult::Success(windows);
	}
	bool visible = true;
	if (args.Count() > 1 && !ParseBool(args.Get(1), visible)) {
		return EditorCommandResult::Failure("true / false を指定してください。例: window.show Console true");
	}
	if (!imgui->ShowWindow(args.Get(0), visible)) {
		std::string names;
		for (const auto& [name, isVisible] : imgui->GetWindowVisibilities()) {
			names += " " + name;
		}
		return EditorCommandResult::Failure("知らないウィンドウです: " + args.Get(0) + "。ウィンドウ:" + names);
	}
	return EditorCommandResult::Success();
}

} // namespace

void RegisterViewCommands(EditorCommandRegistry& registry) {
	registry.Register("view.screenshot", "view.screenshot <scene|game|editor> [ファイル]",
	                  "ビューの描画結果(scene/game)かエディタ全体(editor)を PNG に保存する。既定は <プロジェクト>/Temp/Screenshots/", CommandViewScreenshot);
	registry.Register("window.show", "window.show [ウィンドウ名] [true|false]",
	                  "ウィンドウを開いて前面に出す(false で閉じる)。引数なしでウィンドウの一覧と開閉を表示する", CommandWindowShow);
}

} // namespace KujataEngine

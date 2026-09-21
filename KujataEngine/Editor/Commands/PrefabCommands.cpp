// エディタの CUI のコマンド: プレハブ(処理は PrefabEditing。Inspector / Hierarchy のボタンと同じもの)。
// 設計と一覧は .claude/editor-automation.md。共通の関数は EditorCommandUtil にある。
#include "EditorCommandUtil.h"
#include "../EditorApplication.h"
#include "../PrefabAsset.h"
#include "../PrefabEditing.h"
#include "../../base/ProjectPath.h"
#include "../../scene/GameObject.h"
#include "../../scene/Scene.h"
#include <filesystem>
#include <string>
#include <system_error>

namespace KujataEngine {

namespace {

using namespace EditorCommandUtil;
using nlohmann::json;

// プレハブのインスタンスの「ルート」を探す(子を指定しても、Apply/Revert/Unpack はルート単位で行う)。
GameObject* ResolvePrefabInstanceRoot(Scene& scene, const std::string& spec, std::string& error) {
	GameObject* gameObject = ResolveObject(scene, spec, error);
	if (!gameObject) {
		return nullptr;
	}
	if (!gameObject->IsPrefabInstance()) {
		error = MakeObjectPath(gameObject) + " はプレハブのインスタンスではありません。";
		return nullptr;
	}
	GameObject* root = PrefabAsset::FindPrefabInstanceRoot(scene, *gameObject);
	return root ? root : gameObject;
}

json DescribePrefabResult(const PrefabEditing::Result& result) {
	json value;
	value["object"] = result.object ? MakeObjectPath(result.object) : "";
	if (!result.prefabPath.empty()) {
		value["prefab"] = result.prefabPath.generic_string();
	}
	return value;
}

EditorCommandResult CommandPrefabList(const EditorCommandArgs&) {
	// Data 配下の *.prefab.json を、prefab.instantiate にそのまま渡せる相対パスで返す。
	json prefabs = json::array();
	const std::filesystem::path dataRoot = GetProjectDataRoot();
	std::error_code errorCode;
	for (auto it = std::filesystem::recursive_directory_iterator(dataRoot, errorCode); !errorCode && it != std::filesystem::recursive_directory_iterator(); it.increment(errorCode)) {
		const std::string fileName = it->path().filename().string();
		if (it->is_regular_file() && fileName.ends_with(".prefab.json")) {
			prefabs.push_back(std::filesystem::relative(it->path(), dataRoot, errorCode).generic_string());
		}
	}
	return EditorCommandResult::Success(prefabs);
}

EditorCommandResult CommandPrefabCreate(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	GameObject* gameObject = ResolveObject(*scene, args.Get(0), error);
	if (!gameObject) {
		return EditorCommandResult::Failure(error);
	}
	PrefabEditing::Result result = PrefabEditing::Create(*scene, *gameObject, "[CUI] prefab.create " + MakeObjectPath(gameObject));
	return result.succeeded ? EditorCommandResult::Success(DescribePrefabResult(result)) : EditorCommandResult::Failure(result.message);
}

EditorCommandResult CommandPrefabInstantiate(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	if (args.Count() == 0) {
		return EditorCommandResult::Failure("プレハブのパスを指定してください(prefab.list で一覧。例: prefab.instantiate Prefabs/Enemy.prefab.json)。");
	}
	GameObject* parent = nullptr;
	if (args.Count() > 1) {
		std::string error;
		parent = ResolveObject(*scene, args.Get(1), error);
		if (!parent) {
			return EditorCommandResult::Failure(error);
		}
	}
	PrefabEditing::Result result = PrefabEditing::Instantiate(*scene, args.Get(0), parent, "[CUI] prefab.instantiate " + args.Get(0));
	return result.succeeded ? EditorCommandResult::Success(DescribePrefabResult(result)) : EditorCommandResult::Failure(result.message);
}

EditorCommandResult CommandPrefabApply(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	GameObject* root = ResolvePrefabInstanceRoot(*scene, args.Get(0), error);
	if (!root) {
		return EditorCommandResult::Failure(error);
	}
	PrefabEditing::Result result = PrefabEditing::Apply(*scene, *root);
	return result.succeeded ? EditorCommandResult::Success(DescribePrefabResult(result)) : EditorCommandResult::Failure(result.message);
}

EditorCommandResult CommandPrefabRevert(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	GameObject* root = ResolvePrefabInstanceRoot(*scene, args.Get(0), error);
	if (!root) {
		return EditorCommandResult::Failure(error);
	}
	PrefabEditing::Result result = PrefabEditing::Revert(*scene, *root, "[CUI] prefab.revert " + MakeObjectPath(root));
	return result.succeeded ? EditorCommandResult::Success(DescribePrefabResult(result)) : EditorCommandResult::Failure(result.message);
}

EditorCommandResult CommandPrefabUnpack(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	GameObject* root = ResolvePrefabInstanceRoot(*scene, args.Get(0), error);
	if (!root) {
		return EditorCommandResult::Failure(error);
	}
	PrefabEditing::Result result = PrefabEditing::Unpack(*scene, *root, "[CUI] prefab.unpack " + MakeObjectPath(root));
	return result.succeeded ? EditorCommandResult::Success(DescribePrefabResult(result)) : EditorCommandResult::Failure(result.message);
}

EditorCommandResult CommandPrefabOpen(const EditorCommandArgs& args) {
	// ".prefab.json" で終わればプレハブのパス、そうでなければインスタンス(オブジェクト)として探す。
	// (オブジェクトが見つからないときにパスとして読みに行くと、「同名が2つ」などの本当の理由が隠れてしまう)
	const std::string target = args.RestFrom(0);
	if (target.empty()) {
		return EditorCommandResult::Failure("プレハブのインスタンスか、プレハブのパス(*.prefab.json)を指定してください。");
	}
	std::filesystem::path prefabPath = target;
	if (!target.ends_with(".prefab.json")) {
		Scene* scene = GetScene();
		if (!scene) {
			return EditorCommandResult::Failure("シーンがありません。");
		}
		std::string error;
		GameObject* root = ResolvePrefabInstanceRoot(*scene, args.Get(0), error);
		if (!root) {
			return EditorCommandResult::Failure(error);
		}
		prefabPath = root->GetPrefabAssetPath();
	}
	if (!EditorApplication::GetInstance()->OpenPrefabEditMode(prefabPath)) {
		return EditorCommandResult::Failure("プレハブを開けませんでした: " + prefabPath.generic_string() + "(log.tail で理由を確認できます)");
	}
	return EditorCommandResult::Success(prefabPath.generic_string());
}

EditorCommandResult CommandPrefabSave(const EditorCommandArgs&) {
	if (!EditorApplication::GetInstance()->IsPrefabEditing()) {
		return EditorCommandResult::Failure("プレハブ編集中ではありません(prefab.open で開きます)。");
	}
	if (!EditorApplication::GetInstance()->SavePrefabEditMode()) {
		return EditorCommandResult::Failure("プレハブを保存できませんでした(log.tail で理由を確認できます)。");
	}
	return EditorCommandResult::Success();
}

EditorCommandResult CommandPrefabClose(const EditorCommandArgs& args) {
	EditorApplication* application = EditorApplication::GetInstance();
	if (!application->IsPrefabEditing()) {
		return EditorCommandResult::Failure("プレハブ編集中ではありません。");
	}
	bool save = false;
	if (args.Count() > 0 && !ParseBool(args.Get(0), save)) {
		return EditorCommandResult::Failure("保存するかを true / false で指定してください(省略時は保存しない)。");
	}
	application->ClosePrefabEditMode(save);
	return EditorCommandResult::Success();
}

} // namespace

void RegisterPrefabCommands(EditorCommandRegistry& registry) {
	registry.Register("prefab.list", "prefab.list", "プレハブファイルの一覧(prefab.instantiate にそのまま渡せるパス)", CommandPrefabList);
	registry.Register("prefab.create", "prefab.create <オブジェクト>", "オブジェクトと子階層をプレハブとして保存し、インスタンスにする(Hierarchy の Create Prefab と同じ)", CommandPrefabCreate);
	registry.Register("prefab.instantiate", "prefab.instantiate <プレハブのパス> [親]", "プレハブを配置して選択する", CommandPrefabInstantiate);
	registry.Register("prefab.apply", "prefab.apply <インスタンス>", "インスタンスの変更をプレハブへ書き戻す(Inspector の Apply と同じ)", CommandPrefabApply);
	registry.Register("prefab.revert", "prefab.revert <インスタンス>", "インスタンスをプレハブの内容に戻す(Inspector の Revert と同じ)", CommandPrefabRevert);
	registry.Register("prefab.unpack", "prefab.unpack <インスタンス>", "プレハブとのつながりを切る(Inspector の Unpack と同じ)", CommandPrefabUnpack);
	registry.Register("prefab.open", "prefab.open <インスタンス|プレハブのパス>", "プレハブ編集モードで開く(Open Prefab と同じ)", CommandPrefabOpen);
	registry.Register("prefab.save", "prefab.save", "プレハブ編集モードの内容を保存する(シーンのインスタンスも更新される)", CommandPrefabSave);
	registry.Register("prefab.close", "prefab.close [保存するか true|false]", "プレハブ編集モードを閉じてシーンに戻る(既定は保存しない)", CommandPrefabClose);
}

} // namespace KujataEngine

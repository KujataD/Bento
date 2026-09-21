// エディタのCUIの標準コマンド。一覧と使い方は .claude/editor-automation.md の §5、またはエディタで help。
// 変更系のコマンドは、実行前に必ずUndoのスナップショットを取る(ラベルは "[CUI] ...")。
#include "EditorCommand.h"

#include "AnimationEditing.h"
#include "EditorApplication.h"
#include "EditorCommandServer.h"
#include "EditorConsole.h"
#include "EditorLog.h"
#include "EditorScreenshot.h"
#include "EditorSelection.h"
#include "EditorUndoManager.h"
#include "ImGuiManager.h"
#include "PrefabAsset.h"
#include "PrefabEditing.h"
#include "SceneJsonExporter.h"
#include "SceneJsonImporter.h"
#include "../assets/AnimationClipAsset.h"
#include "../base/ProjectPath.h"
#include "../components/AnimatorComponent.h"
#include "../runtime/AppControl.h"
#include "../scene/Component.h"
#include "../scene/ComponentFactory.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace KujataEngine {

namespace {

using nlohmann::json;

// wait で待てる上限(60fpsで10分)。打ち間違いでエディタが長時間返事をしなくなるのを防ぐ。
constexpr int kMaxWaitFrames = 36000;

Scene* GetScene() { return EditorApplication::GetInstance()->GetCurrentScene(); }

std::string MakeObjectPath(const GameObject* gameObject) {
	std::string path;
	for (const GameObject* current = gameObject; current; current = current->GetParent()) {
		path = path.empty() ? current->GetName() : current->GetName() + "/" + path;
	}
	return path;
}

// "Stage/Enemies/Guardian" のパス、または instanceId("go_...")でオブジェクトを探す。
// 同じパスのオブジェクトが複数あるときは、黙って先頭を選ばずにエラーにする。
GameObject* ResolveObject(Scene& scene, const std::string& spec, std::string& error) {
	if (spec.empty()) {
		error = "オブジェクトを指定してください(パス \"親/子\" または instanceId)。";
		return nullptr;
	}
	if (spec.rfind("go_", 0) == 0) {
		if (GameObject* found = scene.FindGameObjectByInstanceId(spec)) {
			return found;
		}
	}

	std::vector<GameObject*> matches;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (gameObject && MakeObjectPath(gameObject.get()) == spec) {
			matches.push_back(gameObject.get());
		}
	}
	if (matches.empty()) {
		error = "オブジェクトが見つかりません: " + spec + "(scene.list で一覧を表示できます)";
		return nullptr;
	}
	if (matches.size() > 1) {
		error = "同じパスのオブジェクトが " + std::to_string(matches.size()) + " 個あります: " + spec + "。instanceId で指定してください:";
		for (GameObject* match : matches) {
			error += " " + match->GetInstanceId();
		}
		return nullptr;
	}
	return matches.front();
}

// "ColliderComponent" または "ColliderComponent#1"(同じ型のうち何番目か。0始まり)でコンポーネントを探す。
Component* ResolveComponent(GameObject& gameObject, const std::string& spec, std::string& error) {
	std::string typeName = spec;
	size_t wantedIndex = 0;
	if (size_t hash = spec.find('#'); hash != std::string::npos) {
		typeName = spec.substr(0, hash);
		try {
			wantedIndex = static_cast<size_t>(std::stoul(spec.substr(hash + 1)));
		} catch (...) {
			error = "コンポーネントの番号が読めません: " + spec + "(例: ColliderComponent#1)";
			return nullptr;
		}
	}

	size_t sameTypeIndex = 0;
	std::vector<std::string> available;
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (!component) {
			continue;
		}
		available.push_back(component->GetTypeName());
		if (typeName == component->GetTypeName()) {
			if (sameTypeIndex == wantedIndex) {
				return component.get();
			}
			++sameTypeIndex;
		}
	}

	error = "コンポーネントが見つかりません: " + spec + "。このオブジェクトにあるもの:";
	for (const std::string& name : available) {
		error += " " + name;
	}
	return nullptr;
}

json DescribeObjectSummary(const GameObject& gameObject) {
	json entry;
	entry["path"] = MakeObjectPath(&gameObject);
	entry["id"] = gameObject.GetInstanceId();
	entry["active"] = gameObject.IsActive();
	json components = json::array();
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (component) {
			components.push_back(component->GetTypeName());
		}
	}
	entry["components"] = components;
	return entry;
}

void AppendHierarchy(const GameObject& gameObject, json& out) {
	out.push_back(DescribeObjectSummary(gameObject));
	for (const GameObject* child : gameObject.GetChildren()) {
		if (child) {
			AppendHierarchy(*child, out);
		}
	}
}

json DescribeComponent(const Component& component, size_t sameTypeIndex) {
	json properties = json::object();
	component.WriteJson(properties);

	json entry;
	std::string name = component.GetTypeName();
	if (sameTypeIndex > 0) {
		name += "#" + std::to_string(sameTypeIndex);
	}
	entry["type"] = name;
	entry["enabled"] = component.IsEnabled();
	entry["properties"] = properties;
	return entry;
}

// Scene::ToJson の components 配列での位置(nullのコンポーネントは書き出されないので数えない)。
size_t FindComponentJsonIndex(const GameObject& gameObject, const Component* target) {
	size_t index = 0;
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (!component) {
			continue;
		}
		if (component.get() == target) {
			return index;
		}
		++index;
	}
	return static_cast<size_t>(-1);
}

// 同じ型のうち何番目か(field.set の返事で "#1" を付けるため)。
size_t SameTypeIndexOf(const GameObject& gameObject, const Component* target) {
	size_t index = 0;
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (component.get() == target) {
			return index;
		}
		if (component && std::string(component->GetTypeName()) == target->GetTypeName()) {
			++index;
		}
	}
	return 0;
}

// 書き換える前後で値の種類が変わらないかを確かめる(数値の欄に文字列を入れる等の打ち間違いを弾く)。
bool IsCompatibleValue(const json& before, const json& after, std::string& error) {
	if (before.is_number() && !after.is_number()) {
		error = "数値の欄です。数値を指定してください(例: 0.5)。";
		return false;
	}
	if (before.is_boolean() && !after.is_boolean()) {
		error = "true / false の欄です。";
		return false;
	}
	if (before.is_string() && !after.is_string()) {
		error = "文字列の欄です。\"...\" で囲んで指定してください。";
		return false;
	}
	if (before.is_array()) {
		if (!after.is_array() || after.size() != before.size()) {
			error = std::to_string(before.size()) + " 要素の配列の欄です(例: [0, 1, 0])。";
			return false;
		}
	}
	return true;
}

bool ParseBool(const std::string& text, bool& value) {
	if (text == "true" || text == "1" || text == "on") {
		value = true;
		return true;
	}
	if (text == "false" || text == "0" || text == "off") {
		value = false;
		return true;
	}
	return false;
}

void CaptureUndo(Scene& scene, const std::string& label) { EditorUndoManager::GetInstance()->Capture(scene, "[CUI] " + label); }

// ---- 各コマンド ----------------------------------------------------------------

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

EditorCommandResult CommandSceneList(const EditorCommandArgs&) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	json objects = json::array();
	for (const std::unique_ptr<GameObject>& gameObject : scene->GetGameObjects()) {
		if (gameObject && gameObject->IsRoot()) {
			AppendHierarchy(*gameObject, objects);
		}
	}
	return EditorCommandResult::Success(objects);
}

EditorCommandResult CommandObjectGet(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	GameObject* gameObject = ResolveObject(*scene, args.Get(0), error);
	if (!gameObject) {
		return EditorCommandResult::Failure(error);
	}

	json result;
	result["path"] = MakeObjectPath(gameObject);
	result["id"] = gameObject->GetInstanceId();
	result["active"] = gameObject->IsActive();
	result["tag"] = gameObject->GetTag();
	result["layer"] = gameObject->GetLayer();
	// プレハブのインスタンスなら、元のプレハブ(Data 基準の相対パス)。そうでなければ空。
	result["prefab"] = gameObject->IsPrefabInstance() ? gameObject->GetPrefabAssetPath() : "";
	json children = json::array();
	for (const GameObject* child : gameObject->GetChildren()) {
		if (child) {
			children.push_back(child->GetName());
		}
	}
	result["children"] = children;

	json components = json::array();
	for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
		if (component) {
			components.push_back(DescribeComponent(*component, SameTypeIndexOf(*gameObject, component.get())));
		}
	}
	result["components"] = components;
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandComponentTypes(const EditorCommandArgs&) {
	return EditorCommandResult::Success(ComponentFactory::GetInstance().GetRegisteredTypeNames());
}

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

std::string MakeFileTimestamp() {
	const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
	const std::chrono::zoned_time localTime{std::chrono::current_zone(), now};
	return std::format("{:%Y%m%d_%H%M%S}", localTime);
}

// 相対パスはプロジェクトのフォルダ基準にする(起動したカレントフォルダに左右されないように)。
std::filesystem::path ResolveOutputPath(const std::string& text) {
	std::filesystem::path path(text);
	return path.is_absolute() ? path : GetActiveProjectRoot() / path;
}

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

json DescribeObjectFull(const GameObject& gameObject) {
	json entry;
	entry["path"] = MakeObjectPath(&gameObject);
	entry["id"] = gameObject.GetInstanceId();
	entry["parent"] = gameObject.GetParent() ? MakeObjectPath(gameObject.GetParent()) : "";
	entry["active"] = gameObject.IsActive();
	entry["activeInHierarchy"] = gameObject.IsActiveInHierarchy();
	entry["tag"] = gameObject.GetTag();
	entry["layer"] = gameObject.GetLayer();
	json components = json::array();
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (component) {
			components.push_back(DescribeComponent(*component, SameTypeIndexOf(gameObject, component.get())));
		}
	}
	entry["components"] = components;
	return entry;
}

void AppendHierarchyFull(const GameObject& gameObject, json& out) {
	out.push_back(DescribeObjectFull(gameObject));
	for (const GameObject* child : gameObject.GetChildren()) {
		if (child) {
			AppendHierarchyFull(*child, out);
		}
	}
}

EditorCommandResult CommandStateDump(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}

	// エディタの状態と、全オブジェクトの全フィールドを1つのJSONにまとめる(差分を取って変化を確かめる用)。
	json dump;
	dump["state"] = EditorCommandServer::GetInstance().BuildState();
	json objects = json::array();
	for (const std::unique_ptr<GameObject>& gameObject : scene->GetGameObjects()) {
		if (gameObject && gameObject->IsRoot()) {
			AppendHierarchyFull(*gameObject, objects);
		}
	}
	dump["objects"] = objects;

	if (args.Count() == 0) {
		return EditorCommandResult::Success(dump);
	}

	const std::filesystem::path outputPath = ResolveOutputPath(args.RestFrom(0));
	std::error_code errorCode;
	if (outputPath.has_parent_path()) {
		std::filesystem::create_directories(outputPath.parent_path(), errorCode);
	}
	std::ofstream output(outputPath, std::ios::trunc);
	if (!output) {
		return EditorCommandResult::Failure("書き出せませんでした: " + outputPath.string());
	}
	output << dump.dump(2, ' ', false, json::error_handler_t::replace) << '\n';

	json result;
	result["path"] = outputPath.string();
	result["objects"] = objects.size();
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandSelect(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	if (args.Get(0) == "none") {
		EditorSelection::GetInstance()->Clear();
		return EditorCommandResult::Success();
	}
	std::string error;
	GameObject* gameObject = ResolveObject(*scene, args.Get(0), error);
	if (!gameObject) {
		return EditorCommandResult::Failure(error);
	}
	EditorSelection::GetInstance()->SetSelectedGameObject(gameObject);
	return EditorCommandResult::Success(MakeObjectPath(gameObject));
}

EditorCommandResult CommandObjectCreate(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	const std::string& name = args.Get(0);
	if (name.empty() || name.find('/') != std::string::npos) {
		return EditorCommandResult::Failure("名前を指定してください(/ は使えません)。例: object.create Enemy Stage");
	}

	GameObject* parent = nullptr;
	if (args.Count() > 1) {
		std::string error;
		parent = ResolveObject(*scene, args.Get(1), error);
		if (!parent) {
			return EditorCommandResult::Failure(error);
		}
	}

	CaptureUndo(*scene, "object.create " + name);
	GameObject* created = scene->CreateGameObject(name);
	if (!created) {
		return EditorCommandResult::Failure("オブジェクトを作れませんでした。");
	}
	if (parent) {
		created->SetParent(parent, false);
	}
	scene->UpdateWorldTransforms();
	EditorSelection::GetInstance()->SetSelectedGameObject(created);

	json result;
	result["path"] = MakeObjectPath(created);
	result["id"] = created->GetInstanceId();
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandObjectDelete(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	GameObject* gameObject = ResolveObject(*scene, args.Get(0), error);
	if (!gameObject) {
		return EditorCommandResult::Failure(error);
	}

	const std::string path = MakeObjectPath(gameObject);
	CaptureUndo(*scene, "object.delete " + path);
	// 選択中のオブジェクト(またはその親)を消すなら、選択を先に外す(消えたオブジェクトを指さないように)。
	GameObject* selected = EditorSelection::GetInstance()->GetSelectedGameObject();
	if (selected && (selected == gameObject || selected->IsDescendantOf(gameObject))) {
		EditorSelection::GetInstance()->Clear();
	}
	scene->RemoveGameObjectHierarchy(gameObject);
	return EditorCommandResult::Success(path);
}

EditorCommandResult CommandObjectRename(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	GameObject* gameObject = ResolveObject(*scene, args.Get(0), error);
	if (!gameObject) {
		return EditorCommandResult::Failure(error);
	}
	const std::string& newName = args.Get(1);
	if (newName.empty() || newName.find('/') != std::string::npos) {
		return EditorCommandResult::Failure("新しい名前を指定してください(/ は使えません)。");
	}

	CaptureUndo(*scene, "object.rename " + MakeObjectPath(gameObject) + " -> " + newName);
	gameObject->SetName(newName);
	return EditorCommandResult::Success(MakeObjectPath(gameObject));
}

EditorCommandResult CommandObjectActive(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	GameObject* gameObject = ResolveObject(*scene, args.Get(0), error);
	if (!gameObject) {
		return EditorCommandResult::Failure(error);
	}
	bool active = false;
	if (!ParseBool(args.Get(1), active)) {
		return EditorCommandResult::Failure("true / false を指定してください。例: object.active Stage false");
	}

	CaptureUndo(*scene, "object.active " + MakeObjectPath(gameObject) + " " + (active ? "true" : "false"));
	gameObject->SetActive(active);
	return EditorCommandResult::Success(active);
}

EditorCommandResult CommandComponentAdd(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	GameObject* gameObject = ResolveObject(*scene, args.Get(0), error);
	if (!gameObject) {
		return EditorCommandResult::Failure(error);
	}
	const std::string& typeName = args.Get(1);
	std::unique_ptr<Component> component = ComponentFactory::GetInstance().Create(typeName);
	if (!component) {
		return EditorCommandResult::Failure("登録されていないコンポーネントです: " + typeName + "(component.types で一覧を表示できます)");
	}
	if (!component->AllowMultiple()) {
		for (const std::unique_ptr<Component>& existing : gameObject->GetComponents()) {
			if (existing && typeName == existing->GetTypeName()) {
				return EditorCommandResult::Failure(typeName + " は 1 つのオブジェクトに 1 つまでです。");
			}
		}
	}

	CaptureUndo(*scene, "component.add " + MakeObjectPath(gameObject) + " " + typeName);
	// Inspector の Add Component と同じ手順。
	Component* added = gameObject->AddComponent(std::move(component));
	scene->OnEditorComponentAdded(gameObject, added);
	return EditorCommandResult::Success(DescribeComponent(*added, SameTypeIndexOf(*gameObject, added)));
}

EditorCommandResult CommandComponentRemove(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	GameObject* gameObject = ResolveObject(*scene, args.Get(0), error);
	if (!gameObject) {
		return EditorCommandResult::Failure(error);
	}
	Component* component = ResolveComponent(*gameObject, args.Get(1), error);
	if (!component) {
		return EditorCommandResult::Failure(error);
	}
	if (!component->CanRemove()) {
		return EditorCommandResult::Failure(std::string(component->GetTypeName()) + " は外せません。");
	}

	CaptureUndo(*scene, "component.remove " + MakeObjectPath(gameObject) + " " + args.Get(1));
	gameObject->RemoveComponent(component);
	return EditorCommandResult::Success();
}

EditorCommandResult CommandFieldSet(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	if (args.Count() < 4) {
		return EditorCommandResult::Failure("使い方: field.set <オブジェクト> <型名[#番号]> <キー> <値>(例: field.set MonsterBall RotatorComponent speed 0.05)");
	}
	std::string error;
	GameObject* gameObject = ResolveObject(*scene, args.Get(0), error);
	if (!gameObject) {
		return EditorCommandResult::Failure(error);
	}
	Component* component = ResolveComponent(*gameObject, args.Get(1), error);
	if (!component) {
		return EditorCommandResult::Failure(error);
	}
	const std::string& key = args.Get(2);

	// 値は行末までをJSONとして読む(0.5 / true / "文字列" / [1, 2, 3])。読めなければ文字列として扱う。
	const std::string rawValue = args.RestFrom(3);
	json value = json::parse(rawValue, nullptr, false);
	if (value.is_discarded()) {
		value = rawValue;
	}

	// Undoと同じ経路で反映する: シーンのJSONの該当フィールドだけ書き換えて、ApplySceneJsonStringで読み戻す。
	json sceneJson = json::parse(scene->ToJson(), nullptr, false);
	if (sceneJson.is_discarded() || !sceneJson.contains("objects")) {
		return EditorCommandResult::Failure("シーンのJSONを作れませんでした。");
	}
	json* objectJson = nullptr;
	for (json& entry : sceneJson["objects"]) {
		if (entry.value("instanceId", std::string()) == gameObject->GetInstanceId()) {
			objectJson = &entry;
			break;
		}
	}
	const size_t componentIndex = FindComponentJsonIndex(*gameObject, component);
	if (!objectJson || !objectJson->contains("components") || componentIndex >= (*objectJson)["components"].size()) {
		return EditorCommandResult::Failure("シーンのJSONにこのコンポーネントが見つかりません。");
	}
	json& componentJson = (*objectJson)["components"][componentIndex];

	json before;
	if (key == "enabled") {
		if (!value.is_boolean()) {
			return EditorCommandResult::Failure("enabled は true / false で指定してください。");
		}
		before = componentJson.value("enabled", true);
		componentJson["enabled"] = value;
	} else {
		json& properties = componentJson["properties"];
		if (!properties.is_object() || !properties.contains(key)) {
			std::string keys;
			if (properties.is_object()) {
				for (auto it = properties.begin(); it != properties.end(); ++it) {
					keys += " " + it.key();
				}
			}
			return EditorCommandResult::Failure("フィールドが見つかりません: " + key + "。書き換えられるキー: enabled" + keys);
		}
		before = properties[key];
		if (!IsCompatibleValue(before, value, error)) {
			return EditorCommandResult::Failure(key + " の値が合いません: " + error);
		}
		properties[key] = value;
	}

	const std::string label = "field.set " + MakeObjectPath(gameObject) + "/" + args.Get(1) + "." + key;
	CaptureUndo(*scene, label);
	SceneJsonImporter::ImportResult importResult = SceneJsonImporter::ApplySceneJsonString(*scene, sceneJson.dump());
	if (!importResult.succeeded) {
		return EditorCommandResult::Failure("反映に失敗しました: " + importResult.message);
	}
	scene->UpdateWorldTransforms();

	// 読み戻した後の値を返す(コンポーネント側で丸められた等も分かるように)。
	json after = nullptr;
	if (key == "enabled") {
		after = component->IsEnabled();
	} else {
		json properties = json::object();
		component->WriteJson(properties);
		if (properties.contains(key)) {
			after = properties[key];
		}
	}

	json result;
	result["object"] = MakeObjectPath(gameObject);
	result["component"] = args.Get(1);
	result["key"] = key;
	result["before"] = before;
	result["value"] = after;
	return EditorCommandResult::Success(result);
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
	EditorCommandResult result = EditorCommandResult::Success(json{{"frames", frames}});
	result.waitFrames = frames;
	return result;
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

// ---- プレハブ(処理は PrefabEditing。Inspector / Hierarchy のボタンと同じもの) ----

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

// ---- アニメーション(処理は AnimationEditing。Animation ウィンドウと同じもの) ----

AnimatorComponent* ResolveAnimator(Scene& scene, const std::string& spec, std::string& error, bool requireClip) {
	GameObject* gameObject = ResolveObject(scene, spec, error);
	if (!gameObject) {
		return nullptr;
	}
	AnimatorComponent* animator = gameObject->GetComponent<AnimatorComponent>();
	if (!animator) {
		error = MakeObjectPath(gameObject) + " に AnimatorComponent がありません(component.add で足せます)。";
		return nullptr;
	}
	if (requireClip && !animator->HasClip()) {
		error = MakeObjectPath(gameObject) + " の Animator にクリップがありません(animation.createClip で作れます)。";
		return nullptr;
	}
	return animator;
}

bool ParseFloat(const std::string& text, float& value) {
	try {
		size_t used = 0;
		value = std::stof(text, &used);
		return used == text.size();
	} catch (...) {
		return false;
	}
}

EditorCommandResult CommandAnimationInfo(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, false);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}

	json result;
	json clips = json::array();
	for (const AnimationClipReference& reference : animator->GetClipReferences()) {
		clips.push_back(reference.path);
	}
	result["clips"] = clips;
	result["currentClip"] = animator->GetCurrentClipIndex();
	result["hasClip"] = animator->HasClip();
	result["playing"] = animator->IsPlaying();
	result["time"] = animator->GetTime();
	if (animator->HasClip()) {
		const AnimationClipData& clip = animator->GetClip();
		result["name"] = clip.name;
		result["wrapMode"] = AnimationClipAsset::ToString(clip.wrapMode);
		result["duration"] = clip.GetDuration();
		json tracks = json::array();
		for (const AnimationTrack& track : clip.tracks) {
			json keys = json::array();
			for (const AnimationKeyframe& key : track.curve.keys) {
				keys.push_back({{"time", key.time}, {"value", key.value}, {"easing", AnimationClipAsset::ToString(key.easing)}});
			}
			tracks.push_back({{"path", track.path}, {"additive", track.additive}, {"keys", keys}});
		}
		result["tracks"] = tracks;
	}
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandAnimationChannels(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, false);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}
	return EditorCommandResult::Success(AnimationEditing::ListChannels(*animator));
}

EditorCommandResult CommandAnimationCreateClip(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, false);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}
	if (args.Count() < 2) {
		return EditorCommandResult::Failure("クリップの名前を指定してください(例: animation.createClip Door Open)。");
	}
	std::filesystem::path clipPath;
	std::string message;
	if (!AnimationEditing::CreateClip(*animator, args.Get(1), clipPath, message)) {
		return EditorCommandResult::Failure(message);
	}
	EditorConsole::GetInstance()->AddLog("[Animation] " + message, EditorLogLevel::Info);
	return EditorCommandResult::Success(clipPath.generic_string());
}

EditorCommandResult CommandAnimationAddKey(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	if (args.Count() < 3) {
		return EditorCommandResult::Failure("使い方: animation.addKey <オブジェクト> <トラック> <時刻(秒)> [値](トラックは animation.channels で一覧)");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, true);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}
	const std::string& trackPath = args.Get(1);
	float time = 0.0f;
	if (!ParseFloat(args.Get(2), time) || time < 0.0f) {
		return EditorCommandResult::Failure("時刻は 0 以上の秒数で指定してください(例: 0.5)。");
	}

	// 値を指定しなければ、そのチャンネルの今の値でキーを打つ(Animation ウィンドウの Add Key と同じ)。
	float* channelValue = AnimationEditing::FindChannelValue(*animator, trackPath);
	float explicitValue = 0.0f;
	const float* keyValue = channelValue;
	if (args.Count() > 3) {
		if (!ParseFloat(args.Get(3), explicitValue)) {
			return EditorCommandResult::Failure("値は数値で指定してください(例: 1.5)。");
		}
		keyValue = &explicitValue;
	} else if (!channelValue && !animator->GetClip().FindTrack(trackPath)) {
		// 値もなく、チャンネルも既存トラックもない = たぶんトラック名の打ち間違い。
		return EditorCommandResult::Failure("トラックが見つかりません: " + trackPath + "(animation.channels で一覧を表示できます)");
	}

	const int keyIndex = AnimationEditing::AddKey(*animator, trackPath, time, keyValue);
	const AnimationKeyframe& key = animator->GetClip().FindTrack(trackPath)->curve.keys[keyIndex];
	json result;
	result["track"] = trackPath;
	result["time"] = key.time;
	result["value"] = key.value;
	result["note"] = "クリップのメモリ上だけの変更です。animation.save で保存します(シーンの Undo では戻りません)。";
	return EditorCommandResult::Success(result);
}

EditorCommandResult CommandAnimationRemoveKey(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, true);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}
	float time = 0.0f;
	if (args.Count() < 3 || !ParseFloat(args.Get(2), time)) {
		return EditorCommandResult::Failure("使い方: animation.removeKey <オブジェクト> <トラック> <時刻(秒)>");
	}
	// 表示や入力の丸めで少しずれても消せるよう、1/1000 秒の誤差は同じ時刻とみなす。
	if (!AnimationEditing::RemoveKey(*animator, args.Get(1), time, 0.001f)) {
		return EditorCommandResult::Failure("その時刻のキーがありません: " + args.Get(1) + " @ " + args.Get(2) + "(animation.info でキーを確認できます)");
	}
	return EditorCommandResult::Success();
}

EditorCommandResult CommandAnimationSave(const EditorCommandArgs& args) {
	Scene* scene = GetScene();
	if (!scene) {
		return EditorCommandResult::Failure("シーンがありません。");
	}
	std::string error;
	AnimatorComponent* animator = ResolveAnimator(*scene, args.Get(0), error, true);
	if (!animator) {
		return EditorCommandResult::Failure(error);
	}
	std::string message;
	if (!animator->SaveClip(message)) {
		return EditorCommandResult::Failure("クリップを保存できませんでした: " + message);
	}
	EditorConsole::GetInstance()->AddLog("[Animation] Saved: " + animator->GetClipPath(), EditorLogLevel::Info);
	return EditorCommandResult::Success(animator->GetClipPath());
}

EditorCommandResult CommandQuit(const EditorCommandArgs&) {
	RequestQuitApplication();
	return EditorCommandResult::Success("エディタを終了します。");
}

} // namespace

void RegisterBuiltinEditorCommands() {
	EditorCommandRegistry& registry = EditorCommandRegistry::GetInstance();

	registry.Register("help", "help [コマンド]", "コマンドの一覧、または指定したコマンドの使い方を表示する", CommandHelp);
	registry.Register("state", "state", "エディタの状態(モード・シーン・選択・Undoの先頭・フレーム番号)を表示する", CommandState);
	registry.Register("scene.list", "scene.list", "シーンの全オブジェクト(パス・instanceId・有効/無効・コンポーネント)を階層順に表示する", CommandSceneList);
	registry.Register("object.get", "object.get <オブジェクト>", "オブジェクトのコンポーネントと全フィールドの値を表示する", CommandObjectGet);
	registry.Register("component.types", "component.types", "追加できるコンポーネントの型名を表示する", CommandComponentTypes);
	registry.Register("log.tail", "log.tail [件数] [info|warning|error]", "Console の最近のログを表示する(既定 20 件)。重さを付けるとそれ以上のものだけ", CommandLogTail);
	registry.Register("log.file", "log.file", "今回の起動のログファイル(JSON Lines)の場所を表示する", CommandLogFile);
	registry.Register("state.dump", "state.dump [ファイル]", "エディタの状態と全オブジェクトの全フィールドを書き出す(ファイル省略時は返事に含める)", CommandStateDump);
	registry.Register("view.screenshot", "view.screenshot <scene|game|editor> [ファイル]",
	                  "ビューの描画結果(scene/game)かエディタ全体(editor)を PNG に保存する。既定は <プロジェクト>/Temp/Screenshots/", CommandViewScreenshot);

	registry.Register("select", "select <オブジェクト> | select none", "Hierarchy の選択を変える", CommandSelect);

	registry.Register("object.create", "object.create <名前> [親]", "空のオブジェクトを作る(親を指定するとその子にする)", CommandObjectCreate);
	registry.Register("object.delete", "object.delete <オブジェクト>", "オブジェクトを子ごと削除する", CommandObjectDelete);
	registry.Register("object.rename", "object.rename <オブジェクト> <新しい名前>", "オブジェクトの名前を変える", CommandObjectRename);
	registry.Register("object.active", "object.active <オブジェクト> <true|false>", "オブジェクトの有効/無効を切り替える", CommandObjectActive);
	registry.Register("component.add", "component.add <オブジェクト> <型名>", "コンポーネントを足す", CommandComponentAdd);
	registry.Register("component.remove", "component.remove <オブジェクト> <型名[#番号]>", "コンポーネントを外す", CommandComponentRemove);
	registry.Register("field.set", "field.set <オブジェクト> <型名[#番号]> <キー> <値>",
	                  "フィールドを書き換える。値は JSON(0.5 / true / \"文字\" / [1,2,3])。キー enabled でコンポーネントの有効/無効", CommandFieldSet);
	registry.Register("undo", "undo", "直前の操作を戻す", CommandUndo);
	registry.Register("redo", "redo", "戻した操作をやり直す", CommandRedo);

	registry.Register("play.start", "play.start", "Play に入る", CommandPlayStart);
	registry.Register("play.stop", "play.stop", "Play をやめて Edit に戻る(Play 前の状態に戻る)", CommandPlayStop);
	registry.Register("wait", "wait <フレーム数>", "指定フレーム進むのを待ってから返す。待っている間のログも返す", CommandWait);
	registry.Register("scene.save", "scene.save", "シーンを保存する(Ctrl+S と同じ)", CommandSceneSave);
	registry.Register("module.reload", "module.reload", "GameModule をビルドし直して差し替える(Reload DLL と同じ)", CommandModuleReload);
	registry.Register("prefab.list", "prefab.list", "プレハブファイルの一覧(prefab.instantiate にそのまま渡せるパス)", CommandPrefabList);
	registry.Register("prefab.create", "prefab.create <オブジェクト>", "オブジェクトと子階層をプレハブとして保存し、インスタンスにする(Hierarchy の Create Prefab と同じ)", CommandPrefabCreate);
	registry.Register("prefab.instantiate", "prefab.instantiate <プレハブのパス> [親]", "プレハブを配置して選択する", CommandPrefabInstantiate);
	registry.Register("prefab.apply", "prefab.apply <インスタンス>", "インスタンスの変更をプレハブへ書き戻す(Inspector の Apply と同じ)", CommandPrefabApply);
	registry.Register("prefab.revert", "prefab.revert <インスタンス>", "インスタンスをプレハブの内容に戻す(Inspector の Revert と同じ)", CommandPrefabRevert);
	registry.Register("prefab.unpack", "prefab.unpack <インスタンス>", "プレハブとのつながりを切る(Inspector の Unpack と同じ)", CommandPrefabUnpack);
	registry.Register("prefab.open", "prefab.open <インスタンス|プレハブのパス>", "プレハブ編集モードで開く(Open Prefab と同じ)", CommandPrefabOpen);
	registry.Register("prefab.save", "prefab.save", "プレハブ編集モードの内容を保存する(シーンのインスタンスも更新される)", CommandPrefabSave);
	registry.Register("prefab.close", "prefab.close [保存するか true|false]", "プレハブ編集モードを閉じてシーンに戻る(既定は保存しない)", CommandPrefabClose);

	registry.Register("animation.info", "animation.info <オブジェクト>", "Animator のクリップ・トラック・キーを表示する", CommandAnimationInfo);
	registry.Register("animation.channels", "animation.channels <オブジェクト>", "キーを打てるトラック(チャンネル)の一覧", CommandAnimationChannels);
	registry.Register("animation.createClip", "animation.createClip <オブジェクト> <名前>", "新しいクリップを Data/Animations/ に作って Animator に持たせる", CommandAnimationCreateClip);
	registry.Register("animation.addKey", "animation.addKey <オブジェクト> <トラック> <時刻(秒)> [値]",
	                  "キーを打つ(値を省略すると今の値)。メモリ上の変更なので animation.save で保存する", CommandAnimationAddKey);
	registry.Register("animation.removeKey", "animation.removeKey <オブジェクト> <トラック> <時刻(秒)>", "キーを消す", CommandAnimationRemoveKey);
	registry.Register("animation.save", "animation.save <オブジェクト>", "クリップをファイルへ保存する(Animation ウィンドウの Save Clip と同じ)", CommandAnimationSave);

	registry.Register("window.show", "window.show [ウィンドウ名] [true|false]",
	                  "ウィンドウを開いて前面に出す(false で閉じる)。引数なしでウィンドウの一覧と開閉を表示する", CommandWindowShow);
	registry.Register("quit", "quit", "エディタを終了する", CommandQuit);
}

} // namespace KujataEngine

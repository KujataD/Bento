// エディタの CUI のコマンド: シーンのオブジェクトとコンポーネントを見る・選ぶ・変えるコマンド。
// 設計と一覧は .claude/editor-automation.md。共通の関数は EditorCommandUtil にある。
#include "EditorCommandUtil.h"
#include "../EditorCommandServer.h"
#include "../EditorSelection.h"
#include "../SceneJsonImporter.h"
#include "../../scene/Component.h"
#include "../../scene/ComponentFactory.h"
#include "../../scene/GameObject.h"
#include "../../scene/Scene.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace KujataEngine {

namespace {

using namespace EditorCommandUtil;
using nlohmann::json;

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
		// 型情報に範囲があれば確かめる(黙って丸めずに、範囲を添えて失敗にする)。
		json fieldSchemas;
		if (component->DescribeSerializedFields(fieldSchemas)) {
			if (const json* fieldSchema = FindFieldSchema(fieldSchemas, key); fieldSchema && !CheckRange(*fieldSchema, value, error)) {
				return EditorCommandResult::Failure(key + " の値が" + error);
			}
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

} // namespace

void RegisterSceneCommands(EditorCommandRegistry& registry) {
	registry.Register("scene.list", "scene.list", "シーンの全オブジェクト(パス・instanceId・有効/無効・コンポーネント)を階層順に表示する", CommandSceneList);
	registry.Register("object.get", "object.get <オブジェクト>", "オブジェクトのコンポーネントと全フィールドの値を表示する", CommandObjectGet);
	registry.Register("component.types", "component.types", "追加できるコンポーネントの型名を表示する", CommandComponentTypes);
	registry.Register("state.dump", "state.dump [ファイル]", "エディタの状態と全オブジェクトの全フィールドを書き出す(ファイル省略時は返事に含める)", CommandStateDump);
	registry.Register("select", "select <オブジェクト> | select none", "Hierarchy の選択を変える", CommandSelect);
	registry.Register("object.create", "object.create <名前> [親]", "空のオブジェクトを作る(親を指定するとその子にする)", CommandObjectCreate);
	registry.Register("object.delete", "object.delete <オブジェクト>", "オブジェクトを子ごと削除する", CommandObjectDelete);
	registry.Register("object.rename", "object.rename <オブジェクト> <新しい名前>", "オブジェクトの名前を変える", CommandObjectRename);
	registry.Register("object.active", "object.active <オブジェクト> <true|false>", "オブジェクトの有効/無効を切り替える", CommandObjectActive);
	registry.Register("component.add", "component.add <オブジェクト> <型名>", "コンポーネントを足す", CommandComponentAdd);
	registry.Register("component.remove", "component.remove <オブジェクト> <型名[#番号]>", "コンポーネントを外す", CommandComponentRemove);
	registry.Register("field.set", "field.set <オブジェクト> <型名[#番号]> <キー> <値>",
	                  "フィールドを書き換える。値は JSON(0.5 / true / \"文字\" / [1,2,3])。キー enabled でコンポーネントの有効/無効", CommandFieldSet);
}

} // namespace KujataEngine

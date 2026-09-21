#include "EditorCommandUtil.h"

#include "../EditorApplication.h"
#include "../EditorUndoManager.h"
#include "../../base/ProjectPath.h"
#include "../../scene/Component.h"
#include "../../scene/GameObject.h"
#include "../../scene/Scene.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <vector>

namespace KujataEngine {

namespace EditorCommandUtil {

using nlohmann::json;

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

bool ParseFloat(const std::string& text, float& value) {
	try {
		size_t used = 0;
		value = std::stof(text, &used);
		return used == text.size();
	} catch (...) {
		return false;
	}
}

void CaptureUndo(Scene& scene, const std::string& label) { EditorUndoManager::GetInstance()->Capture(scene, "[CUI] " + label); }

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

// フィールドの型情報を探す(object の中は "親.子" では探さない。field.set が扱うのはトップレベルのキーだけ)。
const json* FindFieldSchema(const json& fields, const std::string& key) {
	for (const json& field : fields) {
		if (field.value("key", std::string()) == key) {
			return &field;
		}
	}
	return nullptr;
}

// 型情報に範囲があれば、値(数値、または数値の配列の各要素)が範囲内かを確かめる。
bool CheckRange(const json& fieldSchema, const json& value, std::string& error) {
	if (!fieldSchema.contains("min") || !fieldSchema.contains("max")) {
		return true;
	}
	const double minValue = fieldSchema["min"].get<double>();
	const double maxValue = fieldSchema["max"].get<double>();
	auto inRange = [&](const json& element) { return !element.is_number() || (element.get<double>() >= minValue && element.get<double>() <= maxValue); };
	const bool ok = value.is_array() ? std::all_of(value.begin(), value.end(), inRange) : inRange(value);
	if (!ok) {
		error = "範囲外です(" + json(minValue).dump() + " 〜 " + json(maxValue).dump() + ")。";
	}
	return ok;
}

} // namespace EditorCommandUtil

} // namespace KujataEngine

#include "PrefabAsset.h"
#include "../base/ProjectPath.h"
#include "../scene/Component.h"
#include "../scene/ComponentFactory.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace KujataEngine {

namespace {

using json = nlohmann::json;

struct PrefabWriteObject {
	const GameObject* gameObject = nullptr;
	std::string prefabObjectId;
	std::string parentPrefabObjectId;
};

struct PendingParentLink {
	GameObject* gameObject = nullptr;
	std::string parentPrefabObjectId;
};

std::string EscapeJsonString(const std::string& text) {
	std::string escaped;
	escaped.reserve(text.size());

	for (char character : text) {
		if (character == '\\') {
			escaped += "\\\\";
		} else if (character == '"') {
			escaped += "\\\"";
		} else if (character == '\n') {
			escaped += "\\n";
		} else if (character == '\r') {
			escaped += "\\r";
		} else if (character == '\t') {
			escaped += "\\t";
		} else {
			escaped += character;
		}
	}

	return escaped;
}

std::string SanitizeFileName(const std::string& name) {
	std::string sanitized;
	sanitized.reserve(name.size());

	for (char character : name) {
		bool invalid = character == '<' || character == '>' || character == ':' || character == '"' || character == '/' ||
		               character == '\\' || character == '|' || character == '?' || character == '*';
		if (invalid) {
			sanitized += '_';
		} else {
			sanitized += character;
		}
	}

	if (sanitized.empty()) {
		return "Prefab";
	}

	return sanitized;
}

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
	std::error_code error;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
	if (error) {
		return path.lexically_normal();
	}
	return normalized.lexically_normal();
}

std::filesystem::path ResolvePrefabPath(const std::filesystem::path& prefabPath) {
	if (prefabPath.is_absolute()) {
		return NormalizePath(prefabPath);
	}
	// "Prefabs/Foo.prefab.json"のような相対パスの基準はData配下(SaveAsPrefabの保存先と合わせる)。
	return NormalizePath(GetProjectDataRoot() / prefabPath);
}

std::string MakeStoredPrefabPath(const std::filesystem::path& prefabPath) {
	std::filesystem::path normalizedPrefabPath = ResolvePrefabPath(prefabPath);
	std::filesystem::path projectRoot = GetProjectDataRoot();

	std::error_code error;
	std::filesystem::path relative = std::filesystem::relative(normalizedPrefabPath, projectRoot, error);
	if (!error && !relative.empty()) {
		return relative.generic_string();
	}

	return normalizedPrefabPath.generic_string();
}

std::filesystem::path MakeUniquePrefabPath(const std::filesystem::path& prefabDirectory, const std::string& objectName) {
	std::string baseName = SanitizeFileName(objectName);
	std::filesystem::path candidate = prefabDirectory / (baseName + ".prefab.json");
	if (!std::filesystem::exists(candidate)) {
		return candidate;
	}

	for (uint32_t index = 1; index < 10000; ++index) {
		std::ostringstream suffix;
		suffix << baseName << "_" << index << ".prefab.json";
		candidate = prefabDirectory / suffix.str();
		if (!std::filesystem::exists(candidate)) {
			return candidate;
		}
	}

	return prefabDirectory / (baseName + "_9999.prefab.json");
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text, std::string& message) {
	std::ofstream file(path, std::ios::binary);
	if (!file) {
		message = "Failed to open Prefab file: " + path.string();
		return false;
	}

	file << text;
	if (!file) {
		message = "Failed to write Prefab file: " + path.string();
		return false;
	}

	return true;
}

bool LoadJsonFile(const std::filesystem::path& path, json& outJson, std::string& message) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		message = "Failed to open Prefab file: " + path.string();
		return false;
	}

	try {
		file >> outJson;
	} catch (const json::exception& exception) {
		message = "Failed to parse Prefab JSON: " + path.string() + " : " + exception.what();
		return false;
	}

	return true;
}

std::string ReadString(const json& value, const std::string& key, const std::string& defaultValue) {
	if (!value.contains(key)) {
		return defaultValue;
	}
	if (!value.at(key).is_string()) {
		return defaultValue;
	}
	return value.at(key).get<std::string>();
}

bool ReadBool(const json& value, const std::string& key, bool defaultValue) {
	if (!value.contains(key)) {
		return defaultValue;
	}
	if (!value.at(key).is_boolean()) {
		return defaultValue;
	}
	return value.at(key).get<bool>();
}

uint32_t ReadUint32(const json& value, const std::string& key, uint32_t defaultValue) {
	if (!value.contains(key)) {
		return defaultValue;
	}
	if (!value.at(key).is_number_integer() && !value.at(key).is_number_unsigned()) {
		return defaultValue;
	}

	int64_t rawValue = value.at(key).get<int64_t>();
	if (rawValue < 0) {
		return defaultValue;
	}
	return static_cast<uint32_t>(rawValue);
}

bool IsTransformTypeName(const std::string& typeName) {
	if (typeName == "Transform") {
		return true;
	}
	if (typeName == "TransformComponent") {
		return true;
	}
	return false;
}

const json& GetComponentPropertiesJson(const json& componentJson) {
	if (componentJson.contains("properties") && componentJson.at("properties").is_object()) {
		return componentJson.at("properties");
	}

	// 旧形式や手書きJSONではComponent固有値が直下にある可能性も許容する。
	return componentJson;
}

void CollectPrefabWriteObjects(
    const GameObject& gameObject,
    const std::string& parentPrefabObjectId,
    std::vector<PrefabWriteObject>& outObjects) {
	std::ostringstream idStream;
	idStream << "object_" << outObjects.size();

	PrefabWriteObject writeObject{};
	writeObject.gameObject = &gameObject;
	writeObject.prefabObjectId = idStream.str();
	writeObject.parentPrefabObjectId = parentPrefabObjectId;
	outObjects.push_back(writeObject);

	const std::string currentPrefabObjectId = writeObject.prefabObjectId;
	for (GameObject* child : gameObject.GetChildren()) {
		if (child) {
			CollectPrefabWriteObjects(*child, currentPrefabObjectId, outObjects);
		}
	}
}

void WriteComponentsJson(std::ostream& os, const GameObject& gameObject, int indent) {
	const std::vector<std::unique_ptr<Component>>& components = gameObject.GetComponents();
	for (size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
		const std::unique_ptr<Component>& component = components[componentIndex];
		if (!component) {
			continue;
		}

		component->WriteJson(os, indent);
		if (componentIndex + 1 < components.size()) {
			os << ",";
		}
		os << "\n";
	}
}

std::string BuildPrefabJson(const GameObject& rootObject, size_t& outGameObjectCount, size_t& outComponentCount) {
	std::vector<PrefabWriteObject> writeObjects;
	CollectPrefabWriteObjects(rootObject, "", writeObjects);
	outGameObjectCount = writeObjects.size();

	std::ostringstream os;
	os << "{\n";
	os << "  \"assetType\": \"Prefab\",\n";
	os << "  \"version\": 1,\n";
	os << "  \"name\": \"" << EscapeJsonString(rootObject.GetName()) << "\",\n";
	os << "  \"rootObjectId\": \"object_0\",\n";
	os << "  \"objects\": [\n";

	for (size_t objectIndex = 0; objectIndex < writeObjects.size(); ++objectIndex) {
		const PrefabWriteObject& writeObject = writeObjects[objectIndex];
		const GameObject* gameObject = writeObject.gameObject;
		if (!gameObject) {
			continue;
		}

		os << "    {\n";
		os << "      \"prefabObjectId\": \"" << EscapeJsonString(writeObject.prefabObjectId) << "\",\n";
		os << "      \"parentPrefabObjectId\": \"" << EscapeJsonString(writeObject.parentPrefabObjectId) << "\",\n";
		os << "      \"name\": \"" << EscapeJsonString(gameObject->GetName()) << "\",\n";
		// タグとレイヤーもPrefabに残す。
		// これが無いとPrefabから生成したオブジェクトが必ずUntagged/レイヤー0になり、
		// タグで対象を選ぶ仕組み(ヘイト・ロックオン)やカメラの遮蔽マスクがPrefab経由で機能しない。
		os << "      \"tag\": \"" << EscapeJsonString(gameObject->GetTag()) << "\",\n";
		os << "      \"layer\": " << gameObject->GetLayer() << ",\n";
		os << "      \"active\": ";
		if (gameObject->IsActive()) {
			os << "true,\n";
		} else {
			os << "false,\n";
		}
		os << "      \"components\": [\n";

		const std::vector<std::unique_ptr<Component>>& components = gameObject->GetComponents();
		outComponentCount += components.size();
		WriteComponentsJson(os, *gameObject, 8);

		os << "      ]\n";
		os << "    }";
		if (objectIndex + 1 < writeObjects.size()) {
			os << ",";
		}
		os << "\n";
	}

	os << "  ]\n";
	os << "}\n";
	return os.str();
}

void ApplyComponent(Component& component, const json& componentJson) {
	component.SetEnabled(ReadBool(componentJson, "enabled", component.IsEnabled()));
	component.ReadJson(GetComponentPropertiesJson(componentJson));
}

void NotifyComponentsAfterReadJson(GameObject& gameObject) {
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (component) {
			component->OnAfterReadJson();
		}
	}
}

bool ContainsComponentPointer(const std::vector<Component*>& components, Component* target) {
	return std::find(components.begin(), components.end(), target) != components.end();
}

Component* FindExistingComponent(GameObject& gameObject, const std::string& typeName, const std::vector<Component*>& importedComponents) {
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (!component) {
			continue;
		}
		if (ContainsComponentPointer(importedComponents, component.get())) {
			continue;
		}
		if (component->GetTypeName() == typeName) {
			return component.get();
		}
	}

	return nullptr;
}

size_t ApplyPrefabComponents(Scene& scene, GameObject& gameObject, const json& gameObjectJson) {
	std::vector<Component*> importedComponents;

	Component* transformComponent = gameObject.EnsureTransformComponent();
	if (transformComponent) {
		importedComponents.push_back(transformComponent);
	}

	if (!gameObjectJson.contains("components") || !gameObjectJson.at("components").is_array()) {
		NotifyComponentsAfterReadJson(gameObject);
		return importedComponents.size();
	}

	for (const json& componentJson : gameObjectJson.at("components")) {
		if (!componentJson.is_object()) {
			continue;
		}

		std::string typeName = ReadString(componentJson, "type", "");
		if (typeName.empty()) {
			continue;
		}

		Component* component = nullptr;
		if (IsTransformTypeName(typeName)) {
			component = gameObject.EnsureTransformComponent();
		} else {
			component = FindExistingComponent(gameObject, typeName, importedComponents);
			if (!component) {
				std::unique_ptr<Component> createdComponent = ComponentFactory::GetInstance().Create(typeName);
				component = gameObject.AddComponent(std::move(createdComponent));
				if (component) {
					scene.OnEditorComponentAdded(&gameObject, component);
				}
			}
		}

		if (!component) {
			continue;
		}

		if (!ContainsComponentPointer(importedComponents, component)) {
			importedComponents.push_back(component);
		}
		ApplyComponent(*component, componentJson);
	}

	std::vector<Component*> removeTargets;
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (!component) {
			continue;
		}
		if (!component->CanRemove()) {
			continue;
		}
		if (ContainsComponentPointer(importedComponents, component.get())) {
			continue;
		}
		removeTargets.push_back(component.get());
	}

	for (Component* component : removeTargets) {
		gameObject.RemoveComponent(component);
	}

	NotifyComponentsAfterReadJson(gameObject);
	return importedComponents.size();
}

GameObject* FindInstantiatedObject(
    const std::unordered_map<std::string, GameObject*>& objectsByPrefabId,
    const std::string& prefabObjectId) {
	auto found = objectsByPrefabId.find(prefabObjectId);
	if (found == objectsByPrefabId.end()) {
		return nullptr;
	}
	return found->second;
}

void CollectGameObjectHierarchy(GameObject& gameObject, std::vector<GameObject*>& outObjects) {
	outObjects.push_back(&gameObject);
	for (GameObject* child : gameObject.GetChildren()) {
		if (child) {
			CollectGameObjectHierarchy(*child, outObjects);
		}
	}
}

void BindHierarchyToPrefabInternal(GameObject& rootObject, const std::filesystem::path& prefabPath) {
	std::vector<GameObject*> objects;
	CollectGameObjectHierarchy(rootObject, objects);

	std::string storedPrefabPath = MakeStoredPrefabPath(prefabPath);
	std::string rootInstanceId = rootObject.GetInstanceId();
	for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
		GameObject* object = objects[objectIndex];
		if (!object) {
			continue;
		}

		std::string prefabObjectId = "object_" + std::to_string(objectIndex);
		object->SetPrefabLink(storedPrefabPath, prefabObjectId, rootInstanceId, object == &rootObject);
	}
}

void ClearPrefabLinkHierarchy(GameObject& rootObject) {
	std::vector<GameObject*> objects;
	CollectGameObjectHierarchy(rootObject, objects);
	for (GameObject* object : objects) {
		if (object) {
			object->ClearPrefabLink();
		}
	}
}

/// <summary>読み込み済みJSONからGameObjectエントリを取り出す(ファイル/メモリ共通)。</summary>
bool ParsePrefabObjectEntries(
    const json& prefabJson,
    const std::string& sourceLabel,
    std::vector<json>& outObjectEntries,
    std::string& outRootObjectId,
    std::string& message) {
	std::string assetType = ReadString(prefabJson, "assetType", "");
	if (!assetType.empty() && assetType != "Prefab") {
		message = "JSON is not a Prefab: " + sourceLabel;
		return false;
	}

	outObjectEntries.clear();
	if (prefabJson.contains("objects") && prefabJson.at("objects").is_array()) {
		for (const json& objectEntry : prefabJson.at("objects")) {
			if (objectEntry.is_object()) {
				outObjectEntries.push_back(objectEntry);
			}
		}
	} else {
		outObjectEntries.push_back(prefabJson);
	}

	outRootObjectId = ReadString(prefabJson, "rootObjectId", "object_0");
	if (outObjectEntries.empty()) {
		message = "Prefab does not contain GameObject entries: " + sourceLabel;
		return false;
	}

	return true;
}

/// <summary>
/// GameObjectエントリ群からScene上に階層を生成する(ファイル/メモリ共通のInstantiate本体)。
/// Prefabとしての関連付けは呼び出し側が行う。
/// </summary>
PrefabAsset::InstantiateResult InstantiateFromEntries(Scene& scene, const std::vector<json>& objectEntries, const std::string& rootObjectId,
                                                     std::unordered_map<std::string, GameObject*>& outObjectsByPrefabId) {
	PrefabAsset::InstantiateResult result{};
	std::vector<PendingParentLink> parentLinks;

	for (size_t objectIndex = 0; objectIndex < objectEntries.size(); ++objectIndex) {
		const json& objectJson = objectEntries[objectIndex];

		std::string defaultPrefabObjectId = "object_" + std::to_string(objectIndex);
		std::string prefabObjectId = ReadString(objectJson, "prefabObjectId", defaultPrefabObjectId);
		std::string parentPrefabObjectId = ReadString(objectJson, "parentPrefabObjectId", "");
		std::string objectName = ReadString(objectJson, "name", "PrefabObject");

		GameObject* gameObject = scene.CreateGameObject(objectName);
		if (!gameObject) {
			continue;
		}

		gameObject->SetActive(ReadBool(objectJson, "active", gameObject->IsActive()));
		// 古いPrefabにはtag/layerが無い。その場合は生成直後の既定値(Untagged / 0)のままにする。
		gameObject->SetTag(ReadString(objectJson, "tag", gameObject->GetTag()));
		gameObject->SetLayer(ReadUint32(objectJson, "layer", gameObject->GetLayer()));
		result.componentCount += ApplyPrefabComponents(scene, *gameObject, objectJson);

		outObjectsByPrefabId[prefabObjectId] = gameObject;
		parentLinks.push_back({gameObject, parentPrefabObjectId});
		++result.gameObjectCount;

		if (prefabObjectId == rootObjectId || !result.rootObject) {
			result.rootObject = gameObject;
		}
	}

	for (const PendingParentLink& link : parentLinks) {
		if (!link.gameObject) {
			continue;
		}
		GameObject* parent = FindInstantiatedObject(outObjectsByPrefabId, link.parentPrefabObjectId);
		link.gameObject->SetParent(parent);
	}

	result.succeeded = result.rootObject != nullptr;
	return result;
}

bool LoadPrefabObjectEntries(
    const std::filesystem::path& prefabPath,
    std::vector<json>& outObjectEntries,
    std::string& outRootObjectId,
    std::string& message) {
	json prefabJson;
	if (!LoadJsonFile(ResolvePrefabPath(prefabPath), prefabJson, message)) {
		return false;
	}

	return ParsePrefabObjectEntries(prefabJson, prefabPath.string(), outObjectEntries, outRootObjectId, message);
}

std::unordered_map<std::string, GameObject*> BuildExistingPrefabObjectMap(GameObject& rootObject) {
	std::vector<GameObject*> hierarchyObjects;
	CollectGameObjectHierarchy(rootObject, hierarchyObjects);

	std::unordered_map<std::string, GameObject*> objectsByPrefabId;
	for (GameObject* object : hierarchyObjects) {
		if (!object) {
			continue;
		}
		if (object->GetPrefabObjectId().empty()) {
			continue;
		}
		objectsByPrefabId[object->GetPrefabObjectId()] = object;
	}

	return objectsByPrefabId;
}

GameObject* FindObjectByPrefabId(const std::unordered_map<std::string, GameObject*>& objectsByPrefabId, const std::string& prefabObjectId) {
	auto found = objectsByPrefabId.find(prefabObjectId);
	if (found == objectsByPrefabId.end()) {
		return nullptr;
	}
	return found->second;
}

} // namespace

PrefabAsset::SaveResult PrefabAsset::SaveAsPrefab(const GameObject& rootObject, const std::filesystem::path& projectRoot) {
	SaveResult result{};

	if (projectRoot.empty()) {
		result.message = "ProjectDir is empty.";
		return result;
	}

	std::filesystem::path normalizedProjectRoot = NormalizePath(projectRoot);
	std::filesystem::path prefabDirectory = normalizedProjectRoot / "Prefabs";

	std::error_code error;
	std::filesystem::create_directories(prefabDirectory, error);
	if (error) {
		result.message = "Failed to create Prefabs directory: " + error.message();
		return result;
	}

	result.outputPath = MakeUniquePrefabPath(prefabDirectory, rootObject.GetName());
	return SaveToPath(rootObject, result.outputPath);
}

PrefabAsset::SaveResult PrefabAsset::SaveToPath(const GameObject& rootObject, const std::filesystem::path& prefabPath) {
	SaveResult result{};

	if (prefabPath.empty()) {
		result.message = "Prefab path is empty.";
		return result;
	}

	std::filesystem::path normalizedPrefabPath = ResolvePrefabPath(prefabPath);
	std::error_code error;
	std::filesystem::create_directories(normalizedPrefabPath.parent_path(), error);
	if (error) {
		result.message = "Failed to create Prefab directory: " + error.message();
		return result;
	}

	result.outputPath = normalizedPrefabPath;
	std::string prefabJson = BuildPrefabJson(rootObject, result.gameObjectCount, result.componentCount);
	if (!WriteTextFile(result.outputPath, prefabJson, result.message)) {
		return result;
	}

	result.succeeded = true;
	result.message = "Saved Prefab.";
	return result;
}

PrefabAsset::InstantiateResult PrefabAsset::Instantiate(Scene& scene, const std::filesystem::path& prefabPath, bool linkInstance) {
	InstantiateResult result{};

	std::vector<json> objectEntries;
	std::string rootObjectId;
	if (!LoadPrefabObjectEntries(prefabPath, objectEntries, rootObjectId, result.message)) {
		return result;
	}

	std::unordered_map<std::string, GameObject*> objectsByPrefabId;
	result = InstantiateFromEntries(scene, objectEntries, rootObjectId, objectsByPrefabId);

	if (linkInstance && result.rootObject) {
		std::string storedPrefabPath = MakeStoredPrefabPath(prefabPath);
		std::string rootInstanceId = result.rootObject->GetInstanceId();
		for (const auto& [prefabObjectId, gameObject] : objectsByPrefabId) {
			if (!gameObject) {
				continue;
			}
			gameObject->SetPrefabLink(storedPrefabPath, prefabObjectId, rootInstanceId, gameObject == result.rootObject);
		}
	}

	result.succeeded = result.rootObject != nullptr;
	if (result.succeeded) {
		result.message = "Instantiated Prefab.";
	} else {
		result.message = "Failed to instantiate Prefab: " + prefabPath.string();
	}

	return result;
}

std::string PrefabAsset::CopyHierarchyToJson(const GameObject& rootObject) {
	size_t gameObjectCount = 0;
	size_t componentCount = 0;
	return BuildPrefabJson(rootObject, gameObjectCount, componentCount);
}

PrefabAsset::InstantiateResult PrefabAsset::InstantiateFromJson(Scene& scene, const std::string& prefabJsonText) {
	InstantiateResult result{};

	json prefabJson;
	try {
		prefabJson = json::parse(prefabJsonText);
	} catch (const json::exception& exception) {
		result.message = std::string("Failed to parse GameObject JSON: ") + exception.what();
		return result;
	}

	std::vector<json> objectEntries;
	std::string rootObjectId;
	if (!ParsePrefabObjectEntries(prefabJson, "clipboard", objectEntries, rootObjectId, result.message)) {
		return result;
	}

	// Prefabとしての関連付けはしない(コピー元Prefabに紐づかない独立したObjectにする)。
	std::unordered_map<std::string, GameObject*> objectsByPrefabId;
	result = InstantiateFromEntries(scene, objectEntries, rootObjectId, objectsByPrefabId);
	result.message = result.succeeded ? "Pasted GameObject." : "Failed to paste GameObject.";
	return result;
}

void PrefabAsset::BindHierarchyToPrefab(GameObject& rootObject, const std::filesystem::path& prefabPath) {
	BindHierarchyToPrefabInternal(rootObject, prefabPath);
}

GameObject* PrefabAsset::FindPrefabInstanceRoot(Scene& scene, GameObject& object) {
	if (!object.IsPrefabInstance()) {
		return nullptr;
	}
	if (object.IsPrefabInstanceRoot()) {
		return &object;
	}

	GameObject* root = scene.FindGameObjectByInstanceId(object.GetPrefabInstanceRootId());
	if (root && root->IsPrefabInstanceRoot()) {
		return root;
	}

	GameObject* current = &object;
	while (current) {
		if (current->IsPrefabInstanceRoot()) {
			return current;
		}
		current = current->GetParent();
	}

	return nullptr;
}

PrefabAsset::InstantiateResult PrefabAsset::RevertPrefabInstance(Scene& scene, GameObject& instanceObject) {
	InstantiateResult result{};
	GameObject* rootObject = FindPrefabInstanceRoot(scene, instanceObject);
	if (!rootObject) {
		result.message = "Selected GameObject is not a Prefab Instance.";
		return result;
	}

	std::filesystem::path prefabPath(rootObject->GetPrefabAssetPath());
	std::vector<json> objectEntries;
	std::string rootObjectId;
	if (!LoadPrefabObjectEntries(prefabPath, objectEntries, rootObjectId, result.message)) {
		return result;
	}

	GameObject* originalParent = rootObject->GetParent();
	std::unordered_map<std::string, GameObject*> objectsByPrefabId = BuildExistingPrefabObjectMap(*rootObject);
	std::vector<PendingParentLink> parentLinks;
	std::vector<std::string> importedPrefabObjectIds;

	for (size_t objectIndex = 0; objectIndex < objectEntries.size(); ++objectIndex) {
		const json& objectJson = objectEntries[objectIndex];
		std::string defaultPrefabObjectId = "object_" + std::to_string(objectIndex);
		std::string prefabObjectId = ReadString(objectJson, "prefabObjectId", defaultPrefabObjectId);
		std::string parentPrefabObjectId = ReadString(objectJson, "parentPrefabObjectId", "");
		std::string objectName = ReadString(objectJson, "name", "PrefabObject");

		GameObject* gameObject = FindObjectByPrefabId(objectsByPrefabId, prefabObjectId);
		if (!gameObject) {
			gameObject = scene.CreateGameObject(objectName);
			objectsByPrefabId[prefabObjectId] = gameObject;
		}
		if (!gameObject) {
			continue;
		}

		gameObject->SetName(objectName);
		gameObject->SetActive(ReadBool(objectJson, "active", gameObject->IsActive()));
		// 古いPrefabにはtag/layerが無い。その場合は生成直後の既定値(Untagged / 0)のままにする。
		gameObject->SetTag(ReadString(objectJson, "tag", gameObject->GetTag()));
		gameObject->SetLayer(ReadUint32(objectJson, "layer", gameObject->GetLayer()));
		result.componentCount += ApplyPrefabComponents(scene, *gameObject, objectJson);
		importedPrefabObjectIds.push_back(prefabObjectId);
		parentLinks.push_back({gameObject, parentPrefabObjectId});
		++result.gameObjectCount;

		if (prefabObjectId == rootObjectId || !result.rootObject) {
			result.rootObject = gameObject;
		}
	}

	if (!result.rootObject) {
		result.message = "Failed to find Prefab root while reverting.";
		return result;
	}

	std::string storedPrefabPath = MakeStoredPrefabPath(prefabPath);
	std::string rootInstanceId = result.rootObject->GetInstanceId();
	for (const auto& [prefabObjectId, gameObject] : objectsByPrefabId) {
		if (!gameObject) {
			continue;
		}
		if (std::find(importedPrefabObjectIds.begin(), importedPrefabObjectIds.end(), prefabObjectId) == importedPrefabObjectIds.end()) {
			continue;
		}
		gameObject->SetPrefabLink(storedPrefabPath, prefabObjectId, rootInstanceId, gameObject == result.rootObject);
	}

	for (const PendingParentLink& link : parentLinks) {
		if (!link.gameObject) {
			continue;
		}

		GameObject* parent = FindObjectByPrefabId(objectsByPrefabId, link.parentPrefabObjectId);
		if (!parent && link.parentPrefabObjectId.empty()) {
			parent = originalParent;
		}
		link.gameObject->SetParent(parent);
	}

	std::vector<GameObject*> hierarchyObjects;
	CollectGameObjectHierarchy(*result.rootObject, hierarchyObjects);
	std::vector<GameObject*> removeCandidates;
	for (GameObject* object : hierarchyObjects) {
		if (!object) {
			continue;
		}
		if (object->GetPrefabInstanceRootId() != rootInstanceId) {
			continue;
		}
		if (std::find(importedPrefabObjectIds.begin(), importedPrefabObjectIds.end(), object->GetPrefabObjectId()) != importedPrefabObjectIds.end()) {
			continue;
		}
		removeCandidates.push_back(object);
	}

	std::vector<GameObject*> removeRoots;
	for (GameObject* candidate : removeCandidates) {
		if (!candidate) {
			continue;
		}

		bool ancestorWillBeRemoved = false;
		GameObject* parent = candidate->GetParent();
		while (parent) {
			if (std::find(removeCandidates.begin(), removeCandidates.end(), parent) != removeCandidates.end()) {
				ancestorWillBeRemoved = true;
				break;
			}
			parent = parent->GetParent();
		}

		if (!ancestorWillBeRemoved) {
			removeRoots.push_back(candidate);
		}
	}

	for (GameObject* removeRoot : removeRoots) {
		scene.RemoveGameObjectHierarchy(removeRoot);
	}

	result.succeeded = true;
	result.message = "Reverted Prefab Instance.";
	return result;
}

PrefabAsset::SaveResult PrefabAsset::ApplyPrefabInstance(Scene& scene, GameObject& instanceObject) {
	SaveResult result{};
	GameObject* rootObject = FindPrefabInstanceRoot(scene, instanceObject);
	if (!rootObject) {
		result.message = "Selected GameObject is not a Prefab Instance.";
		return result;
	}

	std::filesystem::path prefabPath(rootObject->GetPrefabAssetPath());
	result = SaveToPath(*rootObject, prefabPath);
	if (!result.succeeded) {
		return result;
	}

	BindHierarchyToPrefabInternal(*rootObject, prefabPath);
	RefreshInstancesFromPrefab(scene, prefabPath, rootObject);
	result.message = "Applied Prefab Instance.";
	return result;
}

bool PrefabAsset::UnpackPrefabInstance(Scene& scene, GameObject& instanceObject) {
	GameObject* rootObject = FindPrefabInstanceRoot(scene, instanceObject);
	if (!rootObject) {
		return false;
	}

	ClearPrefabLinkHierarchy(*rootObject);
	return true;
}

size_t PrefabAsset::RefreshInstancesFromPrefab(Scene& scene, const std::filesystem::path& prefabPath, GameObject* ignoreRoot) {
	std::string storedPrefabPath = MakeStoredPrefabPath(prefabPath);
	std::vector<GameObject*> updateTargets;
	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject) {
			continue;
		}
		if (!gameObject->IsPrefabInstanceRoot()) {
			continue;
		}
		if (gameObject.get() == ignoreRoot) {
			continue;
		}
		if (gameObject->GetPrefabAssetPath() != storedPrefabPath) {
			continue;
		}
		updateTargets.push_back(gameObject.get());
	}

	size_t updatedCount = 0;
	for (GameObject* target : updateTargets) {
		if (!target) {
			continue;
		}
		InstantiateResult result = RevertPrefabInstance(scene, *target);
		if (result.succeeded) {
			++updatedCount;
		}
	}

	return updatedCount;
}

} // namespace KujataEngine

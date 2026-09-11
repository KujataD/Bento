#include "SceneJsonImporter.h"
#include "../scene/ComponentFactory.h"
#include "../scene/GameObject.h"
#include "../scene/ObjectRef.h"
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
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace KujataEngine {

namespace {

using json = nlohmann::json;

struct PendingParentLink {
	GameObject* gameObject = nullptr;
	std::string parentInstanceId;
};

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
		return "GameObject";
	}

	return sanitized;
}

bool LoadJsonFile(const std::filesystem::path& path, json& outJson, std::string& message) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		message = "Failed to open JSON: " + path.string();
		return false;
	}

	try {
		file >> outJson;
	} catch (const json::exception& exception) {
		message = "Failed to parse JSON: " + path.string() + " : " + exception.what();
		return false;
	}

	return true;
}

std::filesystem::path ResolveAssetPath(const std::filesystem::path& projectRoot, const std::string& assetPathText) {
	std::filesystem::path assetPath(assetPathText);
	if (assetPath.is_absolute()) {
		return assetPath.lexically_normal();
	}
	return (projectRoot / assetPath).lexically_normal();
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

bool ContainsPointer(const std::vector<GameObject*>& objects, GameObject* target) {
	return std::find(objects.begin(), objects.end(), target) != objects.end();
}

bool ContainsPointer(const std::vector<Component*>& components, Component* target) {
	return std::find(components.begin(), components.end(), target) != components.end();
}

bool IsInstanceIdUsedByOtherObject(Scene& scene, const std::string& instanceId, const GameObject* ignoreObject) {
	if (instanceId.empty()) {
		return false;
	}

	for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
		if (!gameObject) {
			continue;
		}
		if (gameObject.get() == ignoreObject) {
			continue;
		}
		if (gameObject->GetInstanceId() == instanceId) {
			return true;
		}
	}

	return false;
}

GameObject* FindExistingGameObject(
    Scene& scene,
    const std::string& instanceId,
    size_t objectIndex,
    const std::string& objectName,
    const std::vector<GameObject*>& importedObjects) {
	std::vector<std::unique_ptr<GameObject>>& gameObjects = scene.GetGameObjects();

	// 現行形式ではinstanceIdを最優先する。名前変更や並び替えがあっても同じObjectへ復元できる。
	if (!instanceId.empty()) {
		for (const std::unique_ptr<GameObject>& gameObject : gameObjects) {
			if (!gameObject) {
				continue;
			}
			if (ContainsPointer(importedObjects, gameObject.get())) {
				continue;
			}
			if (gameObject->GetInstanceId() == instanceId) {
				return gameObject.get();
			}
		}
	}

	// 旧形式JSONにはinstanceIdがないため、互換用に名前一致へフォールバックする。
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects) {
		if (!gameObject) {
			continue;
		}
		if (ContainsPointer(importedObjects, gameObject.get())) {
			continue;
		}
		if (gameObject->GetName() == objectName) {
			return gameObject.get();
		}
	}

	// 名前も変わっている旧形式JSONでは、最後に保存順と初期Scene生成順で対応付ける。
	if (objectIndex < gameObjects.size()) {
		GameObject* indexedObject = gameObjects[objectIndex].get();
		if (indexedObject && !ContainsPointer(importedObjects, indexedObject)) {
			return indexedObject;
		}
	}

	return nullptr;
}

Component* FindExistingComponent(GameObject& gameObject, const std::string& typeName, const std::vector<Component*>& importedComponents) {
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (!component) {
			continue;
		}
		if (ContainsPointer(importedComponents, component.get())) {
			continue;
		}
		if (component->GetTypeName() == typeName) {
			return component.get();
		}
	}

	return nullptr;
}

GameObject* FindImportedGameObjectByInstanceId(const std::vector<GameObject*>& importedObjects, const std::string& instanceId) {
	if (instanceId.empty()) {
		return nullptr;
	}

	for (GameObject* gameObject : importedObjects) {
		if (!gameObject) {
			continue;
		}
		if (gameObject->GetInstanceId() == instanceId) {
			return gameObject;
		}
	}

	return nullptr;
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

	// 旧形式ではComponent固有の値が直下にあったため、互換用に直下も読めるようにする。
	return componentJson;
}

void ApplyComponent(Component& component, const json& componentJson) {
	component.SetEnabled(ReadBool(componentJson, "enabled", component.IsEnabled()));
	component.ReadJson(GetComponentPropertiesJson(componentJson));
}

void NotifyComponentsAfterReadJson(GameObject& gameObject) {
	for (const std::unique_ptr<Component>& component : gameObject.GetComponents()) {
		if (!component) {
			continue;
		}
		component->OnAfterReadJson();
	}
}

size_t ApplyComponents(Scene& scene, GameObject& gameObject, const json& gameObjectJson) {
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

		if (!ContainsPointer(importedComponents, component)) {
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
		if (ContainsPointer(importedComponents, component.get())) {
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

void ApplyGameObject(Scene& scene, GameObject& gameObject, const json& gameObjectJson, const std::string& instanceId, size_t& componentCount) {
	// instanceIdは参照の軸なので、空値や重複IDで既存Objectを壊さないようにしてから適用する。
	if (!instanceId.empty() && !IsInstanceIdUsedByOtherObject(scene, instanceId, &gameObject)) {
		gameObject.SetInstanceId(instanceId);
	}

	std::string prefabAssetPath = ReadString(gameObjectJson, "prefabAssetPath", "");
	std::string prefabObjectId = ReadString(gameObjectJson, "prefabObjectId", "");
	std::string prefabInstanceRootId = ReadString(gameObjectJson, "prefabInstanceRootId", "");
	bool prefabInstanceRoot = ReadBool(gameObjectJson, "prefabInstanceRoot", false);
	if (!prefabAssetPath.empty() && !prefabObjectId.empty()) {
		gameObject.SetPrefabLink(prefabAssetPath, prefabObjectId, prefabInstanceRootId, prefabInstanceRoot);
	} else {
		gameObject.ClearPrefabLink();
	}

	gameObject.SetName(ReadString(gameObjectJson, "name", gameObject.GetName()));
	gameObject.SetTag(ReadString(gameObjectJson, "tag", gameObject.GetTag()));
	gameObject.SetLayer(ReadUint32(gameObjectJson, "layer", gameObject.GetLayer()));
	gameObject.SetActive(ReadBool(gameObjectJson, "active", gameObject.IsActive()));
	componentCount += ApplyComponents(scene, gameObject, gameObjectJson);
}

void RemoveUnimportedGameObjects(Scene& scene, const std::vector<GameObject*>& importedObjects) {
	std::vector<std::unique_ptr<GameObject>>& gameObjects = scene.GetGameObjects();
	auto iterator = gameObjects.begin();
	while (iterator != gameObjects.end()) {
		GameObject* gameObject = iterator->get();
		if (!gameObject || ContainsPointer(importedObjects, gameObject)) {
			++iterator;
			continue;
		}

		gameObject->Finalize();
		iterator = gameObjects.erase(iterator);
	}
}

// Scene を IObjectResolver として見せるアダプタ。ドメイン(ObjectRef)は Scene の具象ではなく
// この抽象に依存する。instanceId の解決は Scene の既存APIへ委譲するだけ。
class SceneObjectResolver : public IObjectResolver {
public:
	explicit SceneObjectResolver(Scene& scene) : scene_(scene) {}

	GameObject* ResolveInstanceId(const std::string& instanceId) const override {
		if (instanceId.empty()) {
			return nullptr;
		}
		return scene_.FindGameObjectByInstanceId(instanceId);
	}

private:
	Scene& scene_;
};

// 全GameObject生成後に、各Componentの参照フィールド(instanceId)を実ポインタへ解決する。
// 親子リンクの復元(RestoreHierarchy)と同じく「全体が揃ってから結線する」二段構えの一部。
void ResolveObjectReferences(Scene& scene, const std::vector<GameObject*>& importedObjects) {
	SceneObjectResolver resolver(scene);
	for (GameObject* gameObject : importedObjects) {
		if (!gameObject) {
			continue;
		}
		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			if (component) {
				component->ResolveReferences(resolver);
			}
		}
	}
}

void RestoreHierarchy(const std::vector<GameObject*>& importedObjects, const std::vector<PendingParentLink>& parentLinks) {
	for (GameObject* gameObject : importedObjects) {
		if (gameObject) {
			gameObject->SetParent(nullptr);
		}
	}

	for (const PendingParentLink& link : parentLinks) {
		if (!link.gameObject) {
			continue;
		}

		GameObject* parent = FindImportedGameObjectByInstanceId(importedObjects, link.parentInstanceId);
		link.gameObject->SetParent(parent);
	}
}

} // namespace

SceneJsonImporter::ImportResult SceneJsonImporter::ImportScene(Scene& scene, const std::filesystem::path& projectRoot) {
	ImportResult result{};

	std::error_code error;
	std::filesystem::path normalizedProjectRoot = std::filesystem::weakly_canonical(projectRoot, error);
	if (error) {
		normalizedProjectRoot = projectRoot.lexically_normal();
	}

	std::string sceneName = SanitizeFileName(scene.GetSceneName());
	std::filesystem::path sceneDirectory = normalizedProjectRoot / "SceneJson" / sceneName;
	std::filesystem::path sceneJsonPath = sceneDirectory / (sceneName + ".scene.json");
	result.sourceDirectory = sceneDirectory;

	if (!std::filesystem::exists(sceneJsonPath)) {
		result.succeeded = true;
		result.imported = false;
		result.message = "Saved Scene JSON was not found.";
		return result;
	}

	json sceneJson;
	if (!LoadJsonFile(sceneJsonPath, sceneJson, result.message)) {
		result.imported = true;
		return result;
	}

	if (!sceneJson.contains("gameObjects") || !sceneJson.at("gameObjects").is_array()) {
		result.imported = true;
		result.message = "Scene JSON does not contain gameObjects array.";
		return result;
	}

	std::vector<GameObject*> importedObjects;
	std::vector<PendingParentLink> parentLinks;
	const json& gameObjectEntries = sceneJson.at("gameObjects");
	for (size_t objectIndex = 0; objectIndex < gameObjectEntries.size(); ++objectIndex) {
		const json& objectEntry = gameObjectEntries[objectIndex];
		if (!objectEntry.is_object()) {
			continue;
		}

		std::string objectName = ReadString(objectEntry, "name", "GameObject");
		std::string objectEntryInstanceId = ReadString(objectEntry, "instanceId", "");
		std::string assetPathText = ReadString(objectEntry, "assetPath", "");
		if (assetPathText.empty()) {
			continue;
		}

		std::filesystem::path gameObjectJsonPath = ResolveAssetPath(normalizedProjectRoot, assetPathText);
		json gameObjectJson;
		if (!LoadJsonFile(gameObjectJsonPath, gameObjectJson, result.message)) {
			result.imported = true;
			return result;
		}

		std::string savedObjectName = ReadString(gameObjectJson, "name", objectName);
		std::string savedInstanceId = ReadString(gameObjectJson, "instanceId", objectEntryInstanceId);
		std::string parentInstanceId = ReadString(gameObjectJson, "parentInstanceId", ReadString(objectEntry, "parentInstanceId", ""));
		GameObject* gameObject = FindExistingGameObject(scene, savedInstanceId, objectIndex, savedObjectName, importedObjects);
		if (!gameObject) {
			gameObject = scene.CreateGameObject(savedObjectName);
		}
		if (!gameObject) {
			continue;
		}

		ApplyGameObject(scene, *gameObject, gameObjectJson, savedInstanceId, result.componentCount);
		importedObjects.push_back(gameObject);
		parentLinks.push_back({gameObject, parentInstanceId});
		++result.gameObjectCount;
	}

	RemoveUnimportedGameObjects(scene, importedObjects);
	RestoreHierarchy(importedObjects, parentLinks);
	ResolveObjectReferences(scene, importedObjects);

	result.succeeded = true;
	result.imported = true;
	result.message = "Imported Scene JSON.";
	return result;
}

SceneJsonImporter::ImportResult SceneJsonImporter::ApplySceneJsonString(Scene& scene, const std::string& sceneJsonText) {
	ImportResult result{};
	result.imported = true;

	json sceneJson;
	try {
		sceneJson = json::parse(sceneJsonText);
	} catch (const json::exception& exception) {
		result.message = std::string("Failed to parse memory Scene JSON: ") + exception.what();
		return result;
	}

	const json* gameObjectEntries = nullptr;
	if (sceneJson.contains("objects") && sceneJson.at("objects").is_array()) {
		gameObjectEntries = &sceneJson.at("objects");
	} else if (sceneJson.contains("gameObjects") && sceneJson.at("gameObjects").is_array()) {
		gameObjectEntries = &sceneJson.at("gameObjects");
	}

	if (!gameObjectEntries) {
		result.message = "Scene JSON does not contain objects array.";
		return result;
	}

	std::vector<GameObject*> importedObjects;
	std::vector<PendingParentLink> parentLinks;
	for (size_t objectIndex = 0; objectIndex < gameObjectEntries->size(); ++objectIndex) {
		const json& gameObjectJson = gameObjectEntries->at(objectIndex);
		if (!gameObjectJson.is_object()) {
			continue;
		}

		std::string objectName = ReadString(gameObjectJson, "name", "GameObject");
		std::string instanceId = ReadString(gameObjectJson, "instanceId", "");
		std::string parentInstanceId = ReadString(gameObjectJson, "parentInstanceId", "");
		GameObject* gameObject = FindExistingGameObject(scene, instanceId, objectIndex, objectName, importedObjects);
		if (!gameObject) {
			gameObject = scene.CreateGameObject(objectName);
		}
		if (!gameObject) {
			continue;
		}

		ApplyGameObject(scene, *gameObject, gameObjectJson, instanceId, result.componentCount);
		importedObjects.push_back(gameObject);
		parentLinks.push_back({gameObject, parentInstanceId});
		++result.gameObjectCount;
	}

	RemoveUnimportedGameObjects(scene, importedObjects);
	RestoreHierarchy(importedObjects, parentLinks);
	ResolveObjectReferences(scene, importedObjects);

	result.succeeded = true;
	result.message = "Applied memory Scene JSON.";
	return result;
}

} // namespace KujataEngine

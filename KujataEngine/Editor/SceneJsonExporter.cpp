#include "SceneJsonExporter.h"
#include "../scene/Component.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>
#include <vector>

namespace KujataEngine {

namespace {

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
		return "GameObject";
	}

	return sanitized;
}

std::string MakeRelativeAssetPath(const std::filesystem::path& projectRoot, const std::filesystem::path& assetPath) {
	std::error_code error;
	std::filesystem::path relative = std::filesystem::relative(assetPath, projectRoot, error);
	if (error) {
		return assetPath.generic_string();
	}
	return relative.generic_string();
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text, std::string& message) {
	std::ofstream file(path, std::ios::binary);
	if (!file) {
		message = "Failed to open file: " + path.string();
		return false;
	}

	file << text;
	if (!file) {
		message = "Failed to write file: " + path.string();
		return false;
	}

	return true;
}

std::string BuildGameObjectJson(const GameObject& gameObject) {
	std::ostringstream os;
	os << "{\n";
	os << "  \"assetType\": \"GameObject\",\n";
	// 個別ファイルにもinstanceIdを持たせ、Scene側の参照情報が壊れてもObject単体から復元できるようにする。
	os << "  \"instanceId\": \"" << EscapeJsonString(gameObject.GetInstanceId()) << "\",\n";
	const GameObject* parent = gameObject.GetParent();
	std::string parentInstanceId;
	if (parent) {
		parentInstanceId = parent->GetInstanceId();
	}
	os << "  \"parentInstanceId\": \"" << EscapeJsonString(parentInstanceId) << "\",\n";
	os << "  \"prefabAssetPath\": \"" << EscapeJsonString(gameObject.GetPrefabAssetPath()) << "\",\n";
	os << "  \"prefabObjectId\": \"" << EscapeJsonString(gameObject.GetPrefabObjectId()) << "\",\n";
	os << "  \"prefabInstanceRootId\": \"" << EscapeJsonString(gameObject.GetPrefabInstanceRootId()) << "\",\n";
	os << "  \"prefabInstanceRoot\": ";
	if (gameObject.IsPrefabInstanceRoot()) {
		os << "true,\n";
	} else {
		os << "false,\n";
	}
	os << "  \"name\": \"" << EscapeJsonString(gameObject.GetName()) << "\",\n";
	os << "  \"tag\": \"" << EscapeJsonString(gameObject.GetTag()) << "\",\n";
	os << "  \"layer\": " << gameObject.GetLayer() << ",\n";
	os << "  \"active\": ";
	if (gameObject.IsActive()) {
		os << "true,\n";
	} else {
		os << "false,\n";
	}
	os << "  \"components\": [\n";

	const std::vector<std::unique_ptr<Component>>& components = gameObject.GetComponents();
	for (size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
		const std::unique_ptr<Component>& component = components[componentIndex];
		if (!component) {
			continue;
		}

		component->WriteJson(os, 4);
		if (componentIndex + 1 < components.size()) {
			os << ",";
		}
		os << "\n";
	}

	os << "  ]\n";
	os << "}\n";
	return os.str();
}

std::string BuildSceneJson(
    const Scene& scene,
    const std::filesystem::path& projectRoot,
    const std::vector<std::filesystem::path>& gameObjectPaths) {
	std::ostringstream os;
	os << "{\n";
	os << "  \"assetType\": \"Scene\",\n";
	os << "  \"name\": \"" << EscapeJsonString(scene.GetSceneName()) << "\",\n";
	os << "  \"gameObjects\": [\n";

	const std::vector<std::unique_ptr<GameObject>>& gameObjects = scene.GetGameObjects();
	for (size_t objectIndex = 0; objectIndex < gameObjects.size(); ++objectIndex) {
		const std::unique_ptr<GameObject>& gameObject = gameObjects[objectIndex];
		if (!gameObject) {
			continue;
		}

		os << "    {\n";
		// Scene側の一覧にもinstanceIdを置き、Import時は名前や並び順ではなくIDでObjectを探す。
		os << "      \"instanceId\": \"" << EscapeJsonString(gameObject->GetInstanceId()) << "\",\n";
		GameObject* parent = gameObject->GetParent();
		std::string parentInstanceId;
		if (parent) {
			parentInstanceId = parent->GetInstanceId();
		}
		os << "      \"parentInstanceId\": \"" << EscapeJsonString(parentInstanceId) << "\",\n";
		os << "      \"prefabAssetPath\": \"" << EscapeJsonString(gameObject->GetPrefabAssetPath()) << "\",\n";
		os << "      \"prefabObjectId\": \"" << EscapeJsonString(gameObject->GetPrefabObjectId()) << "\",\n";
		os << "      \"prefabInstanceRootId\": \"" << EscapeJsonString(gameObject->GetPrefabInstanceRootId()) << "\",\n";
		os << "      \"prefabInstanceRoot\": ";
		if (gameObject->IsPrefabInstanceRoot()) {
			os << "true,\n";
		} else {
			os << "false,\n";
		}
		os << "      \"name\": \"" << EscapeJsonString(gameObject->GetName()) << "\",\n";
		os << "      \"tag\": \"" << EscapeJsonString(gameObject->GetTag()) << "\",\n";
		os << "      \"layer\": " << gameObject->GetLayer() << ",\n";
		os << "      \"assetPath\": \"" << EscapeJsonString(MakeRelativeAssetPath(projectRoot, gameObjectPaths[objectIndex])) << "\"\n";
		os << "    }";
		if (objectIndex + 1 < gameObjects.size()) {
			os << ",";
		}
		os << "\n";
	}

	os << "  ]\n";
	os << "}\n";
	return os.str();
}

} // namespace

SceneJsonExporter::ExportResult SceneJsonExporter::ExportScene(const Scene& scene, const std::filesystem::path& projectRoot) {
	ExportResult result{};

	std::error_code error;
	if (projectRoot.empty()) {
		result.message = "ProjectDir is empty.";
		return result;
	}

	std::filesystem::path normalizedProjectRoot = std::filesystem::weakly_canonical(projectRoot, error);
	if (error) {
		normalizedProjectRoot = projectRoot.lexically_normal();
	}

	std::string sceneName = SanitizeFileName(scene.GetSceneName());
	std::filesystem::path sceneJsonRoot = normalizedProjectRoot / "SceneJson";
	std::filesystem::path sceneDirectory = sceneJsonRoot / sceneName;
	std::filesystem::path gameObjectDirectory = sceneDirectory / "GameObjects";

	std::filesystem::create_directories(gameObjectDirectory, error);
	if (error) {
		result.message = "Failed to create SceneJson directory: " + error.message();
		return result;
	}

	const std::vector<std::unique_ptr<GameObject>>& gameObjects = scene.GetGameObjects();
	std::vector<std::filesystem::path> gameObjectPaths;
	gameObjectPaths.resize(gameObjects.size());

	for (size_t objectIndex = 0; objectIndex < gameObjects.size(); ++objectIndex) {
		const std::unique_ptr<GameObject>& gameObject = gameObjects[objectIndex];
		if (!gameObject) {
			continue;
		}

		// GameObject名はEditor上で気軽に変更されるため、保存ファイル名には安定したinstanceIdを使う。
		std::string fileName = SanitizeFileName(gameObject->GetInstanceId()) + ".gameobject.json";

		std::filesystem::path gameObjectPath = gameObjectDirectory / fileName;
		gameObjectPaths[objectIndex] = gameObjectPath;

		if (!WriteTextFile(gameObjectPath, BuildGameObjectJson(*gameObject), result.message)) {
			return result;
		}

		++result.gameObjectFileCount;
	}

	std::filesystem::path sceneJsonPath = sceneDirectory / (sceneName + ".scene.json");
	if (!WriteTextFile(sceneJsonPath, BuildSceneJson(scene, normalizedProjectRoot, gameObjectPaths), result.message)) {
		return result;
	}

	result.succeeded = true;
	result.outputDirectory = sceneDirectory;
	result.sceneFileCount = 1;
	result.message = "Exported Scene JSON.";
	return result;
}

} // namespace KujataEngine

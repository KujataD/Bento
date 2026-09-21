// Scene::ToJson(シーンのJSON書き出し)。Play開始時の退避・Undo・アニメーションのプレビュー退避に使われる。
#include "Scene.h"
#include <sstream>

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

} // namespace

std::string Scene::ToJson() const {
	std::ostringstream os;
	os << "{\n";
	os << "  \"objects\": [\n";

	for (size_t objectIndex = 0; objectIndex < gameObjects_.size(); ++objectIndex) {
		const std::unique_ptr<GameObject>& gameObject = gameObjects_[objectIndex];
		if (!gameObject) {
			continue;
		}

		os << "    {\n";
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
		os << "      \"active\": ";
		if (gameObject->IsActive()) {
			os << "true,\n";
		} else {
			os << "false,\n";
		}
		os << "      \"components\": [\n";

		const std::vector<std::unique_ptr<Component>>& components = gameObject->GetComponents();
		for (size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
			const std::unique_ptr<Component>& component = components[componentIndex];
			if (!component) {
				continue;
			}

			component->WriteJson(os, 8);
			if (componentIndex + 1 < components.size()) {
				os << ",";
			}
			os << "\n";
		}

		os << "      ]\n";
		os << "    }";
		if (objectIndex + 1 < gameObjects_.size()) {
			os << ",";
		}
		os << "\n";
	}

	os << "  ]\n";
	os << "}\n";
	return os.str();
}

} // namespace KujataEngine

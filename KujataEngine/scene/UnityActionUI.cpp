#include "UnityActionUI.h"

#include "../runtime/InspectorUI.h"
#include "Component.h"
#include "GameObject.h"
#include "InvokableMethod.h"
#include <memory>
#include <string>
#include <vector>

namespace KujataEngine {
namespace {

std::string ReadString(const nlohmann::json& json, const char* key, const std::string& defaultValue) {
	if (!json.contains(key) || !json.at(key).is_string()) {
		return defaultValue;
	}
	return json.at(key).get<std::string>();
}

} // namespace

void DrawUnityActionInspector([[maybe_unused]] const char* label, [[maybe_unused]] const char* idPrefix, [[maybe_unused]] UnityAction& action) {
#ifdef USE_IMGUI
	InspectorUI::TextUnformatted(label);

	int removeIndex = -1;
	for (int i = 0; i < static_cast<int>(action.calls.size()); ++i) {
		PersistentCall& call = action.calls[i];
		const std::string suffix = std::string("##") + idPrefix + std::to_string(i);

		// 対象GameObject(HierarchyからD&D)。
		void* dropped = nullptr;
		bool cleared = false;
		const std::string targetName = GameObjectDisplayName(call.target.value);
		if (InspectorUI::ObjectField((std::string("Target") + suffix).c_str(), targetName.c_str(), &dropped, &cleared)) {
			if (cleared) {
				call.target.Clear();
				call.componentType.clear();
				call.methodName.clear();
			} else if (dropped) {
				call.target.Assign(static_cast<GameObject*>(dropped));
				call.componentType.clear();
				call.methodName.clear();
			}
		}

		if (call.target.value) {
			// 対象GameObjectが持つComponentの型名一覧をドロップダウンにする。
			std::vector<std::string> componentNames;
			for (const std::unique_ptr<Component>& component : call.target.value->GetComponents()) {
				if (component) {
					componentNames.push_back(component->GetTypeName());
				}
			}
			std::vector<const char*> componentItems;
			componentItems.reserve(componentNames.size());
			for (const std::string& name : componentNames) {
				componentItems.push_back(name.c_str());
			}
			int componentIndex = -1;
			for (int k = 0; k < static_cast<int>(componentNames.size()); ++k) {
				if (componentNames[k] == call.componentType) {
					componentIndex = k;
					break;
				}
			}
			if (InspectorUI::Combo((std::string("Component") + suffix).c_str(), &componentIndex, componentItems.data(), static_cast<int>(componentItems.size()))) {
				if (componentIndex >= 0 && componentIndex < static_cast<int>(componentNames.size())) {
					call.componentType = componentNames[componentIndex];
					call.methodName.clear();
				}
			}

			// 選択中Componentが公開するメソッド名一覧をドロップダウンにする。
			if (!call.componentType.empty()) {
				Component* targetComponent = nullptr;
				for (const std::unique_ptr<Component>& component : call.target.value->GetComponents()) {
					if (component && call.componentType == component->GetTypeName()) {
						targetComponent = component.get();
						break;
					}
				}

				std::vector<std::string> methodNames;
				if (targetComponent) {
					InvokableMethodRegistry registry;
					targetComponent->RegisterInvokableMethods(registry);
					for (const InvokableMethodRegistry::Entry& entry : registry.Entries()) {
						methodNames.push_back(entry.name);
					}
				}

				if (methodNames.empty()) {
					InspectorUI::TextDisabled("(no invokable methods)");
				} else {
					std::vector<const char*> methodItems;
					methodItems.reserve(methodNames.size());
					for (const std::string& name : methodNames) {
						methodItems.push_back(name.c_str());
					}
					int methodIndex = -1;
					for (int k = 0; k < static_cast<int>(methodNames.size()); ++k) {
						if (methodNames[k] == call.methodName) {
							methodIndex = k;
							break;
						}
					}
					if (InspectorUI::Combo((std::string("Method") + suffix).c_str(), &methodIndex, methodItems.data(), static_cast<int>(methodItems.size()))) {
						if (methodIndex >= 0 && methodIndex < static_cast<int>(methodNames.size())) {
							call.methodName = methodNames[methodIndex];
						}
					}
				}
			}
		}

		if (InspectorUI::Button((std::string("Remove") + suffix).c_str())) {
			removeIndex = i;
		}
	}

	if (removeIndex >= 0) {
		action.calls.erase(action.calls.begin() + removeIndex);
	}

	if (InspectorUI::Button((std::string("Add ") + label + "##add" + idPrefix).c_str())) {
		action.calls.emplace_back();
	}
#endif // USE_IMGUI
}

void WriteUnityActionJson(nlohmann::json& json, const char* key, const UnityAction& action) {
	nlohmann::json callsJson = nlohmann::json::array();
	for (const PersistentCall& call : action.calls) {
		nlohmann::json callJson;
		callJson["target"] = call.target.targetInstanceId;
		callJson["component"] = call.componentType;
		callJson["method"] = call.methodName;
		callsJson.push_back(std::move(callJson));
	}
	json[key] = nlohmann::json::object();
	json[key]["calls"] = std::move(callsJson);
}

void ReadUnityActionJson(const nlohmann::json& json, const char* key, UnityAction& action) {
	action.calls.clear();
	if (!json.contains(key) || !json.at(key).is_object()) {
		return;
	}
	const nlohmann::json& actionJson = json.at(key);
	if (!actionJson.contains("calls") || !actionJson.at("calls").is_array()) {
		return;
	}
	for (const nlohmann::json& callJson : actionJson.at("calls")) {
		if (!callJson.is_object()) {
			continue;
		}
		PersistentCall call;
		call.target.targetInstanceId = ReadString(callJson, "target", "");
		call.componentType = ReadString(callJson, "component", "");
		call.methodName = ReadString(callJson, "method", "");
		action.calls.push_back(std::move(call));
	}
}

} // namespace KujataEngine

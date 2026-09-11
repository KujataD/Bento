#include "UnityAction.h"

#include "GameObject.h"
#include "InvokableMethod.h"

namespace KujataEngine {

namespace {

Component* FindComponentByType(GameObject* gameObject, const std::string& typeName) {
	if (!gameObject || typeName.empty()) {
		return nullptr;
	}
	for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
		if (component && typeName == component->GetTypeName()) {
			return component.get();
		}
	}
	return nullptr;
}

} // namespace

void PersistentCall::Invoke() const {
	Component* component = FindComponentByType(target.value, componentType);
	if (!component) {
		return;
	}

	// 対象Componentが公開しているメソッド一覧を取り、名前一致で呼ぶ。
	InvokableMethodRegistry registry;
	component->RegisterInvokableMethods(registry);
	registry.Invoke(methodName);
}

void UnityAction::Invoke() const {
	for (const PersistentCall& call : calls) {
		call.Invoke();
	}
}

} // namespace KujataEngine

#include "ObjectRef.h"

#include "GameObject.h"

namespace KujataEngine {

std::string GameObjectDisplayName(const GameObject* gameObject) {
	if (!gameObject) {
		return std::string("None");
	}
	return gameObject->GetName();
}

std::string GameObjectInstanceId(const GameObject* gameObject) {
	if (!gameObject) {
		return std::string();
	}
	return gameObject->GetInstanceId();
}

GameObject* ResolveGameObject(const IObjectResolver& resolver, const std::string& instanceId) {
	if (instanceId.empty()) {
		return nullptr;
	}
	return resolver.ResolveInstanceId(instanceId);
}

} // namespace KujataEngine

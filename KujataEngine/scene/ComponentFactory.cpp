#include "ComponentFactory.h"
#include <algorithm>
#include <utility>

namespace KujataEngine {

ComponentFactory& ComponentFactory::GetInstance() {
	static ComponentFactory instance;
	return instance;
}

void ComponentFactory::Register(const std::string& typeName, CreateFunc createFunc) {
	Register(typeName, "Engine", std::move(createFunc));
}

void ComponentFactory::Register(const std::string& typeName, const std::string& moduleName, CreateFunc createFunc) {
	if (typeName.empty() || !createFunc) {
		return;
	}

	if (!entries_.contains(typeName)) {
		typeNames_.push_back(typeName);
	}

	Entry entry{};
	entry.createFunc = std::move(createFunc);
	entry.moduleName = moduleName;
	entries_[typeName] = std::move(entry);
}

void ComponentFactory::Unregister(const std::string& typeName) {
	entries_.erase(typeName);

	auto iterator = std::remove(typeNames_.begin(), typeNames_.end(), typeName);
	typeNames_.erase(iterator, typeNames_.end());
}

void ComponentFactory::UnregisterByModule(const std::string& moduleName) {
	if (moduleName.empty()) {
		return;
	}

	std::vector<std::string> unregisterTargets;
	for (const auto& [typeName, entry] : entries_) {
		if (entry.moduleName == moduleName) {
			unregisterTargets.push_back(typeName);
		}
	}

	for (const std::string& typeName : unregisterTargets) {
		Unregister(typeName);
	}
}

std::unique_ptr<Component> ComponentFactory::Create(const std::string& typeName) const {
	auto iterator = entries_.find(typeName);
	if (iterator == entries_.end()) {
		return nullptr;
	}

	return iterator->second.createFunc();
}

const std::vector<std::string>& ComponentFactory::GetRegisteredTypeNames() const {
	return typeNames_;
}

} // namespace KujataEngine

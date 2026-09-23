#include "SimCommand.h"

namespace KujataEngine {

SimCommandFactory& SimCommandFactory::GetInstance() {
	static SimCommandFactory instance;
	return instance;
}

void SimCommandFactory::Register(const std::string& typeName, CreateFunc createFunc) {
	if (typeName.empty() || !createFunc) {
		return;
	}
	creators_[typeName] = std::move(createFunc);
}

std::unique_ptr<SimCommand> SimCommandFactory::Create(const std::string& typeName) const {
	const auto found = creators_.find(typeName);
	return (found != creators_.end()) ? found->second() : nullptr;
}

bool SimCommandFactory::IsRegistered(const std::string& typeName) const { return creators_.find(typeName) != creators_.end(); }

} // namespace KujataEngine

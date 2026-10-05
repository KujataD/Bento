#include "InputActionSystem.h"

#include "../base/Logger.h"
#include <algorithm>
#include <memory>
#include <cmath>

namespace KujataEngine {

namespace {

int32_t Quantize(float value) {
	const float clamped = std::clamp(value, -1.0f, 1.0f);
	return static_cast<int32_t>(std::lround(clamped * static_cast<float>(kActionAxisMax)));
}

} // namespace

InputActionSystem* InputActionSystem::GetInstance() {
	static InputActionSystem instance;
	// 標準のコマンドを1回だけ登録する(リプレイから型名で作り直せるようにするため)。
	static const bool registered = [] {
		SimCommandFactory::GetInstance().Register("Action", [] { return std::unique_ptr<SimCommand>(new ActionCommand()); });
		return true;
	}();
	(void)registered;
	return &instance;
}

void InputActionSystem::EnsureLoaded() {
	if (!loaded_) {
		Reload();
	}
}

bool InputActionSystem::Reload(std::string* outMessage) {
	std::string message;
	std::vector<InputActionDef> actions;
	const bool succeeded = InputActionAsset::Load(InputActionAsset::GetDefaultPath(), actions, message);
	if (succeeded) {
		actions_ = std::move(actions);
		lastDeviceValues_.clear();
	} else {
		Logger::Log("[InputAction] " + message);
	}
	sourceDescription_ = message;
	loaded_ = true;
	if (outMessage) {
		*outMessage = message;
	}
	return succeeded;
}

const std::vector<InputActionDef>& InputActionSystem::GetDefinitions() {
	EnsureLoaded();
	return actions_;
}

bool InputActionSystem::SaveDefinitions(const std::vector<InputActionDef>& actions, std::string* outMessage) {
	std::string message;
	const bool succeeded = InputActionAsset::Save(InputActionAsset::GetDefaultPath(), actions, message);
	if (succeeded) {
		actions_ = actions;
		lastDeviceValues_.clear();
		sourceDescription_ = message;
		loaded_ = true;
	} else {
		Logger::Log("[InputAction] " + message);
	}
	if (outMessage) {
		*outMessage = message;
	}
	return succeeded;
}

ActionState& InputActionSystem::GetActions() { return actions0_; }

CommandQueue& InputActionSystem::GetQueue() { return queue_; }

void InputActionSystem::PushAction(const std::string& name, float x, float y, uint32_t source) {
	queue_.Push(source, std::make_unique<ActionCommand>(name, Quantize(x), Quantize(y)));
}

void InputActionSystem::CollectFromDevices() {
	EnsureLoaded();
	if (!deviceInputEnabled_) {
		return;
	}

	for (const InputActionDef& action : actions_) {
		int32_t x = 0;
		int32_t y = 0;
		InputActionAsset::ReadAction(action, x, y);

		// 押しっぱなしの間は何も積まない(変わったときだけコマンドにする。記録が小さくなる)。
		auto& last = lastDeviceValues_[action.name];
		if (last.first == x && last.second == y) {
			continue;
		}
		last = {x, y};
		queue_.Push(kCommandSourceLocalPlayer, std::make_unique<ActionCommand>(action.name, x, y));
	}
}

void InputActionSystem::Step(Scene* scene) {
	// 「前の更新の値」を写してから、積まれたコマンドを順に実行する。
	// これで Pressed / Released が、誰が押したか(キー・AI・CUI)に関係なく同じ形で決まる。
	actions0_.BeginStep();
	SimContext context{};
	context.scene = scene;
	context.actions = &actions0_;
	queue_.ExecuteAll(context);
}

void InputActionSystem::Reset() {
	actions0_.Clear();
	queue_.Clear();
	lastDeviceValues_.clear();
}

void InputActionSystem::SetDeviceInputEnabled(bool enabled) {
	deviceInputEnabled_ = enabled;
	if (!enabled) {
		lastDeviceValues_.clear();
	}
}

bool InputActionSystem::IsDeviceInputEnabled() const { return deviceInputEnabled_; }

const std::string& InputActionSystem::GetSourceDescription() {
	EnsureLoaded();
	return sourceDescription_;
}

} // namespace KujataEngine

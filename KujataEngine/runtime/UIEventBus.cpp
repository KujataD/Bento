#include "UIEventBus.h"

#include <unordered_map>
#include <vector>

namespace KujataEngine {
namespace {
std::unordered_map<std::string, std::vector<std::function<void()>>>& Subscribers() {
	static std::unordered_map<std::string, std::vector<std::function<void()>>> subscribers;
	return subscribers;
}
} // namespace

void UIEventBus::Subscribe(const std::string& eventName, std::function<void()> callback) {
	if (eventName.empty() || !callback) {
		return;
	}
	Subscribers()[eventName].push_back(std::move(callback));
}

void UIEventBus::Publish(const std::string& eventName) {
	if (eventName.empty()) {
		return;
	}
	auto found = Subscribers().find(eventName);
	if (found == Subscribers().end()) {
		return;
	}
	// コピーして回す(コールバック内で購読変更されても安全に)。
	std::vector<std::function<void()>> callbacks = found->second;
	for (const std::function<void()>& callback : callbacks) {
		if (callback) {
			callback();
		}
	}
}

void UIEventBus::Clear() { Subscribers().clear(); }

} // namespace KujataEngine

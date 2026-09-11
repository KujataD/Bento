#include "AnimationRecordingState.h"

#include <utility>

namespace KujataEngine {

namespace {

bool g_recording = false;
bool g_keyContextEnabled = false;
std::string g_componentContext;
bool g_hasComponentContext = false;
std::vector<AnimationRecordedChange> g_changes;

} // namespace

bool IsAnimationRecording() {
	return g_recording;
}

void SetAnimationRecording(bool recording) {
	g_recording = recording;
	if (!recording) {
		g_changes.clear();
	}
}

bool IsAnimationKeyContextEnabled() {
	return g_keyContextEnabled;
}

void SetAnimationKeyContextEnabled(bool enabled) {
	g_keyContextEnabled = enabled;
}

void SetAnimationRecordingComponentContext(const char* componentTypeName) {
	if (componentTypeName) {
		g_componentContext = componentTypeName;
		g_hasComponentContext = true;
	} else {
		g_componentContext.clear();
		g_hasComponentContext = false;
	}
}

const char* GetAnimationRecordingComponentContext() {
	return g_hasComponentContext ? g_componentContext.c_str() : nullptr;
}

void ReportAnimationChannelChange(const std::string& channelPath, float value) {
	if (!g_hasComponentContext) {
		return;
	}
	g_changes.push_back({g_componentContext + "/" + channelPath, value});
}

std::vector<AnimationRecordedChange> ConsumeAnimationRecordedChanges() {
	return std::exchange(g_changes, {});
}

} // namespace KujataEngine

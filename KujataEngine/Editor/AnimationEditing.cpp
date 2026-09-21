#include "AnimationEditing.h"

#include "AssetDatabase.h"
#include "../assets/AnimationClipAsset.h"
#include "../base/ProjectPath.h"
#include "../components/AnimatorComponent.h"
#include "../scene/GameObject.h"
#include <algorithm>
#include <cmath>
#include <system_error>

namespace KujataEngine {

namespace AnimationEditing {

bool CreateClip(AnimatorComponent& animator, const std::string& clipName, std::filesystem::path& outPath, std::string& message) {
	const std::string name = clipName.empty() ? "NewAnimation" : clipName;
	std::filesystem::path animationsDirectory = GetProjectDataRoot() / "Animations";
	std::error_code errorCode;
	std::filesystem::create_directories(animationsDirectory, errorCode);

	outPath = animationsDirectory / (name + ".anim.json");
	if (std::filesystem::exists(outPath, errorCode)) {
		message = "同じ名前のクリップがあります: " + outPath.generic_string();
		return false;
	}
	if (!AnimationClipAsset::CreateDefaultFile(outPath, message)) {
		return false;
	}
	AssetDatabase::GetInstance().GetOrCreateAssetId(outPath);
	animator.SetClipPath(outPath.generic_string());
	message = "Created: " + outPath.filename().generic_string();
	return true;
}

int AddKey(AnimatorComponent& animator, const std::string& trackPath, float time, const float* currentValue) {
	AnimationTrack& track = animator.GetClip().GetOrAddTrack(trackPath);

	AnimationKeyframe key;
	key.time = (std::max)(time, 0.0f);
	if (currentValue) {
		key.value = *currentValue;
		if (track.additive) {
			// 加算トラックのキー値は絶対値ではなく「基準値からの差分」で記録する。
			// (絶対値で記録すると、Rec開始地点が原点以外の場合にその座標が二重加算される)
			float baseValue = 0.0f;
			if (animator.TryGetAdditiveBase(trackPath, baseValue)) {
				key.value = *currentValue - baseValue;
			} else {
				// 基準未キャプチャ(プレビュー外)では差分が定まらないため、カーブの現在値を維持する。
				key.value = track.curve.Evaluate(key.time);
			}
		}
	} else {
		key.value = track.curve.Evaluate(key.time);
	}

	int keyIndex = track.curve.AddKey(key);
	// 追加キーと前後キーのタンジェントを滑らかに整える。
	AnimationClipAsset::SetSmoothTangents(track.curve, keyIndex - 1);
	AnimationClipAsset::SetSmoothTangents(track.curve, keyIndex);
	AnimationClipAsset::SetSmoothTangents(track.curve, keyIndex + 1);
	return keyIndex;
}

bool RemoveKey(AnimatorComponent& animator, const std::string& trackPath, float time, float tolerance) {
	AnimationTrack* track = animator.GetClip().FindTrack(trackPath);
	if (!track) {
		return false;
	}
	std::vector<AnimationKeyframe>& keys = track->curve.keys;
	auto found = std::find_if(keys.begin(), keys.end(), [&](const AnimationKeyframe& key) { return std::abs(key.time - time) <= tolerance; });
	if (found == keys.end()) {
		return false;
	}
	const int removedIndex = static_cast<int>(found - keys.begin());
	keys.erase(found);
	// 消した位置の前後のキーの接線を整え直す。
	AnimationClipAsset::SetSmoothTangents(track->curve, removedIndex - 1);
	AnimationClipAsset::SetSmoothTangents(track->curve, removedIndex);
	return true;
}

std::vector<std::string> ListChannels(AnimatorComponent& animator) {
	std::vector<std::string> paths;
	GameObject* owner = animator.GetOwner();
	if (!owner) {
		return paths;
	}
	std::vector<AnimatorChannel> channels;
	AnimatorComponent::CollectHierarchyChannels(*owner, &animator, channels);
	for (const AnimatorChannel& channel : channels) {
		paths.push_back(channel.path);
	}
	return paths;
}

float* FindChannelValue(AnimatorComponent& animator, const std::string& trackPath) {
	GameObject* owner = animator.GetOwner();
	if (!owner) {
		return nullptr;
	}
	std::vector<AnimatorChannel> channels;
	AnimatorComponent::CollectHierarchyChannels(*owner, &animator, channels);
	for (const AnimatorChannel& channel : channels) {
		if (channel.path == trackPath) {
			return channel.value;
		}
	}
	return nullptr;
}

} // namespace AnimationEditing

} // namespace KujataEngine

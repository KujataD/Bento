#include "AudioSourceComponent.h"

#include "../runtime/AssetResolver.h"
#include "../runtime/InspectorUI.h"
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace KujataEngine {

namespace {

std::string ReadString(const nlohmann::json& json, const char* key, const std::string& defaultValue) {
	if (!json.contains(key) || !json.at(key).is_string()) {
		return defaultValue;
	}
	return json.at(key).get<std::string>();
}

} // namespace

AudioSourceComponent::~AudioSourceComponent() { Stop(); }

void AudioSourceComponent::SyncPathBuffer() {
	std::memset(pathBuffer_.data(), 0, pathBuffer_.size());
	strncpy_s(pathBuffer_.data(), pathBuffer_.size(), audioPath_.c_str(), _TRUNCATE);
}

void AudioSourceComponent::SetAudio(const std::string& assetId, const std::string& path) {
	audioAssetId_ = assetId;
	audioPath_ = path;

	// パスだけの参照はassetIdを補完し、リネーム/移動に追従できる参照へ揃える。
	// Editor外(FallbackAssetResolver)ではIDが取れないため、その場合はパスをそのまま保持する。
	if (audioAssetId_.empty() && !audioPath_.empty()) {
		IAssetResolver& assetDatabase = GetAssetResolver();
		std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath("", audioPath_);
		audioAssetId_ = assetDatabase.GetOrCreateAssetId(resolvedPath);
		if (!audioAssetId_.empty()) {
			audioPath_ = assetDatabase.MakeProjectRelativePath(resolvedPath);
		}
	}

	SyncPathBuffer();
}

void AudioSourceComponent::SetVolume(float volume) {
	volume_ = (std::max)(volume, 0.0f);
	if (voiceHandle_ != AudioManager::kInvalidHandle) {
		AudioManager::GetInstance()->SetVolume(voiceHandle_, volume_);
	}
}

bool AudioSourceComponent::EnsureLoaded() {
	if (audioAssetId_.empty() && audioPath_.empty()) {
		return false;
	}

	// assetId優先で現在のパスへ解決する(移動済みアセットは.meta経由で新パスが返る)。
	std::string resolvedPath = GetAssetResolver().ResolveAssetPath(audioAssetId_, audioPath_).string();
	if (resolvedPath.empty()) {
		return false;
	}

	if (soundHandle_ != AudioManager::kInvalidHandle && loadedPath_ == resolvedPath) {
		return true;
	}

	soundHandle_ = AudioManager::GetInstance()->LoadAudio(resolvedPath);
	loadedPath_ = resolvedPath;
	return soundHandle_ != AudioManager::kInvalidHandle;
}

void AudioSourceComponent::Play() {
	if (!EnsureLoaded()) {
		return;
	}

	// 多重再生を防ぐため、このComponentが持つ既存ボイスは止めてから再生し直す。
	Stop();
	voiceHandle_ = AudioManager::GetInstance()->Play(soundHandle_, volume_, loop_);
}

void AudioSourceComponent::Stop() {
	if (voiceHandle_ == AudioManager::kInvalidHandle) {
		return;
	}
	AudioManager::GetInstance()->Stop(voiceHandle_);
	voiceHandle_ = AudioManager::kInvalidHandle;
}

bool AudioSourceComponent::IsPlaying() const {
	if (voiceHandle_ == AudioManager::kInvalidHandle) {
		return false;
	}
	return AudioManager::GetInstance()->IsPlaying(voiceHandle_);
}

void AudioSourceComponent::OnPlayStart() {
	if (playOnStart_) {
		Play();
	}
}

void AudioSourceComponent::OnPlayStop() {
	Stop();
}

void AudioSourceComponent::DrawInspector() {
#ifdef USE_IMGUI
	if (InspectorUI::InputText("Audio (.wav)", pathBuffer_.data(), pathBuffer_.size())) {
		// 存在するパスならassetIdを補完する(入力途中の不完全なパスはID無しのまま)。
		SetAudio("", pathBuffer_.data());
		// 参照が変わったので、次のPlayで読み込み直す。
		soundHandle_ = AudioManager::kInvalidHandle;
		loadedPath_.clear();
	}

	if (InspectorUI::DragFloat("Volume", &volume_, 0.01f, 0.0f, 1.0f)) {
		SetVolume(volume_);
	}

	InspectorUI::Checkbox("Loop", &loop_);
	InspectorUI::Checkbox("Play On Start", &playOnStart_);

	// Edit中の試聴用。Play modeに入らなくても音量やループを確認できる。
	if (IsPlaying()) {
		if (InspectorUI::Button("Stop")) {
			Stop();
		}
	} else {
		if (InspectorUI::Button("Play (Preview)")) {
			Play();
		}
	}
#endif // USE_IMGUI
}

void AudioSourceComponent::WriteJson(nlohmann::json& json) const {
	json["audioAssetId"] = audioAssetId_;
	json["audioPath"] = audioPath_;
	json["volume"] = volume_;
	json["loop"] = loop_;
	json["playOnStart"] = playOnStart_;
}

void AudioSourceComponent::ReadJson(const nlohmann::json& json) {
	// SetAudio経由にすることで、ID未付与の旧データはここで補完される(次回保存時に永続化)。
	SetAudio(ReadString(json, "audioAssetId", audioAssetId_), ReadString(json, "audioPath", audioPath_));

	if (json.contains("volume") && json.at("volume").is_number()) {
		volume_ = json.at("volume").get<float>();
	}
	if (json.contains("loop") && json.at("loop").is_boolean()) {
		loop_ = json.at("loop").get<bool>();
	}
	if (json.contains("playOnStart") && json.at("playOnStart").is_boolean()) {
		playOnStart_ = json.at("playOnStart").get<bool>();
	}
}

} // namespace KujataEngine

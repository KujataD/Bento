#pragma once

#include "../base/AudioManager.h"
#include "../scene/Component.h"
#include <array>
#include <string>

namespace KujataEngine {

/// <summary>
/// WAVサウンドを再生するComponent
/// </summary>
class KUJATA_API AudioSourceComponent : public Component {
public:
	const char* GetTypeName() const override { return "AudioSourceComponent"; }

	/// <summary>
	/// 再生するWAVを設定
	/// </summary>
	void SetAudio(const std::string& assetId, const std::string& path);

	void SetAudioPath(const std::string& path) { SetAudio("", path); }

	/// <summary>音量(0.0〜1.0)。再生中なら即時反映。</summary>
	void SetVolume(float volume);

	float GetVolume() const { return volume_; }

	void SetLoop(bool loop) { loop_ = loop; }

	void Play();

	void Stop();

	bool IsPlaying() const;

	/// <summary>Play開始時: Play On Startなら再生を始める。</summary>
	void OnPlayStart() override;

	/// <summary>Play終了時: 再生中のボイスを止める。</summary>
	void OnPlayStop() override;

	void DrawInspector() override;

	void WriteJson(nlohmann::json& json) const override;

	void ReadJson(const nlohmann::json& json) override;

	~AudioSourceComponent() override;

private:
	/// <summary>assetId優先で現在のパスへ解決し、AudioManagerへ読み込む。</summary>
	bool EnsureLoaded();
	void SyncPathBuffer();

	std::string audioAssetId_;
	std::string audioPath_;
	float volume_ = 1.0f;
	bool loop_ = false;
	// Play開始時に自動再生する(Unity AudioSourceのPlay On Awake相当)。
	bool playOnStart_ = true;

	// 実行時状態(非シリアライズ)
	uint32_t soundHandle_ = AudioManager::kInvalidHandle;
	std::string loadedPath_;
	uint32_t voiceHandle_ = AudioManager::kInvalidHandle;
	std::array<char, 256> pathBuffer_{};
};

} // namespace KujataEngine

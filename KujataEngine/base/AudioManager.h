#pragma once

#include "../runtime/KujataApi.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>
#include <xaudio2.h>

namespace KujataEngine {

/// <summary>
/// XAudio2によるサウンド再生基盤(TextureManagerと同じシングルトン構成)。
/// サウンド(読み込み済みデータ)とボイス(再生中インスタンス)を別ハンドルで管理する。
///
/// 対応フォーマット:
///   - `.wav` … 自前で読む(PCM / IEEE float / WAVE_FORMAT_EXTENSIBLE)
///   - `.mp3` などの圧縮音声 … **Media Foundationで丸ごとPCMへ展開してから**XAudio2へ渡す
///
/// **XAudio2は圧縮データをそのまま鳴らせない**ので、読み込み時に全部展開してメモリへ置く。
/// 数分のBGMでも数十MB程度なので、ストリーミングは用意していない。
/// </summary>
class KUJATA_API AudioManager {
public:
	static constexpr uint32_t kInvalidHandle = 0xFFFFFFFFu;

	static AudioManager* GetInstance();

	/// <summary>XAudio2エンジンとマスターボイスを生成する。失敗時は以後の全操作が安全に無効化される。</summary>
	void Initialize();

	/// <summary>全ボイスとXAudio2エンジンを破棄する。Scene(AudioSourceComponent)破棄後に呼ぶこと。</summary>
	void Finalize();

	/// <summary>
	/// 音声ファイルを読み込みサウンドハンドルを返す(同一パスは使い回す)。失敗時はkInvalidHandle。
	/// **拡張子で読み方を選ぶ**: `.wav` は自前、それ以外はMedia Foundationで展開する。
	/// </summary>
	uint32_t LoadAudio(const std::string& filePath);

	/// <summary>
	/// WAVファイルを読み込みサウンドハンドルを返す(同一パスは使い回す)。失敗時はkInvalidHandle。
	/// **拡張子を問わず読みたいときは LoadAudio を使うこと。** こちらは常にRIFF/WAVEとして解釈する。
	/// </summary>
	uint32_t LoadWav(const std::string& filePath);

	/// <summary>
	/// サウンドを再生し、ボイスハンドルを返す(失敗時はkInvalidHandle)。
	/// 同じサウンドを複数同時再生できる(ボイスは再生ごとに生成)。
	/// </summary>
	uint32_t Play(uint32_t soundHandle, float volume, bool loop);

	/// <summary>再生を停止しボイスを破棄する。無効ハンドルは無視。</summary>
	void Stop(uint32_t voiceHandle);

	/// <summary>再生中ボイスの音量を変更する(0.0〜)。無効ハンドルは無視。</summary>
	void SetVolume(uint32_t voiceHandle, float volume);

	bool IsPlaying(uint32_t voiceHandle);

private:
	AudioManager() = default;
	~AudioManager() = default;
	AudioManager(const AudioManager&) = delete;
	AudioManager& operator=(const AudioManager&) = delete;

	struct SoundData {
		// WAVEFORMATEX(可変長のcbSize拡張含む)の生バイト列。先頭をWAVEFORMATEX*として解釈する。
		std::vector<uint8_t> formatBytes;
		std::vector<uint8_t> pcmBytes;
	};

	/// <summary>再生し終わったボイスを破棄してマップから取り除く(Play/IsPlaying時に遅延実行)。</summary>
	void CollectFinishedVoices();

	/// <summary>
	/// 圧縮音声(mp3等)をMedia FoundationでPCMへ展開して読み込む。
	/// **最初の音声ストリームだけ**を使い、16bit PCMへ落として全サンプルを連結する。
	/// </summary>
	uint32_t LoadCompressed(const std::string& filePath);

	/// <summary>Media Foundationを一度だけ起動する。失敗したら以後の圧縮音声の読み込みを諦める。</summary>
	bool EnsureMediaFoundation();

	Microsoft::WRL::ComPtr<IXAudio2> xaudio2_;
	IXAudio2MasteringVoice* masteringVoice_ = nullptr;
	bool initialized_ = false;

	std::vector<SoundData> sounds_;
	std::unordered_map<std::string, uint32_t> soundHandleByPath_;
	std::unordered_map<uint32_t, IXAudio2SourceVoice*> activeVoices_;
	uint32_t nextVoiceHandle_ = 0;
	// Media Foundationを起動済みか。**Finalizeで必ず落とす**(起動しっぱなしにしない)。
	bool mediaFoundationReady_ = false;
	// 一度失敗したら二度と試さない(壊れた環境で毎回数百msかけない)。
	bool mediaFoundationFailed_ = false;
};

} // namespace KujataEngine

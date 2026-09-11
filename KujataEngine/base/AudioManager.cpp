#include "AudioManager.h"
#include "Logger.h"
#include <cstring>
#include <filesystem>
#include <fstream>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "xaudio2.lib")
// 圧縮音声(mp3等)の展開に使う。Windows標準なので追加の再頒布物は要らない。
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace KujataEngine {

namespace {

struct ChunkHeader {
	char id[4];
	uint32_t size;
};

bool ChunkIdEquals(const ChunkHeader& header, const char* id) { return std::memcmp(header.id, id, 4) == 0; }

/// <summary>拡張子を小文字で返す(先頭のドットを含む)。無ければ空。</summary>
std::string LowerExtension(const std::string& filePath) {
	std::string extension = std::filesystem::path(filePath).extension().string();
	for (char& character : extension) {
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}
	return extension;
}

} // namespace

AudioManager* AudioManager::GetInstance() {
	static AudioManager instance;
	return &instance;
}

void AudioManager::Initialize() {
	if (initialized_) {
		return;
	}

	HRESULT result = XAudio2Create(&xaudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED(result)) {
		Logger::Log("[Audio] XAudio2Create failed. hr=" + std::to_string(result));
		xaudio2_.Reset();
		return;
	}

	result = xaudio2_->CreateMasteringVoice(&masteringVoice_);
	if (FAILED(result)) {
		// マスターボイス生成失敗は再生デバイスが無い環境でも起きる。エンジン全体は止めず、音だけ無効化する。
		Logger::Log("[Audio] CreateMasteringVoice failed (no audio device?). hr=" + std::to_string(result));
		masteringVoice_ = nullptr;
		xaudio2_.Reset();
		return;
	}

	initialized_ = true;
	Logger::Log("[Audio] AudioManager initialized.");
}

void AudioManager::Finalize() {
	for (auto& [handle, voice] : activeVoices_) {
		if (voice) {
			voice->Stop();
			voice->DestroyVoice();
		}
	}
	activeVoices_.clear();

	if (masteringVoice_) {
		masteringVoice_->DestroyVoice();
		masteringVoice_ = nullptr;
	}
	xaudio2_.Reset();
	initialized_ = false;

	// **起動したら必ず落とす。** MFStartup/MFShutdownは対で呼ぶ約束になっている。
	if (mediaFoundationReady_) {
		MFShutdown();
		mediaFoundationReady_ = false;
	}
}

bool AudioManager::EnsureMediaFoundation() {
	if (mediaFoundationReady_) {
		return true;
	}
	if (mediaFoundationFailed_) {
		return false; // 一度失敗した環境では二度と試さない。
	}
	const HRESULT result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	if (FAILED(result)) {
		Logger::Log("[Audio] MFStartup failed. hr=" + std::to_string(result));
		mediaFoundationFailed_ = true;
		return false;
	}
	mediaFoundationReady_ = true;
	return true;
}

uint32_t AudioManager::LoadAudio(const std::string& filePath) {
	// **拡張子で読み方を選ぶ。** WAVは自前で読んだほうが速く、依存も増えない。
	if (LowerExtension(filePath) == ".wav") {
		return LoadWav(filePath);
	}
	return LoadCompressed(filePath);
}

uint32_t AudioManager::LoadCompressed(const std::string& filePath) {
	auto found = soundHandleByPath_.find(filePath);
	if (found != soundHandleByPath_.end()) {
		return found->second;
	}
	if (!EnsureMediaFoundation()) {
		return kInvalidHandle;
	}

	// **必ずpathを経由してワイド化する。** 素朴なchar→wchar_tの引き伸ばしだと、
	// 日本語を含むパスで開けなくなる(プロジェクトルートに日本語が入る構成がある)。
	const std::wstring widePath = std::filesystem::path(filePath).wstring();

	Microsoft::WRL::ComPtr<IMFSourceReader> reader;
	HRESULT result = MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, reader.GetAddressOf());
	if (FAILED(result) || !reader) {
		Logger::Log("[Audio] Failed to open audio: " + filePath + " hr=" + std::to_string(result));
		return kInvalidHandle;
	}

	// 音声の第1ストリームだけを使う(映像や字幕が混ざっていても無視する)。
	reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
	reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);

	// 「PCMで寄こせ」とだけ指定する。ビット深度やサンプリングレートはMFに決めさせ、
	// 決まった形式をあとから引き取ってWAVEFORMATEXへ写す。
	Microsoft::WRL::ComPtr<IMFMediaType> requested;
	if (FAILED(MFCreateMediaType(requested.GetAddressOf()))) {
		return kInvalidHandle;
	}
	requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	result = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, requested.Get());
	if (FAILED(result)) {
		Logger::Log("[Audio] PCM decode not available: " + filePath + " hr=" + std::to_string(result));
		return kInvalidHandle;
	}

	Microsoft::WRL::ComPtr<IMFMediaType> actual;
	if (FAILED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), actual.GetAddressOf()))) {
		return kInvalidHandle;
	}
	WAVEFORMATEX* format = nullptr;
	UINT32 formatSize = 0;
	if (FAILED(MFCreateWaveFormatExFromMFMediaType(actual.Get(), &format, &formatSize)) || !format) {
		return kInvalidHandle;
	}

	SoundData sound{};
	sound.formatBytes.resize((std::max)(static_cast<size_t>(formatSize), sizeof(WAVEFORMATEX)), 0);
	std::memcpy(sound.formatBytes.data(), format, formatSize);
	CoTaskMemFree(format);

	// 最後まで読み切って連結する。XAudio2は圧縮のままでは鳴らせないので、丸ごと展開する。
	for (;;) {
		DWORD flags = 0;
		Microsoft::WRL::ComPtr<IMFSample> sample;
		result = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr,
		    sample.GetAddressOf());
		if (FAILED(result)) {
			Logger::Log("[Audio] ReadSample failed: " + filePath + " hr=" + std::to_string(result));
			return kInvalidHandle;
		}
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
			break;
		}
		if (!sample) {
			continue; // ギャップ。データは無いが終端でもない。
		}

		Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
		if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf())) || !buffer) {
			continue;
		}
		BYTE* data = nullptr;
		DWORD length = 0;
		if (FAILED(buffer->Lock(&data, nullptr, &length))) {
			continue;
		}
		sound.pcmBytes.insert(sound.pcmBytes.end(), data, data + length);
		buffer->Unlock();
	}

	if (sound.pcmBytes.empty()) {
		Logger::Log("[Audio] Decoded no samples: " + filePath);
		return kInvalidHandle;
	}

	const uint32_t handle = static_cast<uint32_t>(sounds_.size());
	sounds_.push_back(std::move(sound));
	soundHandleByPath_[filePath] = handle;
	Logger::Log("[Audio] Decoded: " + filePath);
	return handle;
}

uint32_t AudioManager::LoadWav(const std::string& filePath) {
	auto found = soundHandleByPath_.find(filePath);
	if (found != soundHandleByPath_.end()) {
		return found->second;
	}

	std::ifstream file(filePath, std::ios::binary);
	if (!file) {
		Logger::Log("[Audio] Failed to open WAV: " + filePath);
		return kInvalidHandle;
	}

	// RIFFヘッダ("RIFF" + 全体サイズ + "WAVE")を確認する。
	ChunkHeader riff{};
	char waveId[4]{};
	file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
	file.read(waveId, sizeof(waveId));
	if (!file || !ChunkIdEquals(riff, "RIFF") || std::memcmp(waveId, "WAVE", 4) != 0) {
		Logger::Log("[Audio] Not a RIFF/WAVE file: " + filePath);
		return kInvalidHandle;
	}

	// fmt/data以外のチャンク(JUNK, LIST等)はスキップしながら両方を集める。
	SoundData sound{};
	ChunkHeader chunk{};
	while (file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk))) {
		if (ChunkIdEquals(chunk, "fmt ")) {
			sound.formatBytes.resize((std::max)(static_cast<size_t>(chunk.size), sizeof(WAVEFORMATEX)), 0);
			file.read(reinterpret_cast<char*>(sound.formatBytes.data()), chunk.size);
		} else if (ChunkIdEquals(chunk, "data")) {
			sound.pcmBytes.resize(chunk.size);
			file.read(reinterpret_cast<char*>(sound.pcmBytes.data()), chunk.size);
		} else {
			file.seekg(chunk.size, std::ios::cur);
		}
		// チャンクは2バイト境界に整列される(奇数サイズ時は1バイトのパディング)。
		if (chunk.size % 2 == 1) {
			file.seekg(1, std::ios::cur);
		}
	}

	if (sound.formatBytes.empty() || sound.pcmBytes.empty()) {
		Logger::Log("[Audio] fmt/data chunk not found: " + filePath);
		return kInvalidHandle;
	}

	uint32_t handle = static_cast<uint32_t>(sounds_.size());
	sounds_.push_back(std::move(sound));
	soundHandleByPath_[filePath] = handle;
	Logger::Log("[Audio] Loaded WAV: " + filePath);
	return handle;
}

uint32_t AudioManager::Play(uint32_t soundHandle, float volume, bool loop) {
	if (!initialized_ || soundHandle >= sounds_.size()) {
		return kInvalidHandle;
	}

	CollectFinishedVoices();

	const SoundData& sound = sounds_[soundHandle];
	const WAVEFORMATEX* format = reinterpret_cast<const WAVEFORMATEX*>(sound.formatBytes.data());

	IXAudio2SourceVoice* voice = nullptr;
	HRESULT result = xaudio2_->CreateSourceVoice(&voice, format);
	if (FAILED(result) || !voice) {
		Logger::Log("[Audio] CreateSourceVoice failed. hr=" + std::to_string(result));
		return kInvalidHandle;
	}

	XAUDIO2_BUFFER buffer{};
	buffer.pAudioData = sound.pcmBytes.data();
	buffer.AudioBytes = static_cast<UINT32>(sound.pcmBytes.size());
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

	if (FAILED(voice->SubmitSourceBuffer(&buffer))) {
		voice->DestroyVoice();
		return kInvalidHandle;
	}

	voice->SetVolume((std::max)(volume, 0.0f));
	voice->Start();

	uint32_t voiceHandle = nextVoiceHandle_++;
	activeVoices_[voiceHandle] = voice;
	return voiceHandle;
}

void AudioManager::Stop(uint32_t voiceHandle) {
	auto found = activeVoices_.find(voiceHandle);
	if (found == activeVoices_.end()) {
		return;
	}
	found->second->Stop();
	found->second->DestroyVoice();
	activeVoices_.erase(found);
}

void AudioManager::SetVolume(uint32_t voiceHandle, float volume) {
	auto found = activeVoices_.find(voiceHandle);
	if (found == activeVoices_.end()) {
		return;
	}
	found->second->SetVolume((std::max)(volume, 0.0f));
}

bool AudioManager::IsPlaying(uint32_t voiceHandle) {
	CollectFinishedVoices();
	return activeVoices_.find(voiceHandle) != activeVoices_.end();
}

void AudioManager::CollectFinishedVoices() {
	for (auto it = activeVoices_.begin(); it != activeVoices_.end();) {
		XAUDIO2_VOICE_STATE state{};
		it->second->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		if (state.BuffersQueued == 0) {
			it->second->DestroyVoice();
			it = activeVoices_.erase(it);
		} else {
			++it;
		}
	}
}

} // namespace KujataEngine

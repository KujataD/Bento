#pragma once
#include <KujataEngine.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

/// <summary>
/// シーンをまたいで保持するプレイヤー設定([[GameSession]]と同じ流儀の置き場で、
/// GameModule.dll内の静的変数なのでシーンを作り直しても消えない)。
///
/// **アプリを終了しても残る。** 値は `Data/ProjectSettings/UserSettings.txt` に
/// `キー=値` の1行ずつで書き出し、初回アクセス時に読み戻す。
///
/// **読み書きに失敗しても既定値で必ず起動する。** 設定ファイルが無い初回起動、
/// 読み取り専用の場所へ置かれた配布物、壊れた行が混ざった場合のいずれでも、
/// 例外を投げずにその項目を捨てて先へ進む(設定のために起動できないのは本末転倒なので)。
/// </summary>
namespace GameSettings {

namespace detail {

/// <summary>設定ファイルの場所。</summary>
inline std::filesystem::path SettingsPath() {
	return KujataEngine::GetProjectDataRoot() / "ProjectSettings" / "UserSettings.txt";
}

/// <summary>一度でも読み込んだか。**読み込む前に必ずtrueにする**(再入で無限再帰しないように)。</summary>
inline bool& LoadedRef() {
	static bool loaded = false;
	return loaded;
}

} // namespace detail

// 前方宣言(アクセサから遅延読み込みを呼ぶため)。
inline void EnsureLoaded();

/// <summary>BGMの音量(0〜1)。変更したら [[GameAudio]]::RefreshVolumes() を呼ぶこと。</summary>
inline float& BgmVolumeRef() {
	EnsureLoaded();
	static float volume = 0.6f;
	return volume;
}

/// <summary>効果音の音量(0〜1)。再生のたびに参照されるので、変更は即座に効く。</summary>
inline float& SeVolumeRef() {
	EnsureLoaded();
	static float volume = 0.8f;
	return volume;
}

/// <summary>カメラ感度の倍率。1.0が既定。</summary>
inline float& CameraSensitivityRef() {
	EnsureLoaded();
	static float sensitivity = 1.0f;
	return sensitivity;
}

/// <summary>カメラの上下反転。</summary>
inline bool& InvertCameraYRef() {
	EnsureLoaded();
	static bool invert = false;
	return invert;
}

/// <summary>音量は0〜1、感度は0.3〜2.0に丸める(UIから直接±されるので、ここで一括して守る)。</summary>
inline void Clamp() {
	BgmVolumeRef() = std::clamp(BgmVolumeRef(), 0.0f, 1.0f);
	SeVolumeRef() = std::clamp(SeVolumeRef(), 0.0f, 1.0f);
	CameraSensitivityRef() = std::clamp(CameraSensitivityRef(), 0.3f, 2.0f);
}

/// <summary>
/// ファイルから読み戻す。**失敗しても何もしない**(既定値のまま進む)。
/// 壊れた行は1行ずつ捨てるので、途中まで正しいファイルは途中まで反映される。
/// </summary>
inline void Load() {
	std::ifstream file(detail::SettingsPath());
	if (!file) {
		return; // 初回起動。既定値で始める。
	}
	std::string line;
	while (std::getline(file, line)) {
		const std::size_t separator = line.find('=');
		if (separator == std::string::npos) {
			continue;
		}
		const std::string key = line.substr(0, separator);
		const std::string value = line.substr(separator + 1);
		try {
			if (key == "bgmVolume") {
				BgmVolumeRef() = std::stof(value);
			} else if (key == "seVolume") {
				SeVolumeRef() = std::stof(value);
			} else if (key == "cameraSensitivity") {
				CameraSensitivityRef() = std::stof(value);
			} else if (key == "invertCameraY") {
				InvertCameraYRef() = (value == "1");
			}
		} catch (...) {
			continue; // 数値として読めない行は捨てる。
		}
	}
	Clamp();
}

/// <summary>まだ読んでいなければ読む。全アクセサの入口で呼ばれる。</summary>
inline void EnsureLoaded() {
	if (detail::LoadedRef()) {
		return;
	}
	// **読む前に立てる。** Loadの中でアクセサを呼ぶので、ここで立てないと無限再帰する。
	detail::LoadedRef() = true;
	Load();
}

/// <summary>
/// ファイルへ書き出す。**失敗しても黙って諦める**(設定が保存できないだけで、遊べなくはならない)。
/// 設定画面を閉じたところで呼ぶ。
/// </summary>
inline void Save() {
	Clamp();
	std::error_code error;
	std::filesystem::create_directories(detail::SettingsPath().parent_path(), error);
	std::ofstream file(detail::SettingsPath(), std::ios::trunc);
	if (!file) {
		return;
	}
	file << "bgmVolume=" << BgmVolumeRef() << "\n";
	file << "seVolume=" << SeVolumeRef() << "\n";
	file << "cameraSensitivity=" << CameraSensitivityRef() << "\n";
	file << "invertCameraY=" << (InvertCameraYRef() ? 1 : 0) << "\n";
}

} // namespace GameSettings

#pragma once

#include "GameModule.h"
#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace KujataEngine {

/// <summary>
/// Game DLLを一時コピーしてLoadLibrary / FreeLibraryする
/// </summary>
class GameModuleLoader {
public:
	GameModuleLoader() = default;
	~GameModuleLoader();

	GameModuleLoader(const GameModuleLoader&) = delete;
	GameModuleLoader& operator=(const GameModuleLoader&) = delete;

	/// <summary>
	/// DLLを読み込む。
	/// copyDirectoryを指定すると別名コピーしてから読み込み、元のDLLを次回ビルドで上書きできるようにする(HotReload用)。
	/// copyDirectoryが空の場合はdllPathを直接読み込む(HotReloadしないゲーム単体ビルド用。一時フォルダを作らない)。
	/// </summary>
	GameModuleLoadResult Load(const std::filesystem::path& dllPath, const std::filesystem::path& copyDirectory);

	void Unload();

	bool IsLoaded() const { return moduleHandle_ != nullptr; }

	const GameModuleApi& GetApi() const { return api_; }

	const std::filesystem::path& GetLoadedCopiedDllPath() const { return loadedCopiedDllPath_; }

private:
	template <class T>
	bool LoadExport(const char* name, T& outFunc, std::string& message) const;

	static std::string GetLastWin32ErrorMessage(const std::string& prefix);
	static std::wstring ToWideString(const std::filesystem::path& path);

private:
	HMODULE moduleHandle_ = nullptr;
	GameModuleApi api_{};
	std::filesystem::path loadedCopiedDllPath_;
	uint32_t loadGeneration_ = 0;
};

} // namespace KujataEngine

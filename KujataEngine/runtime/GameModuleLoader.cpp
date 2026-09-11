#include "GameModuleLoader.h"
#include <filesystem>
#include <sstream>
#include <system_error>

namespace KujataEngine {

GameModuleLoader::~GameModuleLoader() {
	Unload();
}

GameModuleLoadResult GameModuleLoader::Load(const std::filesystem::path& dllPath, const std::filesystem::path& copyDirectory) {
	GameModuleLoadResult result{};
	result.sourceDllPath = dllPath;

	// 既に読み込んでいるDLLがあれば先に解放し、古い関数ポインタを残さない。
	Unload();

	std::error_code error;
	if (!std::filesystem::exists(dllPath, error)) {
		result.message = "[HotReload] Game DLL was not found: " + dllPath.string();
		return result;
	}

	// copyDirectory未指定(ゲーム単体ビルド)ではHotReloadしないため、一時フォルダを作らず直接読み込む。
	std::filesystem::path copiedDllPath = dllPath;
	if (!copyDirectory.empty()) {
		std::filesystem::create_directories(copyDirectory, error);
		if (error) {
			result.message = "[HotReload] Failed to create copy directory: " + error.message();
			return result;
		}

		++loadGeneration_;
		std::ostringstream fileName;
		// LoadLibrary中のDLLはWindows側にロックされるため、毎回別名へコピーしてから読み込む。
		// これにより元のビルド成果物は次回HotReloadで上書きできる。
		fileName << dllPath.stem().string() << "_pid" << GetCurrentProcessId() << "_tick" << GetTickCount64() << "_loaded_" << loadGeneration_
		         << dllPath.extension().string();
		copiedDllPath = copyDirectory / fileName.str();

		std::filesystem::copy_file(dllPath, copiedDllPath, std::filesystem::copy_options::overwrite_existing, error);
		if (error) {
			result.message = "[HotReload] Failed to copy DLL: " + error.message();
			return result;
		}

		std::filesystem::path pdbPath = dllPath;
		pdbPath.replace_extension(".pdb");
		bool shouldCopyPdb = false;
		// 古いPDBをコピーするとデバッガが誤ったシンボルを拾うため、DLL以上に新しい場合だけ一緒に運ぶ。
		if (std::filesystem::exists(pdbPath, error)) {
			std::error_code timeError;
			std::filesystem::file_time_type dllWriteTime = std::filesystem::last_write_time(dllPath, timeError);
			if (!timeError) {
				std::filesystem::file_time_type pdbWriteTime = std::filesystem::last_write_time(pdbPath, timeError);
				if (!timeError && pdbWriteTime >= dllWriteTime) {
					shouldCopyPdb = true;
				}
			}
		}

		if (shouldCopyPdb) {
			std::filesystem::path copiedPdbPath = copiedDllPath;
			copiedPdbPath.replace_extension(".pdb");
			std::filesystem::copy_file(pdbPath, copiedPdbPath, std::filesystem::copy_options::overwrite_existing, error);
		}
	}

	std::wstring copiedDllWidePath = ToWideString(copiedDllPath);
	// HotReload時はコピー先から読み込む。ビルド出力を直接LoadLibraryしないことがReload可能性を保つ。
	HMODULE handle = LoadLibraryW(copiedDllWidePath.c_str());
	if (!handle) {
		result.message = GetLastWin32ErrorMessage("[HotReload] Failed to LoadLibrary.");
		return result;
	}

	moduleHandle_ = handle;
	loadedCopiedDllPath_ = copiedDllPath;
	api_ = {};

	// GameModuleとはC APIだけで接続する。
	// C++クラスのABIをDLL境界で直接共有しないため、必要な入口だけを明示的に取得する。
	if (!LoadExport("RegisterGameComponents", api_.RegisterComponents, result.message)) {
		Unload();
		return result;
	}
	if (!LoadExport("UnregisterGameComponents", api_.UnregisterComponents, result.message)) {
		Unload();
		return result;
	}
	if (!LoadExport("CreateGameScene", api_.CreateScene, result.message)) {
		Unload();
		return result;
	}
	if (!LoadExport("DestroyGameScene", api_.DestroyScene, result.message)) {
		Unload();
		return result;
	}

	result.succeeded = true;
	result.copiedDllPath = copiedDllPath;
	result.message = "[HotReload] Game DLL loaded.";
	return result;
}

void GameModuleLoader::Unload() {
	if (moduleHandle_) {
		// FreeLibrary後もOSが即座にファイルハンドルを手放すとは限らないため、
		// 次回Loadでは別名コピーを使う設計にしている。
		FreeLibrary(moduleHandle_);
	}

	moduleHandle_ = nullptr;
	api_ = {};
	loadedCopiedDllPath_.clear();
}

template <class T>
bool GameModuleLoader::LoadExport(const char* name, T& outFunc, std::string& message) const {
	if (!moduleHandle_) {
		message = "[HotReload] DLL is not loaded.";
		return false;
	}

	// exportが欠けているDLLはEngine側の期待するGameModule契約を満たしていない。
	FARPROC proc = GetProcAddress(moduleHandle_, name);
	if (!proc) {
		message = std::string("[HotReload] Missing export: ") + name + ".";
		return false;
	}

	outFunc = reinterpret_cast<T>(proc);
	return true;
}

std::string GameModuleLoader::GetLastWin32ErrorMessage(const std::string& prefix) {
	DWORD errorCode = GetLastError();
	std::ostringstream message;
	message << prefix << " Win32Error=" << errorCode;
	return message.str();
}

std::wstring GameModuleLoader::ToWideString(const std::filesystem::path& path) {
	return path.wstring();
}

} // namespace KujataEngine

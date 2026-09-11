#include "ProjectPath.h"
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <Windows.h>
#include <shellapi.h>

namespace KujataEngine {

namespace {

std::filesystem::path DetectEngineRoot() {
	std::error_code error;
	std::filesystem::path current = std::filesystem::current_path(error);
	if (error) {
		return std::filesystem::path(".");
	}

	std::filesystem::path cursor = current;
	while (!cursor.empty()) {
		if (std::filesystem::exists(cursor / "KujataEngine.vcxproj")) {
			return NormalizeEditorPath(cursor);
		}

		std::filesystem::path directXGameProject = cursor / "DirectXGame" / "KujataEngine.vcxproj";
		if (std::filesystem::exists(directXGameProject)) {
			return NormalizeEditorPath(cursor / "DirectXGame");
		}

		if (std::filesystem::exists(cursor / "KujataEngine.sln")) {
			std::filesystem::path directXGameDirectory = cursor / "DirectXGame";
			if (std::filesystem::exists(directXGameDirectory)) {
				return NormalizeEditorPath(directXGameDirectory);
			}
			return NormalizeEditorPath(cursor);
		}

		std::filesystem::path parent = cursor.parent_path();
		if (parent == cursor) {
			break;
		}
		cursor = parent;
	}

	return NormalizeEditorPath(current);
}

// 起動引数 `--project <フォルダ>` を探す。相対パスは起動時カレント基準。
std::filesystem::path FindProjectArgument() {
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) {
		return {};
	}

	std::filesystem::path result;
	for (int i = 1; i + 1 < argc; ++i) {
		if (std::wstring_view(argv[i]) == L"--project") {
			result = argv[i + 1];
			break;
		}
	}
	LocalFree(argv);
	return result;
}

std::filesystem::path DetectActiveProjectRoot() {
	std::filesystem::path requested = FindProjectArgument();
	if (requested.empty()) {
		std::error_code error;
		std::filesystem::path gameDirectory = GetEngineRoot().parent_path() / "Game";
		if (std::filesystem::is_directory(gameDirectory, error)) {
			return NormalizeEditorPath(gameDirectory);
		}
		return GetEngineRoot();
	}

	std::error_code error;
	std::filesystem::path absolutePath = std::filesystem::absolute(requested, error);
	if (error || !std::filesystem::is_directory(absolutePath, error)) {
		// 黙ってエンジン側を開くと、指定したつもりのプロジェクトを編集していると誤解したまま作業してしまう。
		std::wstring message = L"--project で指定されたフォルダが見つかりません。\nエンジンのフォルダを開きます。\n\n" + requested.wstring();
		MessageBoxW(nullptr, message.c_str(), L"KujataEngine", MB_OK | MB_ICONWARNING);
		return GetEngineRoot();
	}
	return NormalizeEditorPath(absolutePath);
}

} // namespace

std::filesystem::path NormalizeEditorPath(const std::filesystem::path& path) {
	std::error_code error;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
	if (error) {
		return path.lexically_normal();
	}
	return normalized.lexically_normal();
}

std::filesystem::path GetEngineRoot() {
	// 探索はカレント依存なので、ファイルダイアログ等でカレントが変わる前の起動時の結果を使い続ける。
	static const std::filesystem::path root = DetectEngineRoot();
	return root;
}

std::filesystem::path GetEngineDataRoot() {
	return GetEngineRoot() / "EngineData";
}

std::filesystem::path GetActiveProjectRoot() {
	static const std::filesystem::path root = DetectActiveProjectRoot();
	return root;
}

std::filesystem::path GetEditorIconDirectory() {
	return GetEngineRoot() / "KujataEngine" / "resources" / "images";
}

std::filesystem::path GetExecutableDirectory() {
	std::vector<wchar_t> buffer(MAX_PATH);
	for (;;) {
		DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0) {
			return std::filesystem::path(".");
		}
		if (length < buffer.size()) {
			return std::filesystem::path(buffer.data()).parent_path();
		}
		// バッファ不足(ERROR_INSUFFICIENT_BUFFER)時は拡大して再試行する。
		buffer.resize(buffer.size() * 2);
	}
}

std::filesystem::path GetProjectDataRoot() {
	return GetActiveProjectRoot() / "Data";
}

} // namespace KujataEngine

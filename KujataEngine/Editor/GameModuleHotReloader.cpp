#include "GameModuleHotReloader.h"
#include "ImGuiManager.h"
#include "../base/ProjectPath.h"
#include "../scene/ComponentFactory.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace KujataEngine {

namespace {

void Log(const std::string& message) {
#ifdef USE_IMGUI
	ImGuiManager::GetInstance()->AddConsoleLog(message);
#else
	(void)message;
#endif // USE_IMGUI
}

std::filesystem::path FindMSBuildPath() {
	// Visual Studioのインストール先は環境ごとに異なるため、既知の候補を順に探す。
	// 見つからない場合はPATH上のMSBuild.exeへフォールバックする。
	std::vector<std::filesystem::path> candidates = {
	    "C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe",
	    "C:/Program Files/Microsoft Visual Studio/18/Professional/MSBuild/Current/Bin/MSBuild.exe",
	    "C:/Program Files/Microsoft Visual Studio/18/Enterprise/MSBuild/Current/Bin/MSBuild.exe",
	    "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe",
	    "C:/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/MSBuild.exe",
	    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin/MSBuild.exe",
	};

	for (const std::filesystem::path& candidate : candidates) {
		if (std::filesystem::exists(candidate)) {
			return candidate;
		}
	}

	return "MSBuild.exe";
}

std::wstring QuoteCommandArgument(const std::filesystem::path& path) {
	// CreateProcessWへ渡すコマンドライン内で、空白を含むパスを安全に扱う。
	std::wstring quoted = L"\"";
	quoted += path.wstring();
	quoted += L"\"";
	return quoted;
}

std::wstring ToMSBuildDirectoryProperty(const std::filesystem::path& path) {
	// MSBuildのDirectory系Propertyは末尾区切りがないと連結時に壊れやすい。
	// スラッシュ表記へ揃え、末尾に必ず'/'を付ける。
	std::wstring text = path.lexically_normal().generic_wstring();
	if (!text.empty() && text.back() != L'/') {
		text += L'/';
	}
	return text;
}

bool RunProcessAndWait(
    const std::filesystem::path& applicationPath,
    std::wstring commandLine,
    const std::filesystem::path& workingDirectory,
    const std::filesystem::path& outputLogPath,
    DWORD& exitCode,
    DWORD& win32Error) {
	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);

	// MSBuildの標準出力と標準エラーを同じログへ流し、Editor上のExitCodeだけでは追えない失敗原因を残す。
	HANDLE logHandle = INVALID_HANDLE_VALUE;
	HANDLE nullInputHandle = INVALID_HANDLE_VALUE;
	BOOL inheritHandles = FALSE;

	if (!outputLogPath.empty()) {
		std::error_code error;
		std::filesystem::create_directories(outputLogPath.parent_path(), error);

		SECURITY_ATTRIBUTES securityAttributes{};
		securityAttributes.nLength = sizeof(securityAttributes);
		securityAttributes.bInheritHandle = TRUE;

		// ログファイルは毎回作り直す。前回のエラーが残ると原因判断を誤るため。
		logHandle = CreateFileW(
		    outputLogPath.wstring().c_str(),
		    GENERIC_WRITE,
		    FILE_SHARE_READ,
		    &securityAttributes,
		    CREATE_ALWAYS,
		    FILE_ATTRIBUTE_NORMAL,
		    nullptr);

		if (logHandle == INVALID_HANDLE_VALUE) {
			win32Error = GetLastError();
			return false;
		}

		// MSBuildが標準入力待ちにならないよう、入力はNULへつなぐ。
		nullInputHandle = CreateFileW(
		    L"NUL",
		    GENERIC_READ,
		    FILE_SHARE_READ | FILE_SHARE_WRITE,
		    &securityAttributes,
		    OPEN_EXISTING,
		    FILE_ATTRIBUTE_NORMAL,
		    nullptr);

		startupInfo.dwFlags |= STARTF_USESTDHANDLES;
		startupInfo.hStdOutput = logHandle;
		startupInfo.hStdError = logHandle;
		if (nullInputHandle != INVALID_HANDLE_VALUE) {
			startupInfo.hStdInput = nullInputHandle;
		} else {
			startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		}
		inheritHandles = TRUE;
	}

	PROCESS_INFORMATION processInfo{};
	std::wstring applicationPathText = applicationPath.wstring();
	std::wstring workingDirectoryText = workingDirectory.wstring();

	// applicationPathをlpApplicationNameへ直接渡し、コマンドライン先頭の引用符解釈に依存しないようにする。
	BOOL created = CreateProcessW(
	    applicationPathText.c_str(),
	    commandLine.data(),
	    nullptr,
	    nullptr,
	    inheritHandles,
	    CREATE_NO_WINDOW,
	    nullptr,
	    workingDirectoryText.c_str(),
	    &startupInfo,
	    &processInfo);

	if (!created) {
		win32Error = GetLastError();
		if (nullInputHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(nullInputHandle);
		}
		if (logHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(logHandle);
		}
		return false;
	}

	// HotReloadはビルド完了後にDLLを読み直すため、MSBuildの終了まで同期的に待つ。
	WaitForSingleObject(processInfo.hProcess, INFINITE);

	BOOL gotExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode);
	if (!gotExitCode) {
		win32Error = GetLastError();
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		if (nullInputHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(nullInputHandle);
		}
		if (logHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(logHandle);
		}
		return false;
	}

	// 子プロセスとログ用ハンドルはこの関数内で必ず閉じる。
	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	if (nullInputHandle != INVALID_HANDLE_VALUE) {
		CloseHandle(nullInputHandle);
	}
	if (logHandle != INVALID_HANDLE_VALUE) {
		CloseHandle(logHandle);
	}
	return true;
}

} // namespace

bool GameModuleHotReloader::BuildNextGeneration(std::filesystem::path& outDllPath) {
	outDllPath.clear();

	// GameModuleはEditor本体と別プロジェクトとしてビルドする。
	// Editor側はDLLのC APIだけを通してScene/Componentを受け取る。
	std::filesystem::path gameModuleProjectPath = GetProjectPath();
	if (!std::filesystem::exists(gameModuleProjectPath)) {
		Log("[HotReload] GameModule project was not found: " + gameModuleProjectPath.string());
		return false;
	}

	std::filesystem::path solutionDirectory = GetEngineRoot().parent_path();
	std::wstring solutionDirectoryText = ToMSBuildDirectoryProperty(solutionDirectory);
	if (solutionDirectoryText.empty()) {
		Log("[HotReload] Solution directory was not found.");
		return false;
	}

	// プロセスIDとTickを含めた世代名にし、Reloadのたびに出力先を変える。
	// 固定DLL/PDBを上書きしないことが、このHotReload方式の要点。
	std::ostringstream generationName;
	generationName << "pid" << GetCurrentProcessId() << "_tick" << GetTickCount64();

	std::filesystem::path buildDirectory = GetHotReloadBuildRoot() / generationName.str();
	std::filesystem::path buildOutputDirectory = buildDirectory / "bin";
	std::filesystem::path buildIntermediateDirectory = buildDirectory / "obj";

	// OutDirとIntDirを両方分離し、DLL/PDB/ILK/OBJが前世代や固定出力と衝突しないようにする。
	std::error_code error;
	std::filesystem::create_directories(buildOutputDirectory, error);
	if (error) {
		Log("[HotReload] Failed to create build output directory: " + error.message());
		return false;
	}
	std::filesystem::create_directories(buildIntermediateDirectory, error);
	if (error) {
		Log("[HotReload] Failed to create build intermediate directory: " + error.message());
		return false;
	}

	std::wstring outDirText = ToMSBuildDirectoryProperty(buildOutputDirectory);
	std::wstring intDirText = ToMSBuildDirectoryProperty(buildIntermediateDirectory);

#ifdef _DEBUG
	const wchar_t* configuration = L"Debug";
#else
	const wchar_t* configuration = L"Release";
#endif

	std::filesystem::path msbuildPath = FindMSBuildPath();
	std::wostringstream commandLine;
	// SolutionDirはGameModule単体ビルドでは自動設定がずれるため明示する。
	// OutDir/IntDirは世代ディレクトリへ向け、Load中のDLLを上書きしない。
	commandLine << QuoteCommandArgument(msbuildPath) << L" " << QuoteCommandArgument(gameModuleProjectPath)
	            << L" /p:Configuration=" << configuration << L" /p:Platform=x64"
	            << L" /p:SolutionDir=" << solutionDirectoryText
	            << L" /p:OutDir=" << outDirText
	            << L" /p:IntDir=" << intDirText
	            << L" /m /nologo";

	Log("[HotReload] Build GameModule to temp directory...");
	Log("[HotReload] Build output: " + buildOutputDirectory.string());
	std::filesystem::path buildLogPath = buildDirectory / "GameModuleBuild.log";
	Log("[HotReload] MSBuild log: " + buildLogPath.string());

	DWORD exitCode = 0;
	DWORD win32Error = 0;
	std::filesystem::path workingDirectory = solutionDirectory;
	// 失敗時はMSBuildの詳細ログをConsoleから辿れるように、必ずファイルへ保存する。
	if (!RunProcessAndWait(msbuildPath, commandLine.str(), workingDirectory, buildLogPath, exitCode, win32Error)) {
		Log("[HotReload] Failed to launch MSBuild. Win32Error=" + std::to_string(win32Error));
		return false;
	}

	if (exitCode != 0) {
		Log("[HotReload] GameModule build failed. ExitCode=" + std::to_string(exitCode));
		Log("[HotReload] See build log: " + buildLogPath.string());
		return false;
	}

	std::filesystem::path builtDllPath = buildOutputDirectory / "GameModule.dll";
	// MSBuildが成功を返しても、期待したDLLがない場合はLoadLibrary前に止める。
	if (!std::filesystem::exists(builtDllPath)) {
		Log("[HotReload] Built GameModule DLL was not found: " + builtDllPath.string());
		return false;
	}

	outDllPath = builtDllPath;
	Log("[HotReload] GameModule build succeeded.");
	return true;
}

bool GameModuleHotReloader::LoadForEditor() {
	if (loader_.IsLoaded()) {
		return true;
	}

	// 起動直後はまだHotReload用の一時ビルドが存在しないため、標準配置のDLLを読み込む。
	std::filesystem::path dllPath = GetDllPath();
	std::filesystem::path copyDirectory = GetCopyDirectory();
	Log("[GameModule] Load DLL: " + dllPath.string());

	GameModuleLoadResult loadResult = loader_.Load(dllPath, copyDirectory);
	if (!loadResult.succeeded) {
		Log("[GameModule] Failed to load: " + loadResult.message);
		return false;
	}

	Log("[GameModule] Loaded: " + loadResult.copiedDllPath.string());

	const GameModuleApi& api = loader_.GetApi();
	if (!api.RegisterComponents) {
		Log("[GameModule] RegisterGameComponents is null.");
		UnregisterAndUnload();
		return false;
	}

	Log("[GameModule] Register game components.");
	api.RegisterComponents(ComponentFactory::GetInstance());
	return true;
}

bool GameModuleHotReloader::LoadBuiltDll(const std::filesystem::path& dllPath) {
	std::filesystem::path copyDirectory = GetCopyDirectory();
	Log("[HotReload] Copy DLL: " + dllPath.string());

	// GameModuleLoaderは読み込み前にさらに一時コピーを作る。
	// ビルド成果物とLoadLibrary中のDLLを分け、次回ビルドで上書きできるようにする。
	GameModuleLoadResult loadResult = loader_.Load(dllPath, copyDirectory);
	if (!loadResult.succeeded) {
		Log(loadResult.message);
		return false;
	}

	Log("[HotReload] Load new DLL: " + loadResult.copiedDllPath.string());

	const GameModuleApi& api = loader_.GetApi();
	if (!api.RegisterComponents) {
		Log("[HotReload] RegisterGameComponents is null.");
		UnregisterAndUnload();
		return false;
	}

	Log("[HotReload] Register game components.");
	api.RegisterComponents(ComponentFactory::GetInstance());
	return true;
}

void GameModuleHotReloader::UnregisterAndUnload() {
	ComponentFactory& factory = ComponentFactory::GetInstance();
	if (loader_.IsLoaded()) {
		// DLL側に登録解除の機会を渡してから、Engine側でもmoduleName単位で掃除する。
		Log("[HotReload] Unregister game components.");
		const GameModuleApi& api = loader_.GetApi();
		if (api.UnregisterComponents) {
			api.UnregisterComponents(factory);
		}
	}

	factory.UnregisterByModule("GameModule");

	if (loader_.IsLoaded()) {
		Log("[HotReload] Unload old DLL.");
		loader_.Unload();
	}
}

std::filesystem::path GameModuleHotReloader::GetProjectPath() const {
	return GetActiveProjectRoot() / "GameModule" / "GameModule.vcxproj";
}

std::filesystem::path GameModuleHotReloader::GetDllPath() const {
	// 開いているプロジェクトのビルド出力を最優先で探す。exeの隣を先に見ると、
	// 別プロジェクトを開いても「最後にexeの隣へ置かれたDLL」が黙って読まれてしまう。
	// GameModule.dllはビルド構成(Debug/Release)ごとに別フォルダへ出力される。
	// exeと同じ構成のDLLを読み込まないとstd::string等のABIが食い違いクラッシュするため、
	// exe自身の構成に対応するサブフォルダを選ぶ(GameModule.vcxprojのOutDirと一致させること)。
#ifdef _DEBUG
	const char* configuration = "Debug";
#else
	const char* configuration = "Release";
#endif
	std::filesystem::path projectDll = GetActiveProjectRoot() / "GameModule" / "bin" / configuration / "GameModule.dll";
	std::error_code error;
	if (std::filesystem::exists(projectDll, error)) {
		return projectDll;
	}

	// 配布パッケージにはプロジェクトのビルド出力が無く、exeと同じフォルダにGameModule.dllを置く。
	return GetExecutableDirectory() / "GameModule.dll";
}

std::filesystem::path GameModuleHotReloader::GetHotReloadBuildRoot() const {
	return GetCopyDirectory() / "Build";
}

std::filesystem::path GameModuleHotReloader::GetCopyDirectory() const {
#ifdef USE_IMGUI
	return GetActiveProjectRoot() / "Temp" / "HotReload";
#else
	// エディタUI無し(ゲーム単体)ビルドではHotReloadしないため、コピー先を持たない。
	// GameModuleLoaderは空のcopyDirectoryならDLLを直接読み込み、Tempフォルダを作らない。
	return {};
#endif // USE_IMGUI
}

} // namespace KujataEngine

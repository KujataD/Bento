#pragma once

#include "../runtime/GameModule.h"
#include "../runtime/GameModuleLoader.h"
#include <filesystem>

namespace KujataEngine {

/// <summary>
/// GameModule DLLの持ち主。起動時の読み込み、HotReload用の世代別ビルド(MSBuild)、
/// Component登録と解除、DLLの解放を受け持つ。
/// Sceneには触らない。Sceneの退避・破棄・作り直しは呼び出し側(EditorApplication)が行い、
/// その間にこのクラスでDLLを差し替える。
/// </summary>
class GameModuleHotReloader {
public:
	/// <summary>
	/// 起動時に標準配置のGameModuleを読み込み、Componentを登録する(読み込み済みなら何もしない)
	/// </summary>
	bool LoadForEditor();

	/// <summary>
	/// HotReload用の世代別一時ディレクトリへGameModuleをビルドする
	/// </summary>
	bool BuildNextGeneration(std::filesystem::path& outDllPath);

	/// <summary>
	/// BuildNextGenerationで作ったDLLを読み込み、Componentを登録する。
	/// 旧DLLはUnregisterAndUnloadで先に解放しておくこと。
	/// </summary>
	bool LoadBuiltDll(const std::filesystem::path& dllPath);

	/// <summary>
	/// GameModule由来のComponent登録を解除し、読み込み済みDLLを解放する
	/// </summary>
	void UnregisterAndUnload();

	bool IsLoaded() const { return loader_.IsLoaded(); }

	const GameModuleApi& GetApi() const { return loader_.GetApi(); }

private:
	std::filesystem::path GetProjectPath() const;
	std::filesystem::path GetDllPath() const;
	std::filesystem::path GetHotReloadBuildRoot() const;
	std::filesystem::path GetCopyDirectory() const;

	GameModuleLoader loader_;
};

} // namespace KujataEngine

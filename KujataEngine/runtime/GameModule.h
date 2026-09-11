#pragma once

#include <filesystem>
#include <string>

namespace KujataEngine {

class ComponentFactory;
class Scene;

/// <summary>
/// Game DLLから取得する関数群
/// </summary>
struct GameModuleApi {
	using RegisterComponentsFunc = void (*)(ComponentFactory&);
	using UnregisterComponentsFunc = void (*)(ComponentFactory&);
	using CreateSceneFunc = Scene* (*)();
	using DestroySceneFunc = void (*)(Scene*);

	RegisterComponentsFunc RegisterComponents = nullptr;
	UnregisterComponentsFunc UnregisterComponents = nullptr;
	CreateSceneFunc CreateScene = nullptr;
	DestroySceneFunc DestroyScene = nullptr;
};

/// <summary>
/// Game DLLの読み込み結果
/// </summary>
struct GameModuleLoadResult {
	bool succeeded = false;
	std::filesystem::path sourceDllPath;
	std::filesystem::path copiedDllPath;
	std::string message;
};

} // namespace KujataEngine

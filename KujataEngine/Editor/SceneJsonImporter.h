#pragma once

#include <filesystem>
#include <string>

namespace KujataEngine {

class Scene;

/// <summary>
/// ProjectDir配下のScene JSONを読み込み、Editor上のSceneへ適用する
/// </summary>
class SceneJsonImporter {
public:
	struct ImportResult {
		bool succeeded = false;
		bool imported = false;
		std::filesystem::path sourceDirectory;
		size_t gameObjectCount = 0;
		size_t componentCount = 0;
		std::string message;
	};

	/// <summary>
	/// SceneごとのJSONとGameObjectごとのJSONを読み込み、Sceneへ反映する
	/// </summary>
	static ImportResult ImportScene(Scene& scene, const std::filesystem::path& projectRoot);

	/// <summary>
	/// メモリ上のScene JSON文字列をSceneへ反映する
	/// </summary>
	static ImportResult ApplySceneJsonString(Scene& scene, const std::string& sceneJsonText);
};

} // namespace KujataEngine

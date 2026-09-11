#pragma once

#include <filesystem>
#include <string>

namespace KujataEngine {

class Scene;

/// <summary>
/// Editor上のScene / GameObject情報をProjectDir配下へJSONとして書き出す
/// </summary>
class SceneJsonExporter {
public:
	struct ExportResult {
		bool succeeded = false;
		std::filesystem::path outputDirectory;
		size_t sceneFileCount = 0;
		size_t gameObjectFileCount = 0;
		std::string message;
	};

	/// <summary>
	/// SceneごとのJSONとGameObjectごとのJSONを作成する
	/// </summary>
	static ExportResult ExportScene(const Scene& scene, const std::filesystem::path& projectRoot);
};

} // namespace KujataEngine

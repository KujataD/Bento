#pragma once

#include "../../externals/nlohmann/json.hpp"
#include <filesystem>
#include <string>

namespace KujataEngine {

class Scene;
class Component;

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

	/// <summary>
	/// 1つのComponentへプロパティのJSONを読み込ませる。シーンの読み込みと同じ手順
	/// (ReadJson → OnAfterReadJson → 参照フィールドの instanceId を実物へ解決)を、そのComponentだけに行う。
	/// propertiesは WriteJson で書き出した形(全フィールド)を渡すこと。無いキーは今の値のまま残る。
	/// </summary>
	static void ApplyComponentProperties(Scene& scene, Component& component, const nlohmann::json& properties);
};

} // namespace KujataEngine
